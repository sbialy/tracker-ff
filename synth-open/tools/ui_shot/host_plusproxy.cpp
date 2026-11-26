// Tracker+ simulator, round 5 (ref/plus-emulator-plan.md phase 3b in full): the host polysynth:: proxy. With PLATFORM=IMX BRIDGE=1 the plus
// core is built as a Tracker+ image compiles it (no FEATURE_SYNTH_OPEN), so Synth.cpp, the synth editor pages, synthPlay and the stock
// part output path of synth_PolySynth.cpp run unchanged and talk to polysynth::Synth, PatchStore, Patch and Instrument. This file gives
// them those objects: thin host classes over ONE guest of the closed libPolySynth under Unicorn (synth-open/tools/plus_synth/harness.elf,
// its h2_* API). Host tooling only, never firmware.
// - Patches are handles into the guest's table. The metadata is copied out (h2_meta) per patch, and every Param::valueFormatter is a guest
//   call (h2_format), so the stock value text (DIRT included) is the lib's own.
// - The lib's FileSystem calls come back through h2_fs_trap (a code hook) and go to the firmware's FileSystemAdapter the core passed in
//   addFileSystem, which reads and writes the sim card (host_simcard.cpp).
// - MIDI: each instrument subscribes to the core's MidiAdapter and hands its events to the guest instrument at once (h2_event).
// UISHOT_PLUS_SYNTH names the harness ELF (default: the build's own, Makefile UISHOT_PLUS_SYNTH_DEFAULT). UISHOT_PLUS_DUMP=<dir>: part<N>.events / part<N>.f32 as
// host_plussynth.cpp writes them, the input of run.py --events (params as the guest's stored values, diffed per block against the last
// logged ones, the first block against a fresh init patch of the part's type).
#include <unicorn/unicorn.h>
#include "../../../core/source/mtTuning.h"

#include "PolySynth/Synth.h"
#include "PolySynth/Instrument.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/PatchPool.h"
#include "PolySynth/Patch.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/FileSystem.h"
#include "PolySynth/Mixer.h"
#include "PolySynth/Grid.h"
#include "PolySynth/GridMapper.h"
#include "PolySynth/GridMapperStore.h"
#include "PolySynth/SampleStore.h"
#include "PolySynth/config.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
constexpr uint64_t kRam = 0x20000000, kRamSize = 32u << 20, kRet = 0x00010000;
constexpr int kBlock = polysynth::kAudioBufferSize;
constexpr int kMaxParts = 16;

uint32_t bitsOf(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
float floatOf(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
uint32_t optBits(const tiny_optional<float>& v) { return v ? bitsOf(*v) : bitsOf(__builtin_nanf("")); }
tiny_optional<float> bitsOpt(int b) { const float f = floatOf(uint32_t(b)); return std::isnan(f) ? tiny_optional<float>() : tiny_optional<float>(f); }

struct Guest
{
    uc_engine* uc = nullptr;
    std::map<std::string, uint32_t> sym;
    bool dead = false;
    int depth = 0;
    std::vector<std::unique_ptr<polysynth::FileSystem>> fs { 4 };
    std::map<int, polysynth::FileSystem::FileHandle> files;
    int nextFile = 1;

    // the project tuning into the guest's PlugSynthVoice (tuning::cfg and tuning::degMc of the harness): the guest is its own image, the host's
    // Project Settings tuning has to cross before every event and block a plugin engine can take a note on
    void syncTuning()
    {
        static_assert(sizeof(tuning::Cfg) == 16, "tuning::Cfg has the M7 layout");
        auto c = sym.find("_ZN6tuning3cfgE"), d = sym.find("_ZN6tuning5degMcE");
        if (c == sym.end() || d == sym.end()) return;
        uc_mem_write(uc, c->second, &tuning::cfg, sizeof tuning::cfg);
        uc_mem_write(uc, d->second, tuning::degMc, sizeof tuning::degMc);
    }
    uint32_t at(const char* name)
    {
        auto it = sym.find(name);
        if (it == sym.end()) { fprintf(stderr, "plusproxy: no symbol %s\n", name); abort(); }
        return it->second;
    }
    int call(const char* name, int a0 = 0, int a1 = 0, int a2 = 0, int a3 = 0)
    {
        if (dead) return -1;
        const uint32_t pc0 = at(name);
        int32_t args[4] = { a0, a1, a2, a3 };
        static const int regs[4] = { UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3 };
        for (int i = 0; i < 4; i++) uc_reg_write(uc, regs[i], &args[i]);
        uint32_t sp = uint32_t(kRam + kRamSize - 64), lr = uint32_t(kRet | 1);
        uc_reg_write(uc, UC_ARM_REG_SP, &sp);
        uc_reg_write(uc, UC_ARM_REG_LR, &lr);
        uc_err e = uc_emu_start(uc, pc0 | 1, kRet, 0, 0);
        if (e != UC_ERR_OK)
        {
            uint32_t pc = 0;
            uc_reg_read(uc, UC_ARM_REG_PC, &pc);
            fprintf(stderr, "plusproxy: %s faulted: %s at pc 0x%08x\n", name, uc_strerror(e), pc);
            dead = true;
            return -1;
        }
        int32_t r0 = 0;
        uc_reg_read(uc, UC_ARM_REG_R0, &r0);
        return r0;
    }
    void read(const char* name, void* dst, size_t n, size_t off = 0) { uc_mem_read(uc, at(name) + off, dst, n); }
    void write(const char* name, const void* src, size_t n) { uc_mem_write(uc, at(name), src, n); }
    void putStr(const std::string& s)
    {
        std::string z = s.substr(0, 16383);
        z.push_back(0);
        write("h2_buf", z.data(), z.size());
    }
    std::string bufStr(int len)
    {
        if (len <= 0) return {};
        std::string s(size_t(len), '\0');
        read("h2_buf", s.data(), size_t(len));
        return s;
    }
};

Guest* g_guest = nullptr;

// h2_fs_trap: one FileSystem request of the lib (harness.cpp HostFs), served by the core's FileSystemAdapter for that FileSystemId
struct FsReq { int32_t op, fs, handle, arg, ret; };
void fsHook(uc_engine*, uint64_t, uint32_t, void* user)
{
    Guest& g = *static_cast<Guest*>(user);
    FsReq q;
    g.read("h2_fs", &q, sizeof q);
    char path[512] = { 0 };
    g.read("h2_path", path, sizeof path - 1);
    polysynth::FileSystem* fs = q.fs >= 0 && q.fs < 4 ? g.fs[size_t(q.fs)].get() : nullptr;
    q.ret = -1;
    static std::vector<uint8_t> buf(16384);
    if (fs)
    {
        switch (q.op)
        {
            case 1:
            {
                auto h = fs->open(path, static_cast<polysynth::FileSystem::OpenOptions>(q.arg));
                if (h) { q.ret = g.nextFile++; g.files[q.ret] = *h; }
                else q.ret = 0;
                break;
            }
            case 2:
                if (g.files.count(q.handle)) { fs->close(g.files[q.handle]); g.files.erase(q.handle); }
                q.ret = 0;
                break;
            case 3:
                if (g.files.count(q.handle) && q.arg >= 0 && q.arg <= int(buf.size()))
                {
                    g.read("h2_fsbuf", buf.data(), size_t(q.arg));
                    q.ret = fs->write(g.files[q.handle], std::span<const uint8_t>(buf.data(), size_t(q.arg))) ? 1 : 0;
                }
                break;
            case 4:
                if (g.files.count(q.handle) && q.arg >= 0 && q.arg <= int(buf.size()))
                {
                    q.ret = fs->read(g.files[q.handle], std::span<uint8_t>(buf.data(), size_t(q.arg)));
                    if (q.ret > 0) uc_mem_write(g.uc, g.at("h2_fsbuf"), buf.data(), size_t(q.ret));
                }
                break;
            case 5: q.ret = g.files.count(q.handle) && fs->seek(g.files[q.handle], q.arg) ? 1 : 0; break;
            case 6: fs->createFolder(path); q.ret = 0; break;
            case 7:
            {
                auto v = fs->listFiles(path, nullptr, q.arg < 0 ? tiny_optional<int>() : tiny_optional<int>(q.arg));
                std::vector<uint8_t> out;
                int n = 0;
                for (auto s : v)
                {
                    if (out.size() + s.size() + 1 > buf.size()) break;
                    out.insert(out.end(), s.begin(), s.end());
                    out.push_back(0);
                    n++;
                }
                if (!out.empty()) uc_mem_write(g.uc, g.at("h2_fsbuf"), out.data(), out.size());
                q.ret = n;
                break;
            }
            default: break;
        }
    }
    uc_mem_write(g.uc, g.at("h2_fs"), &q, sizeof q);
}

Guest* guest()
{
    if (g_guest) return g_guest;
    const char* elf = getenv("UISHOT_PLUS_SYNTH");
#ifdef UISHOT_PLUS_SYNTH_DEFAULT
    if (!elf || !*elf) elf = UISHOT_PLUS_SYNTH_DEFAULT;   // the build's own harness (Makefile)
#endif
    if (!elf || !*elf) { fprintf(stderr, "plusproxy: no closed engine harness (UISHOT_PLUS_SYNTH=<harness.elf>)\n"); abort(); }
    std::vector<uint8_t> img;
    FILE* f = fopen(elf, "rb");
    if (!f) { fprintf(stderr, "plusproxy: cannot open %s\n", elf); abort(); }
    fseek(f, 0, SEEK_END);
    img.resize(size_t(ftell(f)));
    fseek(f, 0, SEEK_SET);
    if (fread(img.data(), 1, img.size(), f) != img.size()) img.clear();
    fclose(f);
    auto* g = new Guest();
    std::string nm = elf;
    if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, ".elf") == 0) nm.resize(nm.size() - 4);
    nm += ".nm";
    FILE* n = fopen(nm.c_str(), "r");
    if (!n) { fprintf(stderr, "plusproxy: cannot open %s\n", nm.c_str()); abort(); }
    char a[64], t[8], s[512];
    while (fscanf(n, "%63s %7s %511s", a, t, s) == 3) g->sym[s] = uint32_t(strtoul(a, nullptr, 16));
    fclose(n);
    if (uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &g->uc) != UC_ERR_OK) abort();
    uc_ctl_set_cpu_model(g->uc, UC_CPU_ARM_MAX);
    uc_mem_map(g->uc, kRam, kRamSize, UC_PROT_ALL);
    uc_mem_map(g->uc, kRet, 0x1000, UC_PROT_ALL);
    uint32_t cpacr = 0;
    uc_reg_read(g->uc, UC_ARM_REG_C1_C0_2, &cpacr);
    cpacr |= 0xFu << 20;
    uc_reg_write(g->uc, UC_ARM_REG_C1_C0_2, &cpacr);
    uint32_t fpexc = 0x40000000;
    uc_reg_write(g->uc, UC_ARM_REG_FPEXC, &fpexc);
    const uint8_t* d = img.data();
    if (img.size() < 52 || memcmp(d, "\x7f" "ELF", 4) != 0) { fprintf(stderr, "plusproxy: %s is no ELF\n", elf); abort(); }
    uint32_t phoff;
    uint16_t phentsize, phnum;
    memcpy(&phoff, d + 28, 4);
    memcpy(&phentsize, d + 42, 2);
    memcpy(&phnum, d + 44, 2);
    for (int i = 0; i < phnum; i++)
    {
        uint32_t p[8];
        memcpy(p, d + phoff + size_t(i) * phentsize, sizeof p);
        if (p[0] == 1 && p[4]) uc_mem_write(g->uc, p[2], d + p[1], p[4]);
    }
    const uint64_t trap = g->at("h2_fs_trap") & ~1u;
    uc_hook hh;
    uc_hook_add(g->uc, &hh, UC_HOOK_CODE, reinterpret_cast<void*>(fsHook), g, trap, trap);
    g->call("h_boot");
    if (g->call("h2_init") != 0) { fprintf(stderr, "plusproxy: h2_init failed\n"); abort(); }
    fprintf(stderr, "plusproxy: closed libPolySynth guest %s (%zu symbols), stock polysynth:: API proxied\n", elf, g->sym.size());
    g_guest = g;
    return g;
}

// the metadata record of h2_meta (harness.cpp, same order)
struct Rd
{
    const uint8_t* p;
    const uint8_t* e;
    int i() { int v = 0; if (p + 4 <= e) memcpy(&v, p, 4); p += 4; return v; }
    float f() { float v = 0; if (p + 4 <= e) memcpy(&v, p, 4); p += 4; return v; }
    std::string s() { const int n = i(); std::string v; if (n > 0 && p + n <= e) v.assign(reinterpret_cast<const char*>(p), size_t(n)); p += n > 0 ? n : 0; return v; }
    void layout(polysynth::Patch::Metadata::EditorLayout& l)
    {
        using L = polysynth::Patch::Metadata::EditorLayout;
        l.sections.clear();
        const int ns = i();
        for (int k = 0; k < ns; k++)
        {
            L::EditorSection sec;
            sec.name = s();
            sec.shortName = s();
            const int nsub = i();
            for (int j = 0; j < nsub; j++)
            {
                L::EditorSubsection sub;
                sub.name = s();
                const int ne = i();
                for (int q = 0; q < ne; q++) { L::EditorElement el; el.type = static_cast<L::EditorElement::Type>(i()); el.id = i(); sub.elements.push_back(el); }
                sec.subsections.push_back(std::move(sub));
            }
            const int ne = i();
            for (int q = 0; q < ne; q++) { L::EditorElement el; el.type = static_cast<L::EditorElement::Type>(i()); el.id = i(); sec.elements.push_back(el); }
            l.sections.push_back(std::move(sec));
        }
    }
};

// UISHOT_PLUS_DUMP of a part that plays the patch (ProxySynth::dumpBlock): dumpSetBefore writes the stored values that changed since the
// last line (a load, an edit mapped set) and returns the part (-1: no open dump plays it), dumpSetAfter writes the set as a par line and
// takes the guest's stored values as logged. So a set whose value another one changes later (a stock Sync converts its Ratio,
// invalidatesOtherMetadata) replays in the sim's order: the stored values alone (in index order) do not give the same patch.
int dumpSetBefore(const polysynth::Patch* p);
void dumpSetAfter(int part, int id, uint32_t bits);

class ProxyPatch : public polysynth::Patch
{
  public:
    explicit ProxyPatch(int h) : h(h) { refresh(); }
    ~ProxyPatch() override { guest()->call("h2_free_patch", h); }

    void refresh()
    {
        Guest& g = *guest();
        const int n = g.call("h2_meta", h);
        if (n <= 0) { fprintf(stderr, "plusproxy: patch %d metadata does not fit h2_buf\n", h); return; }
        std::vector<uint8_t> b(static_cast<size_t>(n));
        g.read("h2_buf", b.data(), b.size());
        Rd r{ b.data(), b.data() + b.size() };
        Metadata& m = meta;
        m.instrumentTypeId = r.i();
        m.outputChannels = static_cast<Metadata::OutputChannels>(r.i());
        m.macrosAffectParamCC = r.i() != 0;
        m.rendersAuxSends = r.i() != 0;
        m.maxVoices = r.i();
        const int np = r.i();
        m.params.clear();
        m.params.resize(size_t(np > 0 ? np : 0));
        for (int k = 0; k < np; k++)
        {
            auto& q = m.params[size_t(k)];
            q.displayName = r.s();
            const int hasDef = r.i();
            const float def = r.f();
            q.defaultValue = hasDef ? optional_paramvalue_t(def) : optional_paramvalue_t();
            q.minValue = paramvalue_short_t(r.f());
            q.maxValue = paramvalue_short_t(r.f());
            q.editCurve = static_cast<Metadata::Param::Curve>(r.i());
            q.macroOperation = static_cast<Metadata::Param::MacroOperation>(r.i());
            q.isEnum = r.i() != 0;
            const int cc = r.i();
            q.midiCC = cc < 0 ? decltype(q.midiCC)() : decltype(q.midiCC)(uint8_t(cc));
            q.invalidatesOtherMetadata = r.i() != 0;
            if (r.i())
            {
                const int handle = h;
                q.valueFormatter = [handle, k](const Patch&, paramvalue_t v) -> std::string
                {
                    Guest& gg = *guest();
                    const int len = gg.call("h2_format", handle, k, int(bitsOf(v)));
                    return gg.bufStr(len);
                };
            }
            else q.valueFormatter = {};
        }
        const int nm = r.i();
        m.macros.clear();
        m.macros.resize(size_t(nm > 0 ? nm : 0));
        for (int k = 0; k < nm; k++)
        {
            auto& q = m.macros[size_t(k)];
            q.displayName = r.s();
            const int nmod = r.i();
            for (int j = 0; j < nmod; j++)
            {
                Metadata::Macro::ParamModifier mod;
                const int pid = r.i();
                mod.paramId = pid < 0 ? tiny_optional<int>() : tiny_optional<int>(pid);
                mod.amount = r.f();
                q.modifiers.push_back(mod);
            }
            q.bipolar = r.i() != 0;
            q.perVoice = r.i() != 0;
            q.system = r.i() != 0;
        }
        const int nss = r.i();
        m.sampleSlots.clear();
        for (int k = 0; k < nss; k++)
        {
            Metadata::SampleSlot ss;
            ss.displayName = r.s();
            ss.type = static_cast<Metadata::SampleSlot::Type>(r.i());
            ss.section = r.i();
            ss.subsection = r.i();
            m.sampleSlots.push_back(std::move(ss));
        }
        r.layout(m.layout23);
        r.layout(m.layoutFlat);
    }

    optional_paramvalue_t getParamValue(int id) const override { return bitsOpt(guest()->call("h2_pget", h, id)); }
    tiny_optional<float> getParamValueEditMapped(int id) const override { return bitsOpt(guest()->call("h2_pget_em", h, id)); }
    void setParamValueEditMapped(int id, tiny_optional<float> v, ParamChangeSource s) override
    {
        guest()->call("h2_pset_em", h, id, int(optBits(v)), int(s));
        afterSet(id);
    }
    optional_paramvalue_t getParamFinalValue(int id) const override { return bitsOpt(guest()->call("h2_pfinal", h, id)); }
    void setParamValue(int id, optional_paramvalue_t v, ParamChangeSource s) override
    {
        const uint32_t b = optBits(v);
        const int part = dumpSetBefore(this);   // UISHOT_PLUS_DUMP: the set goes into the dump in its call order (P12 round 6)
        guest()->call("h2_pset", h, id, int(b), int(s));
        afterSet(id);
        if (part >= 0) dumpSetAfter(part, id, b);
    }
    // host side listeners are not forwarded (the stock core subscribes none, grep core/)
    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override { guest()->call("h2_batch", h, 1); }
    void finishBatchUpdate() override { guest()->call("h2_batch", h, 0); refresh(); }
    optional_paramvalue_t getMacroValue(int m) const override { return bitsOpt(guest()->call("h2_mget", h, m)); }
    void setMacroValue(int m, optional_paramvalue_t v, tiny_optional<int> voice) override { guest()->call("h2_mset", h, m, int(optBits(v)), voice ? *voice : -1); }
    void setMacroModifier(int m, int idx, tiny_optional<int> pid) override { guest()->call("h2_mmod", h, m, idx, pid ? *pid : -1); refresh(); }
    void setMacroModifierAmount(int m, int idx, float a) override { guest()->call("h2_mamt", h, m, idx, int(bitsOf(a))); refresh(); }
    tiny_optional<float> getMacroModifierAmount(int m, int idx) override { return bitsOpt(guest()->call("h2_mamt_get", h, m, idx)); }
    void clearMacroModifiers() override { guest()->call("h2_mclear", h); refresh(); }
    void changeMacroModifierAmount(int m, int idx, paramvalue_t v) override { guest()->call("h2_mchg", h, m, idx, int(bitsOf(v))); refresh(); }
    void setMacroBipolar(int m, bool on) override { guest()->call("h2_mbip", h, m, on ? 1 : 0); refresh(); }
    void setMacroDisplayName(int m, std::string name) override { guest()->putStr(name); guest()->call("h2_mname", h, m); refresh(); }
    // sample slots: no stock Tracker+ engine has one (the metadata lists them, h2_meta), never loaded on the host
    const polysynth::Sample* getSample(int) const override { return nullptr; }
    polysynth::LoadStatus loadSample(int, const std::string&) override { return polysynth::LoadStatus{}; }
    const Metadata& getMetadata() const override { return meta; }
    const Sequencer& getSequencer() const override { return seq; }
    void setSequence(const Sequencer::Sequence&, int) override {}
    void setSequencerConfig(const Sequencer::Config&) override {}

    int h;
    Metadata meta;
    Sequencer seq;

  private:
    void afterSet(int id)
    {
        if (id >= 0 && id < int(meta.params.size()) && meta.params[size_t(id)].invalidatesOtherMetadata) refresh();
    }
};

class ProxyStore : public polysynth::PatchStore
{
  public:
    explicit ProxyStore(int s) : s(s) {}
    std::unique_ptr<polysynth::Patch> createPatch(int type) override
    {
        const int h = guest()->call("h2_create_patch", s, type);
        if (h < 0) { fprintf(stderr, "plusproxy: createPatch(%d) = %d\n", type, h); return nullptr; }
        return std::make_unique<ProxyPatch>(h);
    }
    std::vector<std::pair<std::string_view, int>> listPatchesFilter(std::function<bool(polysynth::InstrumentTypeId)>) override { return {}; }
    std::vector<std::string_view> listPatchesByType(int type) override
    {
        Guest& g = *guest();
        const int n = g.call("h2_list", s, type);
        int len = 0;
        g.read("h2_len", &len, 4);
        std::string all = g.bufStr(len);
        names.clear();
        size_t p = 0;
        for (int k = 0; k < n && p < all.size(); k++)
        {
            const size_t z = all.find('\0', p);
            names.push_back(all.substr(p, z - p));
            p = z + 1;
        }
        std::vector<std::string_view> v;
        for (const auto& x : names) v.emplace_back(x);
        return v;
    }
    bool loadPatch(const std::string& name, polysynth::Patch& patch, bool sync) override
    {
        auto& pp = static_cast<ProxyPatch&>(patch);
        guest()->putStr(name);
        const bool ok = guest()->call("h2_load", s, pp.h, sync ? 1 : 0) == 1;
        pp.refresh();
        return ok;
    }
    bool savePatch(const std::string& name, const polysynth::Patch& patch) override
    {
        guest()->putStr(name);
        return guest()->call("h2_save", s, static_cast<const ProxyPatch&>(patch).h) == 1;
    }
    std::optional<polysynth::InstrumentTypeId> loadPatchInstrumentType(const std::string& name) override
    {
        guest()->putStr(name);
        const int t = guest()->call("h2_load_type", s);
        return t < 0 ? std::nullopt : std::optional<polysynth::InstrumentTypeId>(static_cast<polysynth::InstrumentTypeId>(t));
    }
    int s;
    std::vector<std::string> names;
};

// the dump of a part for run.py --events (UISHOT_PLUS_DUMP)
struct Dump
{
    FILE* ev = nullptr;
    FILE* f32 = nullptr;
    int type = -1;
    long block = 0;
    std::vector<uint32_t> last;
};
Dump g_dump[kMaxParts];
// the sets of a part's patch before its dump is open (a Vary or a load before Play) or while it still has the old type: in order, as
// dumpBlock writes them when it opens the dump (block 0, against the same fresh init patch)
struct Pend
{
    const polysynth::Patch* p = nullptr;
    std::vector<uint32_t> last;
    std::vector<std::pair<int, uint32_t>> lines;
};
Pend g_pend[kMaxParts];
// opens the part's dump before the event (Play sends the step's note before the part's first block), ProxySynth::dumpBlock
void dumpOpen(int id);

class ProxyInstrument : public polysynth::Instrument
{
  public:
    explicit ProxyInstrument(int id) : id(id)
    {
        cb = std::make_shared<polysynth::MidiSource::MidiEventCallback>(
          [this](const polysynth::MidiEvent& e, int source)
          {
              int w = 0;
              static_assert(sizeof e == 4, "MidiEvent crosses as one word");
              std::memcpy(&w, &e, 4);
              guest()->syncTuning();
              guest()->call("h2_event", this->id, w, source);
              dumpOpen(this->id);
              Dump& d = g_dump[this->id];
              if (d.ev && (e.channel == channel || e.channel == polysynth::MidiEvent::kChannelOmni))
              {
                  const uint8_t* b = reinterpret_cast<const uint8_t*>(&e.data);
                  fprintf(d.ev, "ev %ld %d %d %d %d\n", d.block, int(e.type), e.channel == channel ? 0 : 255, b[0], b[1]);
              }
          });
    }
    void assignPatch(std::shared_ptr<polysynth::Patch> p) override
    {
        patch = p;
        if (p) guest()->call("h2_assign", id, static_cast<ProxyPatch&>(*p).h);
    }
    void connect(polysynth::MidiSource& src) override { src.subscribeMidiEvents(id, cb); }
    void setMidiChannel(int c) override { channel = c; guest()->call("h2_chan", id, c); }
    int getMidiChannel() const override { return guest()->call("h2_chan_get", id); }
    void setLocalMode(LocalMode m) override { guest()->call("h2_local", id, int(m)); }
    LocalMode getLocalMode() const override { return static_cast<LocalMode>(guest()->call("h2_local_get", id)); }
    int getVoiceCount() const override { return guest()->call("h2_voices_get", id); }
    void setVoiceCount(int n) override { guest()->call("h2_voices", id, n); }

    int id;
    int channel = 0;
    std::shared_ptr<polysynth::Patch> patch;
    std::shared_ptr<polysynth::MidiSource::MidiEventCallback> cb;
};

class ProxySynth : public polysynth::Synth
{
  public:
    std::unique_ptr<polysynth::PatchStore> createPatchStore(FileSystemId id, bool folders) override
    {
        const int s = guest()->call("h2_store", int(id), folders ? 1 : 0);
        return s < 0 ? nullptr : std::make_unique<ProxyStore>(s);
    }
    std::unique_ptr<polysynth::PatchPool> createPatchPool(std::shared_ptr<polysynth::PatchStore>, int, std::function<tiny::optional<int>(const std::string&)>) override { return nullptr; }
    std::shared_ptr<polysynth::Instrument> createInstrument(int id) override
    {
        if (id < 0 || id >= kMaxParts || guest()->call("h2_instr", id) != 0) return nullptr;
        auto in = std::make_shared<ProxyInstrument>(id);
        instruments[id] = in.get();
        return in;
    }
    std::unique_ptr<polysynth::Mixer> createMixer(int, int) override { return nullptr; }
    std::unique_ptr<polysynth::Grid> createGrid() override { return nullptr; }
    std::unique_ptr<polysynth::GridMapperStore> createGridMapperStore(FileSystemId) override { return nullptr; }
    std::unique_ptr<polysynth::GridMapper> createGridMapper() override { return nullptr; }
    std::unique_ptr<polysynth::SampleStore> createSampleStore(FileSystemId) override { return nullptr; }
    std::shared_ptr<polysynth::Sequencer> createSequencer() override { return nullptr; }
    void addMidiSource(const std::string&, std::shared_ptr<polysynth::MidiSource>) override {}
    void addMidiSink(const std::string&, std::unique_ptr<polysynth::MidiSink>) override {}
    void addFileSystem(FileSystemId id, std::unique_ptr<polysynth::FileSystem> fs) override
    {
        Guest& g = *guest();
        g.fs[size_t(id)] = std::move(fs);
        g.call("h2_addfs", int(id));
    }
    void initSamplePool(size_t, std::shared_ptr<polysynth::SampleStore>) override {}
    std::string getInstrumentName(int type) override { Guest& g = *guest(); return g.bufStr(g.call("h2_type_name", type)); }
    std::vector<std::string_view> getInstrumentTypeNames() override
    {
        typeNames.clear();
        for (int t : getInstrumentTypeIds()) typeNames.push_back(getInstrumentName(t));
        std::vector<std::string_view> v;
        for (const auto& s : typeNames) v.emplace_back(s);
        return v;
    }
    std::vector<int> getInstrumentTypeIds() override
    {
        Guest& g = *guest();
        const int n = g.call("h2_type_ids");
        std::vector<int> v(size_t(n > 0 ? n : 0));
        if (n > 0) g.read("h_ids", v.data(), v.size() * 4);
        return v;
    }
    void setup(std::span<std::shared_ptr<polysynth::Instrument>> ins, std::span<std::shared_ptr<polysynth::Instrument>>, std::shared_ptr<polysynth::Mixer>,
               polysynth::TempoProvider tp) override
    {
        for (size_t k = 0; k < ins.size(); k++)
            if (!ins[k] || static_cast<ProxyInstrument&>(*ins[k]).id != int(k)) { fprintf(stderr, "plusproxy: setup expects instrument k at slot k\n"); abort(); }
        parts = int(ins.size());
        tempo = std::move(tp);
        guest()->call("h2_setup", parts);
        if (const char* dir = getenv("UISHOT_PLUS_DUMP")) dumpDir = dir;
    }
    void disconnectInstrument(int id) override { guest()->call("h2_disconnect", id); }
    void renderAudioBuffer(polysynth::AudioBufferStereoRef) override {}
    void renderAudioBuffer(polysynth::AudioBufferStereoRef main, std::span<polysynth::AudioBufferRef> aux, int id) override
    {
        Guest& g = *guest();
        if (id >= 0 && id < kMaxParts) dumpBlock(id);
        g.call("h2_render", id);
        g.read("h_outL", main[0].data(), sizeof(float) * kBlock);
        g.read("h_outR", main[1].data(), sizeof(float) * kBlock);
        if (aux.size() > 0) g.read("h_send0", aux[0].data(), sizeof(float) * kBlock);
        if (aux.size() > 1) g.read("h_send1", aux[1].data(), sizeof(float) * kBlock);
        if (id >= 0 && id < kMaxParts && g_dump[id].f32)
        {
            Dump& d = g_dump[id];
            for (int i = 0; i < kBlock; i++) { fwrite(&main[0][size_t(i)], 4, 1, d.f32); fwrite(&main[1][size_t(i)], 4, 1, d.f32); }
            fflush(d.f32);
            fflush(d.ev);
        }
        if (id >= 0 && id < kMaxParts) g_dump[id].block++;
    }
    void updateAudio() override
    {
        Guest& g = *guest();
        const auto t = tempo ? tempo() : tiny::optional<float>();
        const int ok = t ? 1 : 0;
        const float v = t ? *t : 0.f;
        uc_mem_write(g.uc, g.at("h2_tempo_ok"), &ok, 4);
        uc_mem_write(g.uc, g.at("h2_tempo"), &v, 4);
        g.syncTuning();
        g.call("h2_update_audio");
    }
    void updateMain() override { guest()->call("h2_update_main"); }

    // the part's patch values against the last logged ones as par lines before its block (a new type: a new init line and file pair).
    // params false: only the file pair and init line (before a MIDI event), the par lines come before the next block
    void dumpBlock(int id, bool params = true)
    {
        if (dumpDir.empty() || !instruments[id] || !instruments[id]->patch) return;
        Guest& g = *guest();
        auto& p = static_cast<ProxyPatch&>(*instruments[id]->patch);
        Dump& d = g_dump[id];
        const int type = p.meta.instrumentTypeId;
        if (type != d.type)
        {
            if (d.ev) fclose(d.ev);
            if (d.f32) fclose(d.f32);
            d = Dump{};
            d.type = type;
            char path[1024];
            snprintf(path, sizeof path, "%s/part%d.events", dumpDir.c_str(), id);
            d.ev = fopen(path, "w");
            snprintf(path, sizeof path, "%s/part%d.f32", dumpDir.c_str(), id);
            d.f32 = fopen(path, "wb");
            if (d.ev) fprintf(d.ev, "init %d %d\n", type, g.call("h2_voices_get", id));
            // the reference: a fresh init patch of the type, as run.py's h_init makes it
            const int s = 0;
            const int fresh = g.call("h2_create_patch", s, type);
            d.last.assign(p.meta.params.size(), 0xFFFFFFFFu);
            for (size_t k = 0; k < d.last.size() && fresh >= 0; k++) d.last[k] = uint32_t(g.call("h2_pget", fresh, int(k)));
            if (fresh >= 0) g.call("h2_free_patch", fresh);
            Pend& q = g_pend[id];
            if (q.p == &p && q.last.size() == d.last.size() && d.ev)
            {   // the sets before the dump opened, in their order
                for (const auto& [k, b] : q.lines) fprintf(d.ev, "par %ld %d %d\n", d.block, k, int(b));
                d.last = q.last;
            }
            q = Pend{};
        }
        if (!d.ev || !params) return;
        for (size_t k = 0; k < d.last.size(); k++)
        {
            const uint32_t b = uint32_t(g.call("h2_pget", p.h, int(k)));
            if (b == d.last[k] || std::isnan(floatOf(b))) continue;
            d.last[k] = b;
            fprintf(d.ev, "par %ld %zu %d\n", d.block, k, int(b));
        }
    }

    int parts = 0;
    polysynth::TempoProvider tempo;
    ProxyInstrument* instruments[kMaxParts] = {};
    std::vector<std::string> typeNames;
    std::string dumpDir;
};

ProxySynth* g_proxy = nullptr;
void dumpOpen(int id)
{
    if (g_proxy && id >= 0 && id < kMaxParts) g_proxy->dumpBlock(id);   // the params as they are now, before the event
}
bool dumpOpenFor(int id, const polysynth::Patch* p)
{
    return g_dump[id].ev && g_dump[id].type == static_cast<const ProxyPatch*>(p)->meta.instrumentTypeId;
}
int dumpSetBefore(const polysynth::Patch* p)
{
    if (!g_proxy || g_proxy->dumpDir.empty()) return -1;
    for (int id = 0; id < kMaxParts; id++)
    {
        ProxyInstrument* in = g_proxy->instruments[id];
        if (!in || in->patch.get() != p) continue;
        if (dumpOpenFor(id, p))
        {
            g_proxy->dumpBlock(id);   // the changes before this set, as stored values
            return id;
        }
        Pend& q = g_pend[id];
        const auto& pp = static_cast<const ProxyPatch&>(*p);
        Guest& g = *guest();
        if (q.p != p)
        {   // the reference of the dump that opens later: a fresh init patch of the type
            q = Pend{};
            q.p = p;
            const int fresh = g.call("h2_create_patch", 0, pp.meta.instrumentTypeId);
            q.last.assign(pp.meta.params.size(), 0xFFFFFFFFu);
            for (size_t k = 0; k < q.last.size() && fresh >= 0; k++) q.last[k] = uint32_t(g.call("h2_pget", fresh, int(k)));
            if (fresh >= 0) g.call("h2_free_patch", fresh);
        }
        for (size_t k = 0; k < q.last.size(); k++)
        {   // the changes before this set, as stored values (dumpBlock's rule)
            const uint32_t b = uint32_t(g.call("h2_pget", pp.h, int(k)));
            if (b == q.last[k] || std::isnan(floatOf(b))) continue;
            q.last[k] = b;
            q.lines.emplace_back(int(k), b);
        }
        return id;
    }
    return -1;
}
void dumpSetAfter(int part, int id, uint32_t bits)
{
    ProxyInstrument* in = g_proxy ? g_proxy->instruments[part] : nullptr;
    if (!in || !in->patch) return;
    const auto& p = static_cast<const ProxyPatch&>(*in->patch);
    const bool open = dumpOpenFor(part, &p);
    if (!open && g_pend[part].p != &p) return;
    std::vector<uint32_t>& last = open ? g_dump[part].last : g_pend[part].last;
    if (!std::isnan(floatOf(bits)))
    {   // an unset value: the stored diff later
        if (open) fprintf(g_dump[part].ev, "par %ld %d %d\n", g_dump[part].block, id, int(bits));
        else g_pend[part].lines.emplace_back(id, bits);
    }
    for (size_t k = 0; k < last.size(); k++) last[k] = uint32_t(guest()->call("h2_pget", p.h, int(k)));
}
}   // namespace

std::shared_ptr<polysynth::Synth> polysynth::Synth::createSynth(Allocator)
{
    guest();
    auto s = std::make_shared<ProxySynth>();
    g_proxy = s.get();
    return s;
}

extern "C" void uishot_plus_flush()
{
    for (Dump& d : g_dump)
    {
        if (d.ev) fflush(d.ev);
        if (d.f32) fflush(d.f32);
    }
}

// P7 H5: a plugin synth engine the core picks (Synth::attachCardEngine) is loaded and attached inside the guest, where the closed lib and
// PlugSynthVoice.cpp run (harness.cpp h2_plug_attach: the image's loader, the image's adapter). The .plg bytes cross through h2_plg.
#include "../../../core/synth/engines/PlugSynthVoice.h"
int polysynth::plugimx::attachFile(int slot, const uint8_t* file, uint32_t len)
{
    Guest& g = *guest();
    constexpr uint32_t kPlgMax = 64 * 1024;   // harness.cpp h2_plg
    if (!file || len == 0 || len > kPlgMax) return -2;
    uc_mem_write(g.uc, g.at("h2_plg"), file, len);
    const int r = g.call("h2_plug_attach", slot, int(len));
    if (r < 0) fprintf(stderr, "plusproxy: h2_plug_attach(slot %d, %u B) = %d\n", slot, len, r);
    return r;
}
// Paraphony (Synth::plugChordPara and plugNoteChord): the engines' PLUG_EXT_CHORD answers and the pending chord live in the guest
int polysynth::plugimx::chordInfo(int typeId) { return guest()->call("h2_chord_info", typeId); }
// Note ids and the New Note Action (the sequencer's SEQ_NOTE_TAG): the tag lives in the guest, read by PlugVoice inside the send
void polysynth::plugimx::noteTag(uint16_t id, uint8_t endMode) { guest()->call("h2_note_tag", id, endMode); }
void polysynth::plugimx::mpeNote(uint8_t chan) { guest()->call("h2_mpe_note", chan); }
// the "plugmpe" poke (host_poke.cpp): field f of the guest's voice i (PlugSynthVoice.cpp mpeProbe)
int uishotPlugMpeVoice(int i, int f) { return guest()->call("h2_mpe_voice", i, f); }
// the page hands whole semitones and a pressure percent (fSynthPlay.cpp part::mpeConfig), the guest makes the same floats of them
void polysynth::plugimx::partMpe(int part, float memberRange, float pressAmount)
{
    guest()->call("h2_part_mpe", part, int(lroundf(memberRange)), int(lroundf(pressAmount * 100.0f)));
}
void polysynth::plugimx::partNoteBend(int part, uint8_t chan, int raw) { guest()->call("h2_note_bend", part, chan, raw); }
void polysynth::plugimx::partNotePress(int part, uint8_t chan, uint8_t press) { guest()->call("h2_note_press", part, chan, press); }
void polysynth::plugimx::noteChord(int part, uint8_t note, uint8_t chord, int8_t strum)
{
    guest()->call("h2_note_chord", part, note, int(chord) | int(uint8_t(strum)) << 8);
}
