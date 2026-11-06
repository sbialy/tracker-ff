// Synth engine plugins (FEATURE_PLUG_HOST, phase 3): the library, the FlexNVM cache and the ISynthEngine adapter. See plug_host.h,
// plug_bank.h (bank layout, installed.txt, power cut rules) and ~/prj/poly/ref/plugin-architecture.md (sections 4.6 and 12).
//
// Library (round E): every PLUG_KIND_SYNTH file in /Plugins/Synth whose manifest carries its numbers (build_plug.py --synth) is
// in the shim's engine list from boot, installed or not (synthBoot, before Synth::init): an EngineDef per library entry that
// never moves, with the name and type and, while the engine is not attached, no parameters and no engine (the shim's "missing"
// placeholder: a part on it is silent, its files are never rewritten). The FlexNVM bank is a cache of the library: synthEnsure
// installs an engine when a part needs it (Synth::setInstrumentType, the project load), evicting the least recently used ones
// no part needs. Attaching copies the rw segment's initial bytes from the flash to its place in the SRAM pool (the text was
// rebased for that fixed address at install), zeroes the bss, runs plug_entry and init, checks PLUG_EXT_SYNTH and fills the
// EngineDef. Evicting does the reverse with the audio interrupt off (open::engineGone cuts the parts that still point at it).
// The engine text runs from the flash. /Plugins/synth.txt says what happened.
//
// Logos (round B, plug_logo.h): at the end of plug::boot, every listed engine's card logo (a .plgi sidecar, else a trailer in its
// .plg) is written to the RAM_G logo pool, one slot per library entry (12 slots, the first 12 entries), installed or not.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "plug_bank.h"
#include "plug_logo.h"

#include "DisplayDriver.h"
#include "Platform.h"
#include "displayStructs.h"
#include "cr_section_macros.h"
#include "interruptsControl.h"
#include "open_engine.h"
#include "open_load.h"

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
static_assert(int(polysynth::open::Fmt::kPan) == PLUG_FMT_PAN && int(polysynth::open::Fmt::kUnit) == PLUG_FMT_UNIT &&
                PLUG_FMT_COUNT == 16 && int(polysynth::open::CurveKind::kPower) == PLUG_CURVE_POWER,
              "the shim's formats and curves are PLUG_FMT_* and PLUG_CURVE_*");
// Sections of a plugin engine. Nothing is sized by it (the editor keeps them in a vector), it only bounds what the host walks.
// PHZ has 10, the most of any engine
constexpr uint32_t kSynthSectionMax = 10;
constexpr uint32_t kLayout = PLUG_SYNTH_LAYOUT(sizeof(SynthVoice), sizeof(SynthParams), sizeof(SynthNote), sizeof(SynthSlotScratch));

constexpr const char* kSynthDir = "/Plugins/Synth";
constexpr const char* kIndexPath = "/Plugins/installed.txt";
constexpr const char* kStatusPath = "/Plugins/synth.txt";
// rw + bss of the installed engines. JU6 1,648 B (its chorus lines), the other 11 112 to 160 B each: all 12 together 2,976 B, so
// every engine of the library keeps its own rw range and the pool never decides what is evicted (only the flash does). The worst
// 3 at once need 1,928 B (JU6, T-Type and one of 120 B) and the 3 plus the one being picked 2,048 B: a 2,048 B pool would save
// 1,024 B but then hold JU6 plus three others only, and picking a 5th engine would rewrite the flash (plug_bank_test prints both).
constexpr uint32_t kPoolBytes = 3072;
constexpr uint32_t kStageBytes = 32768;  // one engine's image + bss while it is rebased for the flash

__attribute__((aligned(8))) uint8_t s_pool[kPoolBytes];
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_bstage[kStageBytes];
// The engines' work memory (PLUG_EXT_SYNTH work_bytes: PLK's 8,772 B delay line pool), SDRAM, so no engine takes internal
// heap. A library entry gets its block at its first attach and keeps it (zeroed at every attach): attach and evict repeat.
constexpr uint32_t kWorkBytes = 16384;
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_work[kWorkBytes];
uint32_t s_workUsed;
__NOINIT(EXTERNAL_RAM) uint8_t s_sec[kBankSector];
__NOINIT(EXTERNAL_RAM) char s_btext[4096];   // synth.txt
__NOINIT(EXTERNAL_RAM) char s_itext[2048];   // installed.txt
__NOINIT(EXTERNAL_RAM) Bank s_bank;
__NOINIT(EXTERNAL_RAM) Bank s_bankWork[2];   // bankInstall's plan and journal (main loop only, as every bankInstall call)
__NOINIT(EXTERNAL_RAM) IndexEntry s_sidx[kLibMax];
__NOINIT(EXTERNAL_RAM) ScanStats s_sscan;
FileSystem* s_sfs;
int s_sn;            // library entries (the scan of /Plugins/Synth)
bool s_safe;         // the crash marker was there at boot: no engine is attached this session
bool s_uiUp;
int s_logos;         // logo slots used, slot s at kLogoRamG + s * kLogoPixels
BankOpenStatus s_open;
uint32_t s_tick;     // last use clock (LRU)
static_assert(kLibMax <= 16 && kLogoSlots <= kLibMax, "one logo slot per library entry, the first 12");
static_assert(kPlugLogoFirst + 2 * kLogoSlots <= 255, "logo ids fit the label's uint8_t bitmapIndex");
extern "C" volatile uint32_t systick_millis_count;

// The ISynthEngine of a library engine: every call goes to its PLUG_EXT_SYNTH function (audio context, as built in). x is null
// while the engine is not attached: the calls do nothing (render ends the voice), nothing runs from the flash.
class PlugSynth final : public ISynthEngine
{
  public:
    explicit PlugSynth(int32_t type, const char* n) : t(type), nm(n) {}
    const plug_ext_synth* volatile x = nullptr;
    const int32_t t;
    const char* const nm;
    int32_t id() const override { return t; }
    const char* name() const override { return nm; }
    void prepare(SynthSlotScratch& sc, const SynthParams& p) override
    {
        const plug_ext_synth* e = x;
        if (e && e->prepare) e->prepare(&sc, &p);
    }
    void slotReset(SynthSlotScratch& sc) override
    {
        const plug_ext_synth* e = x;
        if (e && e->slot_reset) e->slot_reset(&sc);
    }
    void setTempo(float bpm) override
    {
        const plug_ext_synth* e = x;
        if (e && e->set_tempo) e->set_tempo(bpm);
    }
    bool setSendTap(int slot, float* d, float* r, float gd, float gr) override
    {
        const plug_ext_synth* e = x;
        return e && e->set_send_tap && e->set_send_tap(slot, d, r, gd, gr);
    }
    void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override
    {
        const plug_ext_synth* e = x;
        if (e) e->note_on(&v, &p, &n);
    }
    void noteOff(SynthVoice& v, const SynthParams& p) override
    {
        const plug_ext_synth* e = x;
        if (e) e->note_off(&v, &p);
    }
    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* l, float* r) override
    {
        const plug_ext_synth* e = x;
        return e && e->render(&v, &p, g0, gInc, l, r) != 0;
    }
    bool slotPost(int slot, SynthSlotScratch& sc, const SynthParams& p, float* l, float* r) override
    {
        const plug_ext_synth* e = x;
        return e && e->slot_post && e->slot_post(slot, &sc, &p, l, r);
    }
};

// One library entry: its file (s_sidx), its definition (stable, the registry and the list point at it) and its state
struct Lib
{
    char name[9];
    int8_t listed;      // 1 in the engine list, 0 not (no numbers in the manifest, numbers taken, bad name)
    int8_t err;         // 0, else the last attach (1..8) or install (10 + BankErr) error, 9 no numbers, 11.. see synth.txt
    bool attached;
    uint8_t logo;       // 1 + its logo slot, 0 none (the text name)
    int8_t logoSrc, logoErr;
    uint32_t used;      // last use (s_tick)
    uint8_t* work;      // its work memory block (kept across evictions)
    uint32_t workLen;
};
Lib s_lib[kLibMax];
EngineDef s_defs[kLibMax] = {};
// raw storage, constructed at boot: a global PlugSynth (virtual destructor) would need a static constructor
alignas(PlugSynth) uint8_t s_eng[kLibMax][sizeof(PlugSynth)];
inline PlugSynth& eng(int k)
{
    return *reinterpret_cast<PlugSynth*>(s_eng[k]);
}

// The last installs, for synth.txt
struct Install
{
    char file[kBankFileLen];
    int err, evicted, commands, erases;
    uint32_t ms, cmdMs;
};
Install s_log[8];
int s_logN;

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
    // after moving back an interrupted replace's installed.txt.bak (writeText). -1 for no file and for a card error alike: the
    // boot then writes nothing back, and the bank is a cache (an engine it forgets is installed again from its .plg)
    int readIndex(char* b, int cap) { return readText(kIndexPath, b, cap); }
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

int libOf(const BankEntry& e)
{
    for (int k = 0; k < s_sn; k++)
        if (s_sidx[k].uid == e.uid && s_sidx[k].crc == e.fileCrc) return k;
    return -1;
}

int bankOf(int k)
{
    for (int i = 0; i < s_bank.n; i++)
        if (s_bank.e[i].uid == s_sidx[k].uid && s_bank.e[i].fileCrc == s_sidx[k].crc) return i;
    return -1;
}

int libForType(int type)
{
    for (int k = 0; k < s_sn; k++)
        if (s_lib[k].listed && s_defs[k].typeId == type) return k;
    return -1;
}

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
        x->section_count > kSynthSectionMax || x->macro_count > 6 || x->max_voices - 1u > 3u || x->volume_param >= int32_t(np) ||
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


// The definition of an engine that is not attached: its name and type only, as the shim's missing placeholder
void placeholder(int k)
{
    EngineDef& d = s_defs[k];
    d.params = nullptr;
    d.paramCount = 0;
    d.sections = nullptr;
    d.sectionCount = 0;
    d.macros = nullptr;
    d.macroCount = 0;
    d.volumeParam = -1;
    d.maxVoices = polysynth::open::kMaxVoicesPerInstrument;
    d.slotFx = false;
    d.inst = nullptr;
}

// 0 attached, else: 1 entry refused the host, 2 descriptor, 3 init, 4 extension, 5 no working RAM, 7 not installed,
// 8 the extension's numbers or name are not the manifest's
int attach(int k)
{
    const int bi = bankOf(k);
    if (bi < 0) return 7;
    const BankEntry& e = s_bank.e[bi];
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
    const IndexEntry& ix = s_sidx[k];
    if (x->stored != ix.synthStored || x->patch_engine != ix.synthPatch || x->type_id != ix.synthType ||
        strncmp(x->name, s_lib[k].name, sizeof(s_lib[k].name)) != 0)
        return 8;
    if (x->work_bytes)
    {   // the engine's working RAM (pools, delay lines), SDRAM arena, zeroed at every attach, the block stays the entry's
        const uint32_t need = (x->work_bytes + 7u) & ~7u;
        if (!x->set_work) return 5;
        if (!s_lib[k].work || s_lib[k].workLen < need)
        {
            if (need > kWorkBytes - s_workUsed) return 5;
            s_lib[k].work = s_work + s_workUsed;
            s_lib[k].workLen = need;
            s_workUsed += need;
        }
        memset(s_lib[k].work, 0, need);
        x->set_work(s_lib[k].work, x->work_bytes);
    }
    EngineDef& def = s_defs[k];
    def.params = reinterpret_cast<const polysynth::open::ParamDef*>(x->params);
    def.paramCount = int(x->param_count);
    def.sections = reinterpret_cast<const polysynth::open::SectionDef*>(x->sections);
    def.sectionCount = int(x->section_count);
    def.macros = reinterpret_cast<const polysynth::open::MacroDef*>(x->macros);
    def.macroCount = int(x->macro_count);
    def.volumeParam = x->volume_param;
    def.maxVoices = int(x->max_voices);
    def.slotFx = (x->flags & PLUG_SYNTH_SLOT_FX) && x->slot_post;
    DISABLE_AUDIO_INTERRUPTS();
    eng(k).x = x;
    def.inst = &eng(k);
    ENABLE_AUDIO_INTERRUPTS();
    return 0;
}

// Before the engine's flash or rw bytes may change: no part may reach it any more, nothing runs from it
void detach(int k)
{
    if (!s_lib[k].attached) return;
    DISABLE_AUDIO_INTERRUPTS();
    polysynth::open::engineGone(&s_defs[k]);   // the parts on it: voices cut, no engine (silent until they get a new patch)
    eng(k).x = nullptr;
    placeholder(k);
    ENABLE_AUDIO_INTERRUPTS();
    s_lib[k].attached = false;
}

// Attach with the crash marker around it (plug_host.cpp boot() reads it): a crash in an engine's entry or init means safe mode
// at the next boot
int attachMarked(int k)
{
    const bool had = s_sfs->exists(kMarker);   // an insert's own marker (plug_host.cpp) stays as it is
    if (!had) writeText(kMarker, "synth engines", 13);
    const int r = attach(k);
    if (!had) s_sfs->remove(kMarker);
    if (r) placeholder(k);
    s_lib[k].attached = r == 0;
    return r;
}

// ------------------------------------------------------------------------------------------------ status
void status()
{
    char* t = s_btext;
    const int cap = int(sizeof(s_btext));
    uint32_t sectors = 0, rw = 0, wearMax = 0, wearSum = 0;
    for (int i = 0; i < s_bank.n; i++)
    {
        sectors += uint32_t(sectorEnd(s_bank.e[i]) - sectorFirst(s_bank.e[i]));
        rw += s_bank.e[i].rwLen;
    }
    for (int s = 0; s < kBankSectors; s++)
    {
        wearMax = s_bank.wear[s] > wearMax ? s_bank.wear[s] : wearMax;
        wearSum += s_bank.wear[s];
    }
    int listed = 0, attached = 0;
    for (int k = 0; k < s_sn; k++)
    {
        listed += s_lib[k].listed;
        attached += s_lib[k].attached;
    }
    int len = snprintf(t, size_t(cap),
                       "synth library: files %d listed %d installed %d attached %d%s, boot dropped %d stale %d\r\n"
                       "synth bank: sectors %lu of %d, rw pool %lu of %lu B, flash commands since boot %lu\r\n",
                       s_sscan.listed, listed, s_bank.n, attached, s_safe ? " (safe mode: crash marker)" : "", s_open.dropped,
                       s_open.stale, (unsigned long)sectors, kBankSectors, (unsigned long)rw, (unsigned long)kPoolBytes,
                       (unsigned long)nvmCommands());
    for (int k = 0; k < s_sn && len < cap; k++)
    {
        const int bi = bankOf(k);
        len += snprintf(t + len, size_t(cap - len), "%s %s stored %ld %s", s_sidx[k].file, s_lib[k].name, (long)s_sidx[k].synthStored,
                        !s_lib[k].listed ? "not listed" : (s_lib[k].attached ? "attached" : (bi >= 0 ? "installed" : "on card")));
        if (bi >= 0 && len < cap)
            len += snprintf(t + len, size_t(cap - len), " at %lx", (unsigned long)(kBankRun + s_bank.e[bi].off));
        if (len < cap) len += snprintf(t + len, size_t(cap - len), " err %d\r\n", int(s_lib[k].err));
    }
    if (len < cap) len += snprintf(t + len, size_t(cap - len), "wear (erases per sector, max %lu, total %lu):", (unsigned long)wearMax,
                                   (unsigned long)wearSum);
    for (int s = 0; s < kBankSectors && len < cap; s++) len += snprintf(t + len, size_t(cap - len), " %lu", (unsigned long)s_bank.wear[s]);
    if (len < cap) len += snprintf(t + len, size_t(cap - len), "\r\n");
    for (int i = 0; i < s_logN && i < 8 && len < cap; i++)
    {
        const Install& x = s_log[(s_logN - 1 - i) % 8];
        len += snprintf(t + len, size_t(cap - len), "install %s err %d ms %lu (in flash commands %lu) commands %d erases %d evicted %d\r\n",
                        x.file, x.err, (unsigned long)x.ms, (unsigned long)x.cmdMs, x.commands, x.erases, x.evicted);
    }
    static const char* const kSrc[3] = {"text", "sidecar", "trailer"};
    for (int k = 0; k < s_sn && s_uiUp && len < cap; k++)
        if (s_lib[k].listed)
            len += snprintf(t + len, size_t(cap - len), "logo %s %s err %d\r\n", s_sidx[k].file, kSrc[s_lib[k].logoSrc], int(s_lib[k].logoErr));
    if (s_uiUp && len < cap)
        len += snprintf(t + len, size_t(cap - len), "logos %d of %d, RAM_G %lu of %lu B\r\n", s_logos, kLogoSlots,
                        (unsigned long)(s_logos * kLogoPixels), (unsigned long)(kLogoRamGEnd - kLogoRamG));
    writeText(kStatusPath, t, len < cap ? len : cap - 1);
}

bool contains(const int* v, int n, int x)
{
    for (int i = 0; i < n; i++)
        if (v[i] == x) return true;
    return false;
}

// Installs (when needed) and attaches library entry k. want / keep: as synthEnsure
bool loadEngine(int k, const int* want, int nWant, const int* keep, int nKeep)
{
    if (s_safe)
    {
        s_lib[k].err = 7;
        return false;
    }
    if (bankOf(k) < 0)
    {
        const IndexEntry& x = s_sidx[k];
        BankWant w = {};
        w.uid = x.uid;
        w.version = x.version;
        w.fileCrc = x.crc;
        w.text = x.rwOffset ? x.rwOffset : x.imageSize;
        w.len = x.imageSize;
        w.rwLen = x.imageSize - w.text + x.bssSize;
        w.entry = x.entry;
        snprintf(w.file, sizeof(w.file), "%s", x.file);
        plugUiSynthLoading(s_lib[k].name);
        auto rank = [&](const BankEntry& e) -> long {
            const int j = libOf(e);
            if (j < 0) return 0;   // not in the library any more: first to go
            const int ty = s_defs[j].typeId;
            if (contains(want, nWant, ty)) return -1;
            return long(s_lib[j].used) + (contains(keep, nKeep, ty) ? (1L << 30) : 0L);
        };
        auto onEvict = [&](const BankEntry& e) {
            const int j = libOf(e);
            if (j >= 0) detach(j);
        };
        NvmFlash f;
        SdIo io;
        const uint32_t ms0 = systick_millis_count;
        const uint64_t cy0 = nvmCommandCycles();
        const BankInstallStatus st = bankInstall(f, io, w, rank, onEvict, uintptr_t(s_pool), kPoolBytes, s_bank, s_sec, s_itext,
                                                 int(sizeof(s_itext)), s_bankWork);
        // systick stops inside a flash command (interrupts off): the command time is counted from the DWT (90 MHz there)
        const uint32_t cmdMs = uint32_t((nvmCommandCycles() - cy0) / 90000u);
        Install& l = s_log[s_logN++ % 8];
        snprintf(l.file, sizeof(l.file), "%s", x.file);
        l.err = st.err;
        l.evicted = st.evicted;
        l.commands = st.commands;
        l.erases = st.erases;
        l.cmdMs = cmdMs;
        l.ms = (systick_millis_count - ms0) + cmdMs;
        if (st.err)
        {
            s_lib[k].err = int8_t(10 + st.err);
            status();
            return false;
        }
    }
    const int r = attachMarked(k);
    s_lib[k].err = int8_t(r);
    status();
    return r == 0;
}

}   // namespace

void synthBoot(FileSystem* fs)
{
    s_sfs = fs;
    useFs(fs);
    memset(&s_bank, 0, sizeof(s_bank));
    memset(s_lib, 0, sizeof(s_lib));
    const int n = scanDir(kSynthDir, s_sidx, kLibMax, s_sscan, PLUG_KIND_SYNTH);
    s_sn = n;
    const bool hadIndex = fs->exists(kIndexPath);
    if (!n && !hadIndex) return;   // no synth plugin was ever installed: nothing to do, nothing written
    s_safe = fs->exists(kMarker);  // a marker left by the last session (plug_host.cpp boot() reads it too): no engine runs
    BankWant w[kLibMax] = {};
    for (int k = 0; k < n; k++)
    {
        const IndexEntry& x = s_sidx[k];
        Lib& l = s_lib[k];
        w[k].uid = x.uid;
        w[k].fileCrc = x.crc;
        snprintf(l.name, sizeof(l.name), "%s", x.name);
        new (s_eng[k]) PlugSynth(x.synthType, l.name);
        EngineDef& d = s_defs[k];
        d.name = l.name;
        d.typeId = x.synthType;
        d.engine = nullptr;
        placeholder(k);
        if (x.synthStored < 0)
            l.err = 9;   // built before round E: no numbers, rebuild with build_plug.py --synth
        else if (!l.name[0] || strnlen(x.name, 9) > 8 || strchr(l.name, '/') ||
                 !polysynth::open::engineListAdd(&d, x.synthStored, x.synthPatch))
            l.err = 6;   // a name the shim cannot show or keep a folder of, or numbers a built-in or earlier entry has
        else
            l.listed = 1;
    }
    NvmFlash f;
    SdIo io;
    s_open = bankOpen(f, io, w, n, uintptr_t(s_pool), kPoolBytes, s_bank, s_itext, int(sizeof(s_itext)));
    status();
}

bool synthEnsure(const int* want, int nWant, const int* keep, int nKeep)
{
    bool ok = true;
    for (int i = 0; i < nWant; i++)
    {
        const int k = libForType(want[i]);
        if (k < 0) continue;   // built in, or not a library engine
        s_lib[k].used = ++s_tick;
        if (!s_lib[k].attached && !loadEngine(k, want, nWant, keep, nKeep)) ok = false;
    }
    return ok;
}

bool synthLoaded(int typeId)
{
    const int k = libForType(typeId);
    return k >= 0 && s_lib[k].attached;
}

bool synthIsPlugin(int typeId)
{
    return libForType(typeId) >= 0;
}

bool synthUiUp()
{
    return s_uiUp;
}

namespace
{
// FileSystem for logoFind (plug_logo.h)
struct LogoFs
{
    int readAt(const char* p, uint32_t off, void* b, int cap)
    {
        auto [ok, f] = s_sfs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok) return -1;
        const int n = !off || s_sfs->seekFile(off, f) ? int(s_sfs->readFile(b, size_t(cap), f)) : -1;
        s_sfs->closeFile(f);
        return n;
    }
};
}   // namespace

void synthLogos(DisplayDriver* dd)
{
    s_logos = 0;
    if (!s_sfs || !dd) return;
    s_uiUp = true;
    if (!s_sn) return;
    LogoFs fs;
    for (int k = 0; k < s_sn; k++)
    {
        Lib& l = s_lib[k];
        l.logo = 0;
        l.logoSrc = kLogoNone;
        if (!l.listed) continue;
        LogoErr why = kLogoOk;
        const LogoSource src = s_logos < kLogoSlots ? logoFind(fs, kSynthDir, s_sidx[k].file, s_sidx[k].logoOff, s_sec, &why) : kLogoNone;
        if (src != kLogoNone)
        {
            dd->writeDataRAMG(s_sec + kLogoHeader, kLogoPixels, kLogoRamG + uint32_t(s_logos) * kLogoPixels);
            l.logo = uint8_t(++s_logos);
        }
        l.logoSrc = int8_t(src);
        l.logoErr = int8_t(why);
    }
    status();
}

int synthLogoBitmap(const polysynth::open::EngineDef* d, bool editor)
{
    for (int k = 0; k < s_sn; k++)
        if (&s_defs[k] == d && s_lib[k].logo) return kPlugLogoFirst + 2 * (s_lib[k].logo - 1) + (editor ? 1 : 0);
    return -1;
}

}   // namespace plug

const strBitmap* bitmapAt(int id)
{
    using namespace plug;
    if (id >= 0 && id < displayBitmapsCount) return &bitmaps[id];
    const int s = (id - kPlugLogoFirst) >> 1;
    if (id < kPlugLogoFirst || s >= s_logos) return nullptr;
    // slot s: the list image, then the editor image (L2 = 17, FT81x.h)
    static const strBitmap kTpl[2] = {{nullptr, kLogoRamG, kLogoMiniBytes, kLogoMiniW, kLogoMiniH, 17, kLogoMiniStride},
                                      {nullptr, kLogoRamG + kLogoMiniBytes, kLogoMedBytes, kLogoMedW, kLogoMedH, 17, kLogoMedStride}};
    static strBitmap b;
    b = kTpl[(id - kPlugLogoFirst) & 1];
    b.address += uint32_t(s) * kLogoPixels;
    return &b;
}

#endif   // FEATURE_PLUG_HOST
