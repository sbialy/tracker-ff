// Tracker+ simulator, phase 3 (ref/plus-emulator-plan.md): a tiny Cortex-M7 program around the UNMODIFIED closed libPolySynth.a and
// libPolyDSP.a of synth/lib, driven through their public headers only and run under Unicorn on the Mac (run.py). Host tooling, not firmware.
// It builds what the stock Tracker+ core builds in Synth::init (core/synth/Synth.cpp, the non FEATURE_SYNTH_OPEN path): a synth, a patch
// store, the init patch of one instrument type, one instrument on MIDI channel 0, setup(). Notes go in as the MIDI events MidiAdapter sends.
//
// ABI note: the lib was built with GCC 10.3, where a dynamic-extent std::span is laid out {size, ptr}. This harness is built with 12.3
// ({ptr, size}). The one dynamic span this file passes (setup()) is built swapped (span10()), the render call takes fixed-extent spans
// (a plain pointer, same in both). The FileSystem the lib may call is a stub that never reads its span arguments.
// The lib is never disassembled or patched (its licence, synth/lib/LICENSE-PROPRIETARY.txt): it runs as a black box.
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include "PolySynth/Synth.h"
#include "PolySynth/Instrument.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/Patch.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/FileSystem.h"
#include "PolySynth/config.h"

extern "C" void __libc_init_array();

// The project tuning of the image (core/source/mtTuneScale.cpp holds the Scala degrees, tuning::cfg is an inline variable of mtTuning.h,
// _ZN6tuning3cfgE): the plus sim writes both from the host before the events and the block (host_plusproxy.cpp syncTuning), run.py --tune cfg
#include "../../../core/source/mtTuning.h"
namespace tuning
{
uint32_t degMc[kMaxN];
}

namespace
{
// GCC 10.3 layout of std::span<T> (dynamic extent): size first. Built as a 12.3 span whose fields hold the swapped values
template <class T> std::span<T> span10(T* p, size_t n) { return std::span<T>(reinterpret_cast<T*>(n), reinterpret_cast<size_t>(p)); }

class NoFs : public polysynth::FileSystem
{
  public:
    std::vector<std::string_view> listFiles(const std::string&, std::function<bool(const std::string&)>, tiny_optional<int>) override { return {}; }
    std::optional<FileHandle> open(const std::string&, OpenOptions) override { return std::nullopt; }
    void close(FileHandle) override {}
    bool write(FileHandle, std::span<const uint8_t>) override { return false; }
    int32_t read(FileHandle, std::span<uint8_t>) override { return -1; }
    bool seek(FileHandle, int) override { return false; }
    void createFolder(const std::string&) override {}
};

class Keys : public polysynth::MidiSource
{
  public:
    Keys() : MidiSource(Type::kInternalKeyboard) {}
    void subscribeMidiEvents(int, std::weak_ptr<MidiEventCallback> cb) override { callback = cb; }
    void send(const polysynth::MidiEvent& e)
    {
        if (auto c = callback.lock()) (*c)(e, 0);
    }
    std::weak_ptr<MidiEventCallback> callback;
};

std::shared_ptr<polysynth::Synth> g_synth;
std::shared_ptr<polysynth::Instrument> g_instr;
std::shared_ptr<polysynth::PatchStore> g_store;
std::shared_ptr<polysynth::Patch> g_patch;
Keys* g_keys;
std::array<std::shared_ptr<polysynth::Instrument>, 1> g_list;
}   // namespace

extern "C"
{
float h_outL[polysynth::kAudioBufferSize];
float h_outR[polysynth::kAudioBufferSize];
volatile int h_step;   // the last step reached, for a fault report
int h_chan;   // the MIDI channel of the one instrument (run.py --chan, written before h_init): a synth part's channel is its part index (Synth.cpp)

// the C runtime: static constructors (the lib has some), once
int h_boot()
{
    h_step = 1;
    __libc_init_array();
    h_step = 2;
    return 0;
}

// type: polysynth::InstrumentTypeId (1 ACD, 2 FAT, 4 VAP, 5 WTFM, 6 PERC, 7 DIRT), voices: the instrument's voice count
int h_init(int type, int voices)
{
    h_step = 10;
    g_synth = polysynth::Synth::createSynth([](size_t) -> void* { return nullptr; });   // as the firmware: no large buffer modules
    if (!g_synth) return -1;
    h_step = 11;
    g_synth->addFileSystem(polysynth::Synth::FileSystemId::kPatches, std::make_unique<NoFs>());
    g_synth->addFileSystem(polysynth::Synth::FileSystemId::kWorkspace, std::make_unique<NoFs>());
    h_step = 12;
    g_store = g_synth->createPatchStore(polysynth::Synth::FileSystemId::kPatches, true);
    if (!g_store) return -2;
    h_step = 13;
    g_patch = g_store->createPatch(type);
    if (!g_patch) return -3;
    h_step = 14;
    g_keys = new Keys();
    g_instr = g_synth->createInstrument(0);
    if (!g_instr) return -4;
    h_step = 15;
    g_instr->assignPatch(g_patch);
    g_instr->connect(*g_keys);
    g_instr->setVoiceCount(voices);
    g_instr->setMidiChannel(h_chan);
    g_list[0] = g_instr;
    h_step = 16;
    g_synth->setup(span10(g_list.data(), g_list.size()), span10<std::shared_ptr<polysynth::Instrument>>(nullptr, 0), nullptr,
                   []() -> tiny::optional<float> { return 120.f; });
    h_step = 17;
    return 0;
}

// on: 1 note on, 0 note off
void h_note(int on, int note, int velocity)
{
    polysynth::MidiEvent e{};
    e.type = on ? polysynth::MidiEvent::Type::kNoteOn : polysynth::MidiEvent::Type::kNoteOff;
    e.channel = uint8_t(h_chan);
    e.data.note.note = uint8_t(note);
    e.data.note.velocity = uint8_t(velocity);
    g_keys->send(e);
}

// any MIDI event of the firmware's MidiAdapter as the instrument receives it (the plus sim's synth parts forward theirs, host_plussynth.cpp):
// type: polysynth::MidiEvent::Type, channel: 0 (the instrument's h_chan) or MidiEvent::kChannelOmni, d0/d1: the first two data bytes
void h_event(int type, int channel, int d0, int d1)
{
    polysynth::MidiEvent e{};
    e.type = static_cast<polysynth::MidiEvent::Type>(type);
    e.channel = uint8_t(channel == 0 ? h_chan : channel);
    reinterpret_cast<uint8_t*>(&e.data)[0] = uint8_t(d0);
    reinterpret_cast<uint8_t*>(&e.data)[1] = uint8_t(d1);
    g_keys->send(e);
}

// one block of kAudioBufferSize frames into h_outL / h_outR, the order of the firmware's audio interrupt: updateAudio, then render
// The firmware's call (core/libs/Audio/synth_PolySynth.cpp): per part, main stereo out plus the two send buffers as a dynamic span (GCC 10
// layout, span10). The overload without the part id mixes through a Mixer, which the firmware never creates (setup gets nullptr)
float h_send0[polysynth::kAudioBufferSize];
float h_send1[polysynth::kAudioBufferSize];
void h_render()
{
    g_synth->updateAudio();
    std::memset(h_outL, 0, sizeof h_outL);
    std::memset(h_outR, 0, sizeof h_outR);
    std::memset(h_send0, 0, sizeof h_send0);
    std::memset(h_send1, 0, sizeof h_send1);
    static std::array<polysynth::AudioBufferRef, 2> sends = { polysynth::AudioBufferRef(h_send0), polysynth::AudioBufferRef(h_send1) };
    g_synth->renderAudioBuffer(polysynth::AudioBufferStereoRef{ polysynth::AudioBufferRef(h_outL), polysynth::AudioBufferRef(h_outR) },
                               span10(sends.data(), sends.size()), 0);
}

// the main loop's share (patch and parameter work), once per block like the firmware's main loop at idle
void h_main() { g_synth->updateMain(); }

// the patch's parameters as the stock param pages see them (polysynth::Patch, the id is the index into Metadata::params). Floats cross
// as their bit pattern in a core register. The plus sim sets the stored value of the part's patch (getParamValue of the UI side), the
// source kKnob as an edit on the unit's param page
static float bitsToFloat(int b) { float f; std::memcpy(&f, &b, 4); return f; }
static int floatToBits(float f) { int b; std::memcpy(&b, &f, 4); return b; }
int h_nparams() { return int(g_patch->getMetadata().params.size()); }
char h_name[64];
// the parameter's displayName into h_name (cut at 63 bytes), its length or -1
int h_param_name(int id)
{
    const auto& ps = g_patch->getMetadata().params;
    if (id < 0 || id >= int(ps.size())) return -1;
    const std::string& n = ps[size_t(id)].displayName;
    const size_t k = n.size() < sizeof h_name - 1 ? n.size() : sizeof h_name - 1;
    std::memcpy(h_name, n.data(), k);
    h_name[k] = 0;
    return int(k);
}
int h_param_get(int id)
{
    const auto v = g_patch->getParamValue(id);
    return v ? floatToBits(*v) : floatToBits(__builtin_nanf(""));
}
void h_param_set(int id, int bits) { g_patch->setParamValue(id, bitsToFloat(bits), polysynth::Patch::ParamChangeSource::kKnob); }
// one parameter's metadata for a UI list entry of an engine the open stack has no port of (DIRT, host_plussynth.cpp): h_info = default (the
// min when none), min, max. Returns isEnum | editCurve << 1
float h_info[3];
int h_param_info(int id)
{
    const auto& ps = g_patch->getMetadata().params;
    if (id < 0 || id >= int(ps.size())) return -1;
    const auto& p = ps[size_t(id)];
    h_info[0] = p.defaultValue ? *p.defaultValue : float(p.minValue);
    h_info[1] = float(p.minValue);
    h_info[2] = float(p.maxValue);
    return (p.isEnum ? 1 : 0) | (int(p.editCurve) << 1);
}
// the flat editor layout (Metadata::layoutFlat): the section count, then per section its name into h_name and its parameter ids into h_ids
int h_ids[128];
int h_nsections() { return int(g_patch->getMetadata().layoutFlat.sections.size()); }
int h_section(int i)
{
    const auto& ss = g_patch->getMetadata().layoutFlat.sections;
    if (i < 0 || i >= int(ss.size())) return -1;
    const auto& sec = ss[size_t(i)];
    const size_t k = sec.name.size() < sizeof h_name - 1 ? sec.name.size() : sizeof h_name - 1;
    std::memcpy(h_name, sec.name.data(), k);
    h_name[k] = 0;
    int n = 0;
    for (const auto& e : sec.elements)
        if (e.type == polysynth::Patch::Metadata::EditorLayout::EditorElement::Type::kParam && n < 128) h_ids[n++] = e.id;
    return n;
}
int h_maxvoices() { return g_patch->getMetadata().maxVoices; }

// the two metadata switches of the stock part output path (core/libs/Audio/synth_PolySynth.cpp, the non FEATURE_SYNTH_OPEN branch)
int h_stereo() { return g_patch->getMetadata().outputChannels == polysynth::Patch::Metadata::OutputChannels::kStereo ? 1 : 0; }
int h_sends() { return g_patch->getMetadata().rendersAuxSends ? 1 : 0; }
}

// newlib nano with nosys: no exit path is ever taken, the runner stops at its return address
extern "C" void _exit(int) { for (;;) {} }

// what the firmware image gives the lib besides registry.cpp (compiled with this file): the C++ runtime's DSO handle (crtbegin is not
// linked, -nostartfiles) and the audio interrupt guards of the platform (the harness has no interrupt: the runner calls render and the
// main loop share one after the other, as the unit's audio ISR and main loop never overlap inside these guards)
extern "C" { void* __dso_handle = &__dso_handle; void _init() {} void _fini() {} }
void disableAudioInterrupt() {}
void enableAudioInterrupt() {}

// ---------------------------------------------------------------------------------------------------------------------------------------
// Round 5: the multi-object API of the plus sim's host polysynth:: proxy (synth-open/tools/ui_shot/host_plusproxy.cpp). One guest holds
// what the stock Tracker+ core builds in Synth::init: one Synth, the patch stores, a table of patches (the host's Patch objects are handles
// into it), the instruments (parts) and their MIDI source. Strings and lists cross through h2_buf, numbers through core registers, floats
// as their bit pattern. Files: the lib's FileSystem calls go to the host through h2_fs_trap (the host hooks its address and serves the
// request in h2_fs from host_simcard through the firmware's own FileSystemAdapter). The h_* API above stays for run.py.
#include "PolySynth/Musicology.h"
namespace
{
constexpr int kH2Patches = 96;
constexpr int kH2Instr = 16;
std::shared_ptr<polysynth::Synth> g2_synth;
std::unique_ptr<polysynth::PatchStore> g2_store[4];
std::shared_ptr<polysynth::Patch> g2_patch[kH2Patches];
std::shared_ptr<polysynth::Instrument> g2_instr[kH2Instr];

class Keys2 : public polysynth::MidiSource
{
  public:
    Keys2() : MidiSource(Type::kInternalKeyboard) {}
    void subscribeMidiEvents(int id, std::weak_ptr<MidiEventCallback> cb) override
    {
        if (id >= 0 && id < kH2Instr) callbacks[id] = cb;
    }
    std::weak_ptr<MidiEventCallback> callbacks[kH2Instr];
};
Keys2* g2_keys;
}   // namespace

extern "C"
{
char h2_buf[16384];
int h2_len;
// one FileSystem request (op: 1 open 2 close 3 write 4 read 5 seek 6 mkdir 7 list), fs the FileSystemId, path in h2_path, data in h2_fsbuf
struct H2Fs { int op, fs, handle, arg, ret; } h2_fs;
char h2_path[512];
uint8_t h2_fsbuf[16384];
__attribute__((noinline, used)) void h2_fs_trap() { asm volatile("" ::: "memory"); }
}

namespace
{
int fsCall(int op, int fs, int handle, int arg, const std::string* path = nullptr)
{
    h2_fs.op = op; h2_fs.fs = fs; h2_fs.handle = handle; h2_fs.arg = arg; h2_fs.ret = -1;
    if (path)
    {
        const size_t k = path->size() < sizeof h2_path - 1 ? path->size() : sizeof h2_path - 1;
        std::memcpy(h2_path, path->data(), k);
        h2_path[k] = 0;
    }
    h2_fs_trap();
    return h2_fs.ret;
}

// the lib's FileSystem: every call is a host request. Spans arrive in the GCC 10.3 layout ({size, ptr}): data() holds the size and size()
// the pointer when read through this file's 12.3 std::span
class HostFs : public polysynth::FileSystem
{
  public:
    explicit HostFs(int id) : id(id) {}
    std::vector<std::string_view> listFiles(const std::string& path, std::function<bool(const std::string&)> matcher, tiny_optional<int> maxFiles) override
    {
        names.clear();
        const int n = fsCall(7, id, 0, maxFiles ? *maxFiles : -1, &path);
        const char* p = reinterpret_cast<const char*>(h2_fsbuf);
        for (int i = 0; i < n; i++)
        {
            std::string s(p);
            p += s.size() + 1;
            if (!matcher || matcher(s)) names.push_back(std::move(s));
            if (maxFiles && int(names.size()) >= *maxFiles) break;
        }
        std::vector<std::string_view> v;
        for (const auto& s : names) v.emplace_back(s);
        return v;
    }
    std::optional<FileHandle> open(const std::string& path, OpenOptions o) override
    {
        const int h = fsCall(1, id, 0, int(o), &path);
        if (h <= 0) return std::nullopt;
        return reinterpret_cast<FileHandle>(intptr_t(h));
    }
    void close(FileHandle f) override { fsCall(2, id, int(intptr_t(f)), 0); }
    bool write(FileHandle f, std::span<const uint8_t> d) override
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(d.size());
        size_t n = reinterpret_cast<size_t>(d.data());
        while (n)
        {
            const size_t k = n < sizeof h2_fsbuf ? n : sizeof h2_fsbuf;
            std::memcpy(h2_fsbuf, p, k);
            if (fsCall(3, id, int(intptr_t(f)), int(k)) != 1) return false;
            p += k;
            n -= k;
        }
        return true;
    }
    int32_t read(FileHandle f, std::span<uint8_t> b) override
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(b.size());
        const size_t n = reinterpret_cast<size_t>(b.data());
        size_t got = 0;
        while (got < n)
        {
            const size_t want = n - got < sizeof h2_fsbuf ? n - got : sizeof h2_fsbuf;
            const int r = fsCall(4, id, int(intptr_t(f)), int(want));
            if (r < 0) return got ? int32_t(got) : -1;
            std::memcpy(p + got, h2_fsbuf, size_t(r));
            got += size_t(r);
            if (size_t(r) < want) break;
        }
        return int32_t(got);
    }
    bool seek(FileHandle f, int off) override { return fsCall(5, id, int(intptr_t(f)), off) == 1; }
    void createFolder(const std::string& path) override { fsCall(6, id, 0, 0, &path); }
    int id;
    std::vector<std::string> names;
};

polysynth::Patch* P(int h) { return h >= 0 && h < kH2Patches ? g2_patch[h].get() : nullptr; }
int putStr(const std::string& s)
{
    const size_t k = s.size() < sizeof h2_buf - 1 ? s.size() : sizeof h2_buf - 1;
    std::memcpy(h2_buf, s.data(), k);
    h2_buf[k] = 0;
    h2_len = int(k);
    return int(k);
}
int newPatch(std::unique_ptr<polysynth::Patch> p)
{
    if (!p) return -1;
    for (int i = 0; i < kH2Patches; i++)
        if (!g2_patch[i]) { g2_patch[i] = std::move(p); return i; }
    return -2;
}
tiny_optional<int> optInt(int v) { return v < 0 ? tiny_optional<int>() : tiny_optional<int>(v); }

// the metadata walk: a flat little endian record the host parses (host_plusproxy.cpp readMeta, same order)
struct W
{
    size_t n = 0;
    bool ok = true;
    void b(const void* p, size_t k) { if (n + k > sizeof h2_buf) { ok = false; return; } std::memcpy(h2_buf + n, p, k); n += k; }
    void i(int v) { b(&v, 4); }
    void f(float v) { b(&v, 4); }
    void s(const std::string& v) { i(int(v.size())); b(v.data(), v.size()); }
    void layout(const polysynth::Patch::Metadata::EditorLayout& l)
    {
        i(int(l.sections.size()));
        for (const auto& sec : l.sections)
        {
            s(sec.name); s(sec.shortName);
            i(int(sec.subsections.size()));
            for (const auto& sub : sec.subsections)
            {
                s(sub.name);
                i(int(sub.elements.size()));
                for (const auto& e : sub.elements) { i(int(e.type)); i(e.id); }
            }
            i(int(sec.elements.size()));
            for (const auto& e : sec.elements) { i(int(e.type)); i(e.id); }
        }
    }
};
}   // namespace

extern "C"
{
int h2_init()
{
    h_step = 100;
    g2_synth = polysynth::Synth::createSynth([](size_t) -> void* { return nullptr; });
    if (!g2_synth) return -1;
    g2_keys = new Keys2();
    return 0;
}
void h2_addfs(int id) { g2_synth->addFileSystem(static_cast<polysynth::Synth::FileSystemId>(id), std::make_unique<HostFs>(id)); }
int h2_store(int fs, int folders)
{
    for (int i = 0; i < 4; i++)
        if (!g2_store[i])
        {
            g2_store[i] = g2_synth->createPatchStore(static_cast<polysynth::Synth::FileSystemId>(fs), folders != 0);
            return g2_store[i] ? i : -1;
        }
    return -2;
}
int h2_create_patch(int store, int type) { return store >= 0 && store < 4 && g2_store[store] ? newPatch(g2_store[store]->createPatch(type)) : -1; }
void h2_free_patch(int h) { if (h >= 0 && h < kH2Patches) g2_patch[h].reset(); }
// name in h2_buf
int h2_load(int store, int h, int sync) { auto* p = P(h); return p && g2_store[store] ? int(g2_store[store]->loadPatch(std::string(h2_buf), *p, sync != 0)) : 0; }
int h2_save(int store, int h) { auto* p = P(h); return p && g2_store[store] ? int(g2_store[store]->savePatch(std::string(h2_buf), *p)) : 0; }
int h2_load_type(int store) { auto t = g2_store[store]->loadPatchInstrumentType(std::string(h2_buf)); return t ? int(*t) : -1; }
// the names NUL separated in h2_buf, the count
int h2_list(int store, int type)
{
    auto v = g2_store[store]->listPatchesByType(type);
    size_t n = 0;
    int c = 0;
    for (auto s : v)
    {
        if (n + s.size() + 1 > sizeof h2_buf) break;
        std::memcpy(h2_buf + n, s.data(), s.size());
        n += s.size();
        h2_buf[n++] = 0;
        c++;
    }
    h2_len = int(n);
    return c;
}
int h2_pget(int h, int id) { auto* p = P(h); auto v = p ? p->getParamValue(id) : std::nullopt; return v ? floatToBits(*v) : floatToBits(__builtin_nanf("")); }
int h2_pfinal(int h, int id) { auto* p = P(h); auto v = p ? p->getParamFinalValue(id) : std::nullopt; return v ? floatToBits(*v) : floatToBits(__builtin_nanf("")); }
int h2_pget_em(int h, int id) { auto* p = P(h); auto v = p ? p->getParamValueEditMapped(id) : std::nullopt; return v ? floatToBits(*v) : floatToBits(__builtin_nanf("")); }
// bits of a NaN: no value (std::nullopt)
void h2_pset(int h, int id, int bits, int src)
{
    auto* p = P(h);
    if (!p) return;
    const float f = bitsToFloat(bits);
    p->setParamValue(id, f != f ? polysynth::Patch::optional_paramvalue_t() : polysynth::Patch::optional_paramvalue_t(f), static_cast<polysynth::Patch::ParamChangeSource>(src));
}
void h2_pset_em(int h, int id, int bits, int src)
{
    auto* p = P(h);
    if (!p) return;
    const float f = bitsToFloat(bits);
    p->setParamValueEditMapped(id, f != f ? tiny_optional<float>() : tiny_optional<float>(f), static_cast<polysynth::Patch::ParamChangeSource>(src));
}
int h2_mget(int h, int m) { auto* p = P(h); auto v = p ? p->getMacroValue(m) : std::nullopt; return v ? floatToBits(*v) : floatToBits(__builtin_nanf("")); }
void h2_mset(int h, int m, int bits, int voice)
{
    auto* p = P(h);
    if (!p) return;
    const float f = bitsToFloat(bits);
    p->setMacroValue(m, f != f ? polysynth::Patch::optional_paramvalue_t() : polysynth::Patch::optional_paramvalue_t(f), optInt(voice));
}
void h2_mmod(int h, int m, int idx, int param) { if (auto* p = P(h)) p->setMacroModifier(m, idx, optInt(param)); }
void h2_mamt(int h, int m, int idx, int bits) { if (auto* p = P(h)) p->setMacroModifierAmount(m, idx, bitsToFloat(bits)); }
int h2_mamt_get(int h, int m, int idx) { auto* p = P(h); auto v = p ? p->getMacroModifierAmount(m, idx) : std::nullopt; return v ? floatToBits(*v) : floatToBits(__builtin_nanf("")); }
void h2_mchg(int h, int m, int idx, int bits) { if (auto* p = P(h)) p->changeMacroModifierAmount(m, idx, bitsToFloat(bits)); }
void h2_mclear(int h) { if (auto* p = P(h)) p->clearMacroModifiers(); }
void h2_mbip(int h, int m, int on) { if (auto* p = P(h)) p->setMacroBipolar(m, on != 0); }
void h2_mname(int h, int m) { if (auto* p = P(h)) p->setMacroDisplayName(m, std::string(h2_buf)); }
void h2_batch(int h, int start) { if (auto* p = P(h)) { if (start) p->startBatchUpdate(); else p->finishBatchUpdate(); } }
// the value text of the param's valueFormatter into h2_buf, its length, -1 when it has none
int h2_format(int h, int id, int bits)
{
    auto* p = P(h);
    if (!p) return -1;
    const auto& ps = p->getMetadata().params;
    if (id < 0 || id >= int(ps.size()) || !ps[size_t(id)].valueFormatter) return -1;
    return putStr(ps[size_t(id)].valueFormatter(*p, bitsToFloat(bits)));
}
// the patch's metadata into h2_buf, its byte length, -1 when it does not fit
int h2_meta(int h)
{
    auto* p = P(h);
    if (!p) return -1;
    const auto& m = p->getMetadata();
    W w;
    w.i(m.instrumentTypeId); w.i(int(m.outputChannels)); w.i(m.macrosAffectParamCC); w.i(m.rendersAuxSends); w.i(m.maxVoices);
    w.i(int(m.params.size()));
    for (const auto& q : m.params)
    {
        w.s(q.displayName);
        w.i(q.defaultValue ? 1 : 0); w.f(q.defaultValue ? *q.defaultValue : 0.f);
        w.f(float(q.minValue)); w.f(float(q.maxValue));
        w.i(int(q.editCurve)); w.i(int(q.macroOperation)); w.i(q.isEnum); w.i(q.midiCC ? int(*q.midiCC) : -1);
        w.i(q.invalidatesOtherMetadata); w.i(q.valueFormatter ? 1 : 0);
    }
    w.i(int(m.macros.size()));
    for (const auto& q : m.macros)
    {
        w.s(q.displayName);
        w.i(int(q.modifiers.size()));
        for (const auto& mod : q.modifiers) { w.i(mod.paramId ? *mod.paramId : -1); w.f(mod.amount); }
        w.i(q.bipolar); w.i(q.perVoice); w.i(q.system);
    }
    w.i(int(m.sampleSlots.size()));
    for (const auto& q : m.sampleSlots) { w.s(q.displayName); w.i(int(q.type)); w.i(q.section); w.i(q.subsection); }
    w.layout(m.layout23);
    w.layout(m.layoutFlat);
    h2_len = int(w.n);
    return w.ok ? int(w.n) : -1;
}
int h2_instr(int id)
{
    if (id < 0 || id >= kH2Instr) return -1;
    g2_instr[id] = g2_synth->createInstrument(id);
    if (!g2_instr[id]) return -2;
    g2_instr[id]->connect(*g2_keys);
    return 0;
}
void h2_assign(int id, int h) { if (g2_instr[id] && P(h)) g2_instr[id]->assignPatch(g2_patch[h]); }
void h2_voices(int id, int n) { if (g2_instr[id]) g2_instr[id]->setVoiceCount(n); }
int h2_voices_get(int id) { return g2_instr[id] ? g2_instr[id]->getVoiceCount() : 0; }
void h2_chan(int id, int c) { if (g2_instr[id]) g2_instr[id]->setMidiChannel(c); }
int h2_chan_get(int id) { return g2_instr[id] ? g2_instr[id]->getMidiChannel() : 0; }
void h2_local(int id, int m) { if (g2_instr[id]) g2_instr[id]->setLocalMode(static_cast<polysynth::Instrument::LocalMode>(m)); }
int h2_local_get(int id) { return g2_instr[id] ? int(g2_instr[id]->getLocalMode()) : 0; }
// setup with the first n instruments (the firmware passes kSynthParts, no effects, no mixer, the tempo of h2_tempo)
float h2_tempo = 120.f;
int h2_tempo_ok = 1;
void h2_setup(int n)
{
    static std::array<std::shared_ptr<polysynth::Instrument>, kH2Instr> list;
    for (int i = 0; i < n && i < kH2Instr; i++) list[size_t(i)] = g2_instr[i];
    g2_synth->setup(span10(list.data(), size_t(n)), span10<std::shared_ptr<polysynth::Instrument>>(nullptr, 0), nullptr,
                    []() -> tiny::optional<float> { return h2_tempo_ok ? tiny::optional<float>(h2_tempo) : tiny::optional<float>(); });
}
void h2_disconnect(int id) { g2_synth->disconnectInstrument(id); }
// a MIDI event the host's MidiAdapter sent to instrument id: the 4 bytes of polysynth::MidiEvent as one word, the source id
void h2_event(int id, int ev, int source)
{
    if (id < 0 || id >= kH2Instr) return;
    polysynth::MidiEvent e{};
    static_assert(sizeof e == 4, "MidiEvent crosses as one word");
    std::memcpy(&e, &ev, 4);
    if (auto c = g2_keys->callbacks[id].lock()) (*c)(e, source);
}
void h2_update_audio() { g2_synth->updateAudio(); }
void h2_update_main() { g2_synth->updateMain(); }
void h2_render(int id)
{
    std::memset(h_outL, 0, sizeof h_outL);
    std::memset(h_outR, 0, sizeof h_outR);
    std::memset(h_send0, 0, sizeof h_send0);
    std::memset(h_send1, 0, sizeof h_send1);
    static std::array<polysynth::AudioBufferRef, 2> sends = { polysynth::AudioBufferRef(h_send0), polysynth::AudioBufferRef(h_send1) };
    g2_synth->renderAudioBuffer(polysynth::AudioBufferStereoRef{ polysynth::AudioBufferRef(h_outL), polysynth::AudioBufferRef(h_outR) },
                                span10(sends.data(), sends.size()), id);
}
int h2_type_name(int type) { return putStr(g2_synth->getInstrumentName(type)); }
// the type ids into h_ids, their count
int h2_type_ids()
{
    auto v = g2_synth->getInstrumentTypeIds();
    int n = 0;
    for (int t : v)
        if (n < 128) h_ids[n++] = t;
    return n;
}
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// P7 H5: plugin synth engines in this guest, as the IMX image plays them (core/synth/engines/PlugSynthVoice.cpp, compiled into this
// harness). The host writes a whole .plg into h2_plg and calls h2_plug_attach: the loader of the image (plug_load.h loadImage, the
// one plug::imx::load runs) places it in one of the guest's code slots, PlugSynthVoice attaches it and registers it in the lib under
// its type id, so createPatch and createInstrument of that type work as for a built-in engine. The services table is the IMX one
// (plug_imx.cpp s_host: newlib libm, the codec rate 44,100 Hz). The guest's RAM is one flat rwx region: no cache steps.
#include "plug_load.h"
#include "engines/PlugSynthVoice.h"
#include <cmath>
namespace
{
uint8_t g_plugSlot[polysynth::plugimx::kSlots][plug::kImxSynthSlotBytes] __attribute__((aligned(32)));
struct NoCache
{
    void cleanDCache(const void*, uint32_t) {}
    void invalidateICache() {}
    void barrier() {}
};
void* hAlloc(uint32_t, uint32_t) { return nullptr; }   // init only on the unit (beginAlloc), no engine of the set asks
uint32_t hCycles() { return 0; }
const void* hQuery(uint32_t) { return nullptr; }
float hSin(float x) { return sinf(x); }
float hCos(float x) { return cosf(x); }
float hTan(float x) { return tanf(x); }
float hTanh(float x) { return tanhf(x); }
float hExp(float x) { return expf(x); }
float hLog(float x) { return logf(x); }
float hPow(float x, float y) { return powf(x, y); }
float hExp2(float x) { return exp2f(x); }
float hLog2(float x) { return log2f(x); }
const plug_host g_plugHost = {
    sizeof(plug_host), PLUG_HOST_LEVEL, 44100.0f, hSin, hCos, hTan, hTanh, hExp, hLog, hPow, memcpy, memset,
    hAlloc,            hCycles,         hQuery,   hExp2, hLog2,
};
}   // namespace

extern "C"
{
uint8_t h2_plg[64 * 1024];
// loads h2_plg[0..len) into code slot `slot` (0..2) and registers the engine: its type id, or plugimx::attach's error, -100 - plug::Err
int h2_plug_attach(int slot, int len)
{
    if (slot < 0 || slot >= polysynth::plugimx::kSlots || len <= 0 || len > int(sizeof h2_plg)) return -2;
    polysynth::plugimx::detach(slot);
    uint32_t entry = 0;
    NoCache ops;
    uint8_t* s = g_plugSlot[slot];
    const plug::Err e = plug::loadImage(h2_plg, uint32_t(len), PLUG_KIND_SYNTH, s, plug::kImxSynthSlotBytes, uint32_t(uintptr_t(s)), entry, ops);
    if (e != plug::kOk) return -100 - int(e);
    return polysynth::plugimx::attach(slot, entry, &g_plugHost);
}
void h2_plug_detach(int slot) { polysynth::plugimx::detach(slot); }
// paraphony (the plus sim's Synth::plugChordPara and plugNoteChord, host_plusproxy.cpp): PlugSynthVoice chordInfo and noteChord.
// cs: the chord in bits 0-7, the strum (-63..63) in bits 8-15 as int8
int h2_chord_info(int type) { return polysynth::plugimx::chordInfo(type); }
void h2_note_tag(int id, int end) { polysynth::plugimx::noteTag(uint16_t(id), uint8_t(end)); }
int h2_mpe_voice(int i, int f) { return polysynth::plugimx::mpeProbe(i, f); }
void h2_mpe_note(int chan) { polysynth::plugimx::mpeNote(uint8_t(chan)); }
void h2_part_mpe(int part, int range, int pressPercent) { polysynth::plugimx::partMpe(part, float(range), float(pressPercent) * 0.01f); }
void h2_note_bend(int part, int chan, int raw) { polysynth::plugimx::partNoteBend(part, uint8_t(chan), raw); }
void h2_note_press(int part, int chan, int press) { polysynth::plugimx::partNotePress(part, uint8_t(chan), uint8_t(press)); }
void h2_note_chord(int part, int note, int cs) { polysynth::plugimx::noteChord(part, uint8_t(note), uint8_t(cs & 0xFF), int8_t(uint8_t(cs >> 8))); }
}
