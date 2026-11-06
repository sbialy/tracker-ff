// Synth engine plugins (FEATURE_PLUG_HOST, phase 3): the FlexNVM installer and the ISynthEngine adapter. See plug_host.h,
// plug_bank.h (bank layout, installed.txt, power cut rules) and ~/prj/poly/ref/plugin-architecture.md (sections 4.6 and 12).
//
// At boot, before the synth starts (TrackerImpl::init calls synthBoot before Synth::init): the PLUG_KIND_SYNTH files in
// /Plugins/Synth are installed into the bank where needed (bankSync: no flash command when the bytes are there), then every
// installed engine is attached: its rw segment's initial bytes are copied from the flash to its place in the SRAM pool (the
// text was rebased for that fixed address at install), the bss zeroed, plug_entry and init run, PLUG_EXT_SYNTH is checked and
// the engine joins the shim's engine list (open::engineListAdd) behind an adapter. The engine text runs from the flash.
// /Plugins/synth.txt says what happened.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "plug_bank.h"

#include "Platform.h"
#include "cr_section_macros.h"
#include "open_engine.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace plug
{

namespace
{
using namespace ot::audio;
using polysynth::open::EngineDef;

static_assert(sizeof(polysynth::open::ParamDef) == sizeof(plug_synth_param) &&
                offsetof(polysynth::open::ParamDef, enumNames) == offsetof(plug_synth_param, enum_names),
              "ParamDef is plug_synth_param");
static_assert(sizeof(polysynth::open::SectionDef) == sizeof(plug_synth_section), "SectionDef is plug_synth_section");
static_assert(sizeof(polysynth::open::MacroDef) == sizeof(plug_synth_macro) &&
                offsetof(polysynth::open::MacroDef, targets) == offsetof(plug_synth_macro, targets),
              "MacroDef is plug_synth_macro");
static_assert(int(polysynth::open::Fmt::kPan) == PLUG_FMT_PAN && int(polysynth::open::CurveKind::kPower) == PLUG_CURVE_POWER,
              "the shim's formats and curves are PLUG_FMT_* and PLUG_CURVE_*");
constexpr uint32_t kLayout = PLUG_SYNTH_LAYOUT(sizeof(SynthVoice), sizeof(SynthParams), sizeof(SynthNote), sizeof(SynthSlotScratch));

constexpr const char* kSynthDir = "/Plugins/Synth";
constexpr const char* kIndexPath = "/Plugins/installed.txt";
constexpr uint32_t kPoolBytes = 2560;    // rw + bss of the installed engines (JU6 1,648 B with its chorus lines, FM4 112 B, B-Type 120 B)
constexpr uint32_t kStageBytes = 32768;  // one engine's image + bss while it is rebased for the flash

// The engines' rw segments at a fixed SRAM address: the text in the flash holds their addresses, so they never move while
// this firmware runs (another firmware build moves the pool: bankCheck drops the entries, they are installed again once).
__attribute__((aligned(8))) uint8_t s_pool[kPoolBytes];
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_bstage[kStageBytes];
__NOINIT(EXTERNAL_RAM) uint8_t s_sec[kBankSector];
__NOINIT(EXTERNAL_RAM) char s_btext[4096];
__NOINIT(EXTERNAL_RAM) Bank s_bank;
__NOINIT(EXTERNAL_RAM) IndexEntry s_sidx[kBankMax];
__NOINIT(EXTERNAL_RAM) BankWant s_want[kBankMax];
__NOINIT(EXTERNAL_RAM) ScanStats s_sscan;
FileSystem* s_sfs;

// The ISynthEngine of an installed engine: every call goes to its PLUG_EXT_SYNTH function (audio context, as built in)
class PlugSynth final : public ISynthEngine
{
  public:
    explicit PlugSynth(const plug_ext_synth* e) : x(e) {}
    const plug_ext_synth* const x;
    int32_t id() const override { return x->type_id; }
    const char* name() const override { return x->name; }
    void prepare(SynthSlotScratch& sc, const SynthParams& p) override
    {
        if (x->prepare) x->prepare(&sc, &p);
    }
    void slotReset(SynthSlotScratch& sc) override
    {
        if (x->slot_reset) x->slot_reset(&sc);
    }
    void setTempo(float bpm) override
    {
        if (x->set_tempo) x->set_tempo(bpm);
    }
    bool setSendTap(int slot, float* d, float* r, float gd, float gr) override
    {
        return x->set_send_tap && x->set_send_tap(slot, d, r, gd, gr);
    }
    void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override { x->note_on(&v, &p, &n); }
    void noteOff(SynthVoice& v, const SynthParams& p) override { x->note_off(&v, &p); }
    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* l, float* r) override
    {
        return x->render(&v, &p, g0, gInc, l, r) != 0;
    }
    bool slotPost(int slot, SynthSlotScratch& sc, const SynthParams& p, float* l, float* r) override
    {
        return x->slot_post && x->slot_post(slot, &sc, &p, l, r);
    }
};
// raw storage, constructed at attach: a global PlugSynth (virtual destructor) would need a static constructor
alignas(PlugSynth) uint8_t s_eng[kBankMax][sizeof(PlugSynth)];
EngineDef s_defs[kBankMax] = {};
int8_t s_attachErr[kBankMax];   // per bank entry: 0 attached, else why not (synth.txt)

// ------------------------------------------------------------------------------------------------ bank adapters
struct NvmFlash
{
    int commands = 0;
    const uint8_t* at(uint32_t off) { return reinterpret_cast<const uint8_t*>(kBankRun + off); }
    bool erase(uint32_t off)
    {
        commands++;
        return nvmErase(off);
    }
    bool program(uint32_t off, const uint8_t* d)
    {
        commands++;
        return nvmProgram(off, d);
    }
};

struct SdIo
{
    int readIndex(char* b, int cap)
    {
        auto [ok, f] = s_sfs->openFile(kIndexPath, FileSystem::FileOpenMode::kRead);
        if (!ok) return -1;
        const int n = int(s_sfs->readFile(b, size_t(cap), f));
        s_sfs->closeFile(f);
        return n;
    }
    bool writeIndex(const char* t, int n) { return writeText(kIndexPath, t, n); }
    // the .plg of e, rebased for e's flash and pool addresses, in the stage: text, rw initial bytes, then zeros (bss, padding)
    const uint8_t* image(const BankEntry& e)
    {
        char p[64];
        snprintf(p, sizeof(p), "%s/%s", kSynthDir, e.file);
        Place pl;
        pl.textDst = s_bstage;
        pl.textLen = kStageBytes;
        pl.textRun = kBankRun + e.off;
        pl.rwRun = e.rwRun;
        pl.run = false;
        Loaded l;
        if (load(s_sfs, p, pl, PLUG_KIND_SYNTH, l) != kOk || l.hdr.uid != e.uid || l.hdr.crc32 != e.fileCrc) return nullptr;
        memset(s_bstage + l.hdr.image_size, 0, e.len - l.hdr.image_size);   // padding: zeros, bss follows the rw bytes
        return s_bstage;
    }
};

// ------------------------------------------------------------------------------------------------ attach
inline bool in(const void* p, uintptr_t lo, uint32_t n)
{
    return uintptr_t(p) - lo < n;
}

inline bool fn(const void* f, uintptr_t lo, uint32_t n, bool need)
{
    return f ? (uintptr_t(f) & 1u) && in(reinterpret_cast<const void*>(uintptr_t(f) & ~1u), lo, n) : !need;
}

// Everything the shim, the editor and the patch files will read from the extension: inside the engine's text (its rw for the
// extension struct itself), counts and indices in range, the shim's formats only
bool checkSynth(const plug_ext_synth* x, uintptr_t t, uint32_t tn, uintptr_t rw, uint32_t rwn)
{
    if (!x || !(in(x, t, tn) || in(x, rw, rwn)) || x->struct_size < sizeof(plug_ext_synth) || x->layout != kLayout) return false;
    const uint32_t np = x->param_count;
    if (!in(x->name, t, tn) || !x->name[0] || strnlen(x->name, 9) > 8 || strchr(x->name, '/') || np > kSynthParamMax ||
        x->section_count > 8 || x->macro_count > 6 || x->max_voices - 1u > 3u || x->volume_param >= int32_t(np) ||
        x->volume_param < -1 || !fn(reinterpret_cast<const void*>(x->note_on), t, tn, true) ||
        !fn(reinterpret_cast<const void*>(x->note_off), t, tn, true) || !fn(reinterpret_cast<const void*>(x->render), t, tn, true))
        return false;
    const void* const opt[] = {reinterpret_cast<const void*>(x->prepare), reinterpret_cast<const void*>(x->slot_reset),
                               reinterpret_cast<const void*>(x->set_tempo), reinterpret_cast<const void*>(x->slot_post),
                               reinterpret_cast<const void*>(x->set_send_tap), reinterpret_cast<const void*>(x->set_work)};
    for (const void* f : opt)
        if (!fn(f, t, tn, false)) return false;
    if (np && (!in(x->params, t, tn) || !in(x->params + np - 1, t, tn))) return false;
    for (uint32_t i = 0; i < np; i++)
    {
        const plug_synth_param& p = x->params[i];
        if (!in(p.name, t, tn) || p.fmt >= PLUG_FMT_COUNT || p.curve >= PLUG_CURVE_COUNT || !(p.max >= p.min)) return false;
        if (p.fmt == PLUG_FMT_ENUM)
        {
            const uint32_t ne = uint32_t(p.max) + 1u;
            if (p.min != 0.0f || p.max > 64.0f || !in(p.enum_names, t, tn) || !in(p.enum_names + ne - 1, t, tn)) return false;
            for (uint32_t k = 0; k < ne; k++)
                if (!in(p.enum_names[k], t, tn)) return false;
        }
    }
    if (x->section_count && (!in(x->sections, t, tn) || !in(x->sections + x->section_count - 1, t, tn))) return false;
    for (uint32_t s = 0; s < x->section_count; s++)
    {
        const plug_synth_section& c = x->sections[s];
        if (!in(c.name, t, tn) || !in(c.short_name, t, tn) || (c.count && (!in(c.ids, t, tn) || !in(c.ids + c.count - 1, t, tn))))
            return false;
        for (uint32_t k = 0; k < c.count; k++)
            if (c.ids[k] >= np) return false;
    }
    if (x->macro_count && (!in(x->macros, t, tn) || !in(x->macros + x->macro_count - 1, t, tn))) return false;
    for (uint32_t m = 0; m < x->macro_count; m++)
    {
        const plug_synth_macro& c = x->macros[m];
        if (!in(c.name, t, tn) || c.count > 2 || (c.count && c.targets[0].param >= np) || (c.count > 1 && c.targets[1].param >= np))
            return false;
    }
    return true;
}

// 0 attached, else: 1 entry refused the host, 2 descriptor, 3 init, 4 extension, 5 no working RAM, 6 numbers taken or list full
int attach(int k)
{
    const BankEntry& e = s_bank.e[k];
    const uintptr_t t = kBankRun + e.off;
    uint8_t* const rw = reinterpret_cast<uint8_t*>(e.rwRun);
    const uint32_t init = e.len - e.text;   // rw initial bytes and the zero padding
    memcpy(rw, reinterpret_cast<const void*>(t + e.text), init);
    memset(rw + init, 0, e.rwLen - init);
    *reinterpret_cast<volatile uint32_t*>(0x4000D000) = 0;   // the K66 system MPU lets the core fetch anywhere (as load())
    syncCode();
    const plug_desc* d = reinterpret_cast<plug_entry_fn>((t + e.entry) | 1u)(hostTable(), (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
    if (!d) return 1;
    if (checkDesc(d, t, e.text, PLUG_KIND_SYNTH) != kOk) return 2;
    static uint64_t inst;   // a synth engine has no instance: init gets 8 bytes it may ignore
    if (d->init(&inst, hostTable()) != 0) return 3;
    const plug_ext_synth* x =
      fn(reinterpret_cast<const void*>(d->query), t, e.text, true) ? static_cast<const plug_ext_synth*>(d->query(PLUG_EXT_SYNTH)) : nullptr;
    if (!checkSynth(x, t, e.text, e.rwRun, e.rwLen)) return 4;
    if (x->work_bytes)
    {   // the engine's working RAM (pools, delay lines), SRAM heap, zeroed, kept while the firmware runs
        void* w = calloc(1, (x->work_bytes + 7u) & ~7u);
        if (!w) return 5;
        if (x->set_work) x->set_work(w, x->work_bytes);
    }
    EngineDef& def = s_defs[k];
    def.name = x->name;
    def.typeId = x->type_id;
    def.params = reinterpret_cast<const polysynth::open::ParamDef*>(x->params);
    def.paramCount = int(x->param_count);
    def.sections = reinterpret_cast<const polysynth::open::SectionDef*>(x->sections);
    def.sectionCount = int(x->section_count);
    def.macros = reinterpret_cast<const polysynth::open::MacroDef*>(x->macros);
    def.macroCount = int(x->macro_count);
    def.volumeParam = x->volume_param;
    def.maxVoices = int(x->max_voices);
    def.slotFx = (x->flags & PLUG_SYNTH_SLOT_FX) && x->slot_post;
    def.inst = new (s_eng[k]) PlugSynth(x);
    return polysynth::open::engineListAdd(&def, x->stored, x->patch_engine) ? 0 : 6;
}

}   // namespace

void synthBoot(FileSystem* fs)
{
    s_sfs = fs;
    useFs(fs);
    memset(&s_bank, 0, sizeof(s_bank));
    const int n = scanDir(kSynthDir, s_sidx, kBankMax, s_sscan, PLUG_KIND_SYNTH);
    for (int i = 0; i < n; i++)
    {
        const IndexEntry& x = s_sidx[i];
        BankWant& w = s_want[i];
        w.uid = x.uid;
        w.version = x.version;
        w.fileCrc = x.crc;
        w.text = x.rwOffset ? x.rwOffset : x.imageSize;
        w.len = x.imageSize;
        w.rwLen = x.imageSize - w.text + x.bssSize;
        w.entry = x.entry;
        snprintf(w.file, sizeof(w.file), "%s", x.file);
    }
    const bool hadIndex = fs->exists(kIndexPath);
    if (!n && !hadIndex) return;   // no synth plugin was ever installed: nothing to do, nothing written
    NvmFlash f;
    SdIo io;
    const BankStatus st = bankSync(f, io, s_want, n, uintptr_t(s_pool), kPoolBytes, s_bank, s_sec, s_btext, sizeof(s_btext));
    // the crash marker (plug_host.cpp boot() reads it): a marker left by the last session means safe mode, no engine runs
    const bool safe = fs->exists(kMarker);
    if (!safe && s_bank.n) writeText(kMarker, "synth engines", 13);
    for (int k = 0; k < s_bank.n; k++) s_attachErr[k] = int8_t(safe ? 7 : attach(k));
    if (!safe && s_bank.n) fs->remove(kMarker);
    char* t = s_btext;
    const int cap = int(sizeof(s_btext));
    int len = snprintf(t, size_t(cap), "synth bank: listed %d dropped %d installed %d failed %d skipped %d commands %d\r\n",
                       s_sscan.listed, st.dropped, st.installed, st.failed, st.skipped, st.commands);
    for (int k = 0; k < s_bank.n && len < cap; k++)
        len += snprintf(t + len, size_t(cap - len), "%s at %lx attach %d\r\n", s_bank.e[k].file,
                        (unsigned long)(kBankRun + s_bank.e[k].off), int(s_attachErr[k]));
    writeText("/Plugins/synth.txt", t, len < cap ? len : cap - 1);
}

}   // namespace plug

#endif   // FEATURE_PLUG_HOST
