// Runtime plugin host, master insert host v1. See plug_host.h.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "plug_defer.h"
#include "log.h"
#include "plug_sidecar_io.h"
#include "mtSequencer.h"
#include "mtFollowChord.h"
#ifdef FEATURE_PARTS
#include "parts.h"
#endif
#include "plug_mtff.h"
#ifdef FEATURE_SYNTH_OPEN
#include "open_patch.h"
#endif
#include "fx34_io.h"
#include "strum_depth.h"

#include "FileSystemAdapter.h"
#include "Platform.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"
#include "scales.h"
#include "cr_section_macros.h"
#include "interruptsControl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char* const saturationListTexts[kSaturationTypeEnd];   // gMasterParams.cpp
static_assert(plug::kSatSoftClip == SaturationType::kSoftClip, "mixer stage values are saturationType values");
#ifdef FEATURE_SAT_SOFTKNEE
static_assert(plug::kSatSoftKnee == SaturationType::kSoftKnee, "mixer stage values are saturationType values");
#endif
#ifdef FEATURE_FF_DIRT
static_assert(plug::kSatDirtFirst == SaturationType::kDirtSoft && plug::kSatDirtLast == SaturationType::kDirtCrush, "Dirt values");
#endif

namespace plug
{

namespace
{
// ------------------------------------------------------------------------------------------------ memory homes
// SRAM_L, NOLOAD: .dmabuffers is the first RAM output section of mk66fx1m0.ld after the USB descriptor table. Both inserts run
// from it, insert 1 from the bottom and insert 2 from the top (plug_index.h insertPlace): no flash command for either
__attribute__((section(".dmabuffers.plugslot"), aligned(16))) uint8_t s_slot[kSlotBytes];
static_assert(kSlotBytes == kInsertRoom, "the pair rule of plug_index.h is the slot");

constexpr uint32_t kArenaPerSlot = 32 * 1024;   // SDRAM for plugin delay lines and tables (alloc flag 1), per slot
// SDRAM globals are never constructed (no static constructor may touch the SDRAM, f563f27): plain arrays and PODs only,
// boot() clears what needs clearing
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_arena[kSlots][kArenaPerSlot];
__NOINIT(EXTERNAL_RAM) Sidecar s_side;                                            // the project's references and values
__NOINIT(EXTERNAL_RAM) char s_text[kSidecarMax];                                  // sidecar file text
// sidecar paths (plug_sidecar_io.h kPathMax): the two a hook works on and the scratch the replace builds <path>.tmp in. Off the stack,
// which the audio interrupt shares. The hooks run in the main loop only and never nest
__NOINIT(EXTERNAL_RAM) char s_pathA[kPathMax];
__NOINIT(EXTERNAL_RAM) char s_pathB[kPathMax];
__NOINIT(EXTERNAL_RAM) char s_pathScratch[kPathBuf];
// project.mtff (plug_mtff.h): the file as read or written (unknown lines are kept from it), what it holds, what the load found
__NOINIT(EXTERNAL_RAM) char s_mtffRaw[kMtffMax];
__NOINIT(EXTERNAL_RAM) char s_mtffTmp[kMtffMax];   // the new text of a save, the copy buffer of open and save, the park scratch
int s_mtffRawN;
Mtff s_mtff;
SidecarState s_mtffState;
bool s_mtffOpenStale;   // the last project open could not copy the project's project.mtff to the Workspace
MtStamp s_stamp;        // the project.mt last read or written (projectStamp)
const char* s_buildId;   // BUILD_ID of the running image (the git sha7), kept by boot for the header of project.mtff
bool s_mtffUserOpen;     // sidecarOpen ran: the next project load is a user's open (File screen, USB console), not a Workspace reload or autosave
uint8_t s_mtffNotice;    // what that open found for the screen (plug_host.h takeOpenNotice): 0 none, 1 a newer writer of this format, 2 a newer format
uint32_t s_mtffNoticeFf; // writer of the file, packed as mtffWriterVersion
uint16_t s_mtffNoticeFormat;
char s_mtffNote[160];   // the one status line of the last load: parked lines, a file of a newer format, a card error (host.txt)
SidecarState s_sideState;   // what the project load found (plug_sidecar_io.h): a card error blocks every write of the file
__NOINIT(EXTERNAL_RAM) IndexEntry s_index[kIndexMax];
__NOINIT(EXTERNAL_RAM) char s_names[kScanMax * 40];
__NOINIT(EXTERNAL_RAM) char* s_nameList[kScanMax];
__NOINIT(EXTERNAL_RAM) ScanStats s_scan;
ScanCache s_scanCache;   // SRAM .bss, zero at boot: the first rescan() reads every file (plug_index.h)
bool s_staleRetry;       // one reload after a kStale install (the forced rescan made the index match the folder)

constexpr const char* kDir = "/Plugins/Master";
constexpr uint32_t kBudget = 60000;         // cycles per block and slot: 11.5 % of a 522,240 cycle block, then bypass
constexpr uint32_t kKeyWindowMs = 4000;     // hold Shift this long after power on: no plugin loads this session
constexpr uint32_t kDebounceMs = 400;       // a UI pick loads once the selection rests this long
constexpr uint32_t kMarkerMs = 3000;        // clean running after a load before the crash marker goes (a power off
                                            // inside this window makes the next boot a safe one, harmless)

struct Slot
{
    const plug_desc* desc;
    const plug_ext_tempo* tempo;
    const plug_ext_tail* tail;
    void* inst;
    void* heap[4];
    uint8_t nHeap;
    uint32_t arenaUsed;
    SlotState state;
    Err err;
    bool pending, manual;
    float val[kMaxParams];
};
Slot s_slots[kSlots];
InsertPair s_pair;   // who holds which bytes of s_slot, and a refused insert's size (plug_index.h)
int s_allocSlot = -1;
int s_count;
FileSystem* s_fs;
bool s_safe, s_windowOpen, s_on;
char s_reason[48];
char s_note[96];   // what the last project load changed (a Dirt entry migrated), host.txt line 3
Timer* s_timer;
uint32_t s_bootMs, s_pickMs, s_markerMs;
OpenGate s_openGate;   // plug_defer.h: the loads that come from a project load wait their turn
bool s_markerSet;
uint32_t now()
{
    return s_timer ? s_timer->millis() : 0;
}

void* hostAlloc(uint32_t bytes, uint32_t flags)
{
    if (s_allocSlot < 0) return nullptr;   // init only
    Slot& s = s_slots[s_allocSlot];
    bytes = (bytes + 7u) & ~7u;
    if (flags & 1u)
    {
        if (bytes > kArenaPerSlot - s.arenaUsed) return nullptr;
        void* p = s_arena[s_allocSlot] + s.arenaUsed;
        s.arenaUsed += bytes;
        memset(p, 0, bytes);
        return p;
    }
    if (s.nHeap >= 4) return nullptr;
    void* p = malloc(bytes);   // SRAM heap, may fail (never an exception on MK66)
    if (!p) return nullptr;
    memset(p, 0, bytes);
    s.heap[s.nHeap++] = p;
    return p;
}

uint32_t hostCycles()
{
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004);   // DWT_CYCCNT
}

// host level 2: PLUG_EXT_FILES (plug_synth.cpp), PLUG_EXT_STRUM (the project's Strum ramp depth, live), NULL for every other id
const void* hostQuery(uint32_t id)
{
    if (id == PLUG_EXT_FILES) return filesQuery();
    return id == PLUG_EXT_STRUM ? &strumramp::g_depth : nullptr;   // strum depth: plug_abi.h
}

float hSin(float x) { return sinf(x); }
float hCos(float x) { return cosf(x); }
float hTan(float x) { return tanf(x); }
float hTanh(float x) { return tanhf(x); }
float hExp(float x) { return expf(x); }
float hLog(float x) { return logf(x); }
float hPow(float x, float y) { return powf(x, y); }
float hExp2(float x) { return exp2f(x); }
float hLog2(float x) { return log2f(x); }

const plug_host s_host = {
    sizeof(plug_host), PLUG_HOST_LEVEL, 44117.64706f, hSin, hCos, hTan, hTanh, hExp, hLog, hPow, memcpy, memset,
    hostAlloc,         hostCycles,      hostQuery,    hExp2, hLog2,
};

bool readAll(FileSystem* fs, FileSystem::FileHandle f, uint8_t* dst, uint32_t n)
{
    while (n)
    {
        const uint32_t chunk = n > 256 ? 256 : n;   // through FatFs' own sector buffer, as PtfxLoader does
        if (fs->readFile(dst, chunk, f) != int32_t(chunk)) return false;
        dst += chunk;
        n -= chunk;
    }
    return true;
}

inline bool inImage(uintptr_t p, uintptr_t base, uint32_t size)
{
    return size == 0 || (p >= base && p < base + size);
}

void path(char* out, int cap, const char* dir, const char* name)
{
    snprintf(out, size_t(cap), "%s/%s", dir, name);
}

}   // namespace

void useFs(FileSystem* fs)
{
    s_fs = fs;
}

namespace
{
// plug_sidecar_io.h's file operations on the SD card
struct SdOps
{
    char* pathScratch() { return s_pathScratch; }
    Presence stat(const char* p) { return FileSystemAdapter::statPath(p); }
    bool remove(const char* p) { return s_fs->remove(p); }
    bool rename(const char* from, const char* to) { return s_fs->rename(from, to); }
    int readFile(const char* p, char* buf, int cap)
    {
        auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok) return -1;
        int n = int(s_fs->readFile(buf, size_t(cap), f));
        char more;
        if (n == cap && s_fs->readFile(&more, 1, f) != 0) n = -1;   // larger than cap (or a read error): not this file
        s_fs->closeFile(f);
        return n;
    }
    // FatFs reports a failed flush only to f_close, which the platform's closeFile does not return: the bytes are read back
    bool writeNew(const char* p, const char* buf, int n)
    {
        auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kWriteCreateNew);
        if (!ok) return false;
        const bool wrote = s_fs->writeFile(buf, size_t(n), f) == n;
        s_fs->closeFile(f);
        if (!wrote) return false;
        auto [rok, r] = s_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!rok) return false;
        bool same = true;
        char chunk[64];   // the main stack, which the audio interrupt shares: small
        for (int at = 0; same && at < n; at += int(sizeof(chunk)))
        {
            const int want = n - at < int(sizeof(chunk)) ? n - at : int(sizeof(chunk));
            same = s_fs->readFile(chunk, size_t(want), r) == want && memcmp(chunk, buf + at, size_t(want)) == 0;
        }
        same = same && s_fs->readFile(chunk, 1, r) == 0;
        s_fs->closeFile(r);
        return same;
    }
};
}   // namespace

// <path>.tmp, read back, then replaceFileOps (open_fileops.h): the old file stays, as path or as <path>.bak, until the new one
// is in place. A power cut between the two renames leaves only <path>.bak: readText (and the sidecar load) moves it back.
bool writeText(const char* p, const char* text, int n)
{
    if (!s_fs) return false;
    SdOps ops;
    return sidecarReplace(ops, p, text, n);
}

int readText(const char* p, char* buf, int cap)
{
    if (!s_fs) return -1;
    SdOps ops;
    int n = 0;
    return sidecarFetch(ops, p, buf, cap, n) == Presence::kPresent ? n : -1;
}

namespace
{

// /Plugins/host.txt: what each slot holds, after every load and on every Workspace save (the device test reads it back)
// host.txt text off the stack (a local gave writeStatus an 896 B frame on the project open and save paths, see
// mainLoopScratch.h for the main loop's room). Every caller (apply, rescan, projectLoaded, projectSave) is main loop only.
// Room for the head and slot lines (about 700 B) and a "plugin" line per index entry (at most 96 B: name 15, file 39)
constexpr int kStatusMax = 1024 + kIndexMax * 96;
__NOINIT(EXTERNAL_RAM) char s_statusText[kStatusMax];

void writeStatus()
{
    char (&t)[kStatusMax] = s_statusText;
    int n = snprintf(t, sizeof(t), "plugin host 1, index %d, inserts %s, safe %d %s, nvm commands %lu\r\n", s_count,
                     s_on ? "on" : "bypassed", int(s_safe), s_reason, (unsigned long)nvmCommands());
    n += snprintf(t + n, sizeof(t) - size_t(n), "scan %s: listed %d, rejected %d\r\n", kDir, s_scan.listed, s_scan.rejected);
    n += snprintf(t + n, sizeof(t) - size_t(n), "mixer %s%s%s\r\n", saturationListTexts[mixerStage()], s_note[0] ? ", " : "", s_note);
    n += snprintf(t + n, sizeof(t) - size_t(n), "project.mtff: strum ramp %d %%, state %d%s%s\r\n", strumramp::depthPct(), int(s_mtffState),
                  s_mtffNote[0] ? ", " : "", s_mtffNote);
    for (int i = 0; i < s_scan.rejected && i < 3 && n < int(sizeof(t)); i++)
        n += snprintf(t + n, sizeof(t) - size_t(n), "rejected %s err %ld\r\n", s_scan.reject[i].file, long(s_scan.reject[i].err));
    for (int i = 0; i < s_count && n < int(sizeof(t)); i++)
        n += snprintf(t + n, sizeof(t) - size_t(n), "plugin %s uid %08lx v %lu file %s\r\n", s_index[i].name,
                      (unsigned long)s_index[i].uid, (unsigned long)s_index[i].version, s_index[i].file);
    for (int k = 0; k < kSlots && n < int(sizeof(t)); k++)
    {
        const Slot& s = s_slots[k];
        const SlotRef& r = s_side.slot[k];
        n += snprintf(t + n, sizeof(t) - size_t(n), "insert %d: %s uid %08lx state %d err %ld text 0x%08lx inst 0x%08lx cycles %lu max %lu\r\n",
                      k + 1, r.uid ? r.name : "-", (unsigned long)r.uid, int(slotState(k)), long(s.err),
                      (unsigned long)(s.desc ? uintptr_t(s.desc->process) & ~1u : 0), (unsigned long)uintptr_t(s.inst),
                      (unsigned long)slotCycles(k), (unsigned long)slotMaxCycles(k));
        if (s.state == SlotState::kNoRoom && n < int(sizeof(t)))
            n += snprintf(t + n, sizeof(t) - size_t(n),
                          "insert %d: %s needs %lu B, %lu B free beside insert %d %s (both inserts share the %lu B SRAM slot)\r\n",
                          k + 1, r.name, (unsigned long)s_pair.want[k],
                          (unsigned long)(kSlotBytes - s_pair.foot[1 - k]), 2 - k,
                          s_side.slot[1 - k].uid ? s_side.slot[1 - k].name : "-", (unsigned long)kSlotBytes);
    }
    if (n < int(sizeof(t)))
        n += snprintf(t + n, sizeof(t) - size_t(n), "slot %lu B: insert 1 %lu B, insert 2 %lu B, no flash\r\n", (unsigned long)kSlotBytes,
                      (unsigned long)s_pair.foot[0], (unsigned long)s_pair.foot[1]);
    if (n >= int(sizeof(t))) n = int(sizeof(t)) - 1;
    writeText("/Plugins/host.txt", t, n);
}

// ------------------------------------------------------------------------------------------------ slots
void unload(int k)
{
    Slot& s = s_slots[k];
    if (plugInsert()) plugInsert()->attach(k, nullptr, nullptr, 0);   // audio interrupt off inside: the node lets go first
    plugUiSlotGone(k);   // and the Inserts page: its lists hold the plugin's enum names, the next load overwrites them
    for (uint8_t i = 0; i < s.nHeap; i++) free(s.heap[i]);
    s.nHeap = 0;
    s.arenaUsed = 0;
    s_pair.freed(k);   // its part of the slot is free (detached above, the audio interrupt no longer runs it)
    s_pair.clearRefusal(k);
    s.desc = nullptr;
    s.tempo = nullptr;
    s.tail = nullptr;
    s.inst = nullptr;
    s.err = kOk;
}

// Slot k on the node when the inserts are on, off it when bypassed. A slot that goes on gets every value first: the plugin is
// not running here (detached), and values set while it was bypassed only reached the host's copy.
void connect(int k)
{
    Slot& s = s_slots[k];
    if (!plugInsert() || !s.desc) return;
    if (!s_on)
    {
        plugInsert()->attach(k, nullptr, nullptr, 0);
        return;
    }
    if (s.desc->set_param)
        for (uint32_t i = 0; i < s.desc->num_params; i++) s.desc->set_param(s.inst, i, s.val[i]);
    plugInsert()->attach(k, s.desc, s.inst, kBudget, s.tempo, s.tail);
}

// Loads what s_side.slot[k] names into slot k now. Main thread.
void apply(int k)
{
    Slot& s = s_slots[k];
    const SlotRef& ref = s_side.slot[k];
    s.pending = false;
    unload(k);
    if (!ref.uid)
    {
        s.state = SlotState::kEmpty;
        return;
    }
    const int ix = indexFind(ref.uid);
    if (ix < 0)
    {
        s.state = SlotState::kMissing;
        LOGEF("plg%d %s missing", k + 1, ref.name);
        return;
    }
    if (s_safe && !s.manual)
    {
        s.state = SlotState::kSafe;
        LOGEF("plg%d %s safe", k + 1, ref.name);
        return;
    }
    const IndexEntry& e = s_index[ix];
    // Both inserts share the SRAM_L slot (plug_index.h insertPlace): insert 1 from the bottom, insert 2 from the top, image and bss
    // in one piece. The other insert keeps playing from its own bytes, which this load never writes. A plugin that fits the slot
    // alone but not beside the other one is refused here, before any card read, and tick() loads it once the other one leaves room.
    // There is no other home: no insert ever writes the flash.
    const uint32_t foot = insertFoot(e.imageSize, e.bssSize);
    uint32_t at = 0, room = 0;
    Err err = kOk;
    if (foot > kSlotBytes)
        err = kTooBig;   // not even alone
    else if (!s_pair.place(k, foot, at, room))
    {
        s.err = kNoRoom;
        s.state = SlotState::kNoRoom;
        LOGEF("plg%d %s no room %lu", k + 1, ref.name, (unsigned long)foot);
        writeStatus();
        return;
    }
    char p[64];
    path(p, sizeof(p), kDir, e.file);
    Place pl;   // text, rw and bss from slot offset at, at most room bytes (load() checks the file's own sizes against room)
    pl.textDst = s_slot + at;
    pl.textLen = room;
    pl.textRun = uintptr_t(s_slot + at);
    // crash marker first: a plugin that hangs init or its first block leaves it behind, the next boot runs in safe mode
    writeText(kMarker, ref.name, int(strlen(ref.name)));
    s_markerSet = true;
    s_markerMs = now();
    pl.wantUid = e.uid;   // only the file the index listed: a stale list never installs a changed or replaced file
    pl.wantCrc = e.crc;
    Loaded l;
    if (err == kOk) err = load(s_fs, p, pl, PLUG_KIND_MASTER_INSERT, l);
    if (err == kStale && !s_staleRetry)
    {   // the folder changed under the index: read it again and load once more from what it lists now (tick, no debounce)
        s_fs->remove(kMarker);
        s_markerSet = false;
        s_staleRetry = true;
        s_scanCache.invalidate();
        rescan();
        s.pending = true;   // manual stays: a pick in safe mode still loads
        LOGEF("plg%d %s stale, rescanned", k + 1, ref.name);
        return;
    }
    s_staleRetry = false;
    if (err == kOk)
    {
        s_allocSlot = k;
        s.inst = hostAlloc(l.desc->instance_size ? l.desc->instance_size : 8, 0);
        if (!s.inst || l.desc->init(s.inst, &s_host) != 0) err = kInit;
        s_allocSlot = -1;
    }
    if (err != kOk)
    {
        unload(k);
        s.err = err;
        s.state = SlotState::kFailed;
        LOGEF("plg%d %s err %d", k + 1, ref.name, int(err));
        writeStatus();
        return;
    }
    s.desc = l.desc;
    s.tempo = l.tempo;
    s.tail = l.tail;
    // insert 2 holds [at, end of slot), whatever the file's own size is (load() kept it inside room)
    s_pair.loaded(k, at, insertFoot(l.hdr));
    // the project's values by name, defaults for the rest; init left the plugin at its defaults
    for (uint32_t i = 0; i < l.desc->num_params; i++)
    {
        const plug_param& pp = l.desc->params[i];
        float v = pp.def;
        sidecarValue(ref, pp.name, v);
        v = v < pp.min ? pp.min : (v > pp.max ? pp.max : v);
        s.val[i] = v;
        if (v != pp.def && l.desc->set_param) l.desc->set_param(s.inst, i, v);
    }
    s.state = SlotState::kRunning;
    LOGEF("plg%d %s ok", k + 1, ref.name);
    if (s_on && plugInsert()) plugInsert()->attach(k, s.desc, s.inst, kBudget, s.tempo, s.tail);   // bypassed: loaded, off the node
    writeStatus();
}

// The sidecar entry of slot k from the running plugin's values (a missing or failed plugin keeps what was read)
void syncRef(int k)
{
    const Slot& s = s_slots[k];
    SlotRef& r = s_side.slot[k];
    if (!s.desc || !r.uid) return;
    r.count = uint8_t(s.desc->num_params);
    for (uint32_t i = 0; i < s.desc->num_params; i++)
    {
        copyName(r.pname[i], s.desc->params[i].name, kNameLen);
        r.pval[i] = s.val[i];
    }
}

void enterSafe(const char* why)
{
    s_safe = true;
    snprintf(s_reason, sizeof(s_reason), "%s", why);
    LOGE(s_reason);
    for (int k = 0; k < kSlots; k++)
        if (s_slots[k].state == SlotState::kRunning || s_slots[k].pending)
        {
            unload(k);
            s_slots[k].pending = false;
            s_slots[k].state = SlotState::kSafe;
        }
}
}   // namespace

// ------------------------------------------------------------------------------------------------ loader
uint8_t* sramSlot()
{
    return s_slot;
}

const plug_host* hostTable()
{
    return &s_host;
}

void syncCode()
{
    asm volatile("dsb 0xf" ::: "memory");
    // LMEM_PCCCR: GO | INVW1 | INVW0 | ENWRBUF | ENCACHE, as the Teensy startup writes it
    volatile uint32_t* pcccr = reinterpret_cast<volatile uint32_t*>(0xE0082000);
    *pcccr = 0x85000003u;
    while (*pcccr & 0x80000000u) {}
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
}

Err checkDesc(const plug_desc* d, uintptr_t base, uint32_t size, uint32_t wantKind)
{
    if (!d || !inImage(uintptr_t(d), base, size)) return kDesc;
    if (d->struct_size < offsetof(plug_desc, query) || d->kind != wantKind) return kDesc;
    const uintptr_t fns[] = {uintptr_t(d->init), uintptr_t(d->process)};
    for (uintptr_t f : fns)
        if (!f || !(f & 1u) || !inImage(f & ~1u, base, size)) return kDesc;
    if (d->set_param && !inImage(uintptr_t(d->set_param) & ~1u, base, size)) return kDesc;
    if (d->num_params > uint32_t(kMaxParams) ||
        (d->num_params && (!inImage(uintptr_t(d->params), base, size) ||
                           !inImage(uintptr_t(d->params + d->num_params) - 1u, base, size))))
        return kDesc;
    for (uint32_t i = 0; i < d->num_params; i++)
    {
        const plug_param& pp = d->params[i];
        if (!inImage(uintptr_t(pp.name), base, size) || pp.max < pp.min) return kDesc;
        if (pp.unit && !inImage(uintptr_t(pp.unit), base, size)) return kDesc;
        if (pp.enum_count &&
            (!pp.enum_names || !inImage(uintptr_t(pp.enum_names), base, size) ||
             !inImage(uintptr_t(pp.enum_names + pp.enum_count) - 1u, base, size)))
            return kDesc;
        for (uint32_t e = 0; e < pp.enum_count; e++)
            if (!inImage(uintptr_t(pp.enum_names[e]), base, size)) return kDesc;
        // a numeric parameter that names its ends (PLUG_PARAM_NAME_*): the names must be in the image like an enum's
        const uint32_t nn = plug_param_name_count(&pp);
        if (nn && (!pp.enum_names || !inImage(uintptr_t(pp.enum_names), base, size) ||
                   !inImage(uintptr_t(pp.enum_names + nn) - 1u, base, size)))
            return kDesc;
        for (uint32_t e = 0; e < nn; e++)
            if (!inImage(uintptr_t(pp.enum_names[e]), base, size)) return kDesc;
    }
    if (d->instance_size > 16384) return kDesc;
    return kOk;
}

const plug_ext_tempo* tempoExt(const plug_desc* d, uintptr_t base, uint32_t size)
{
    if (!d || d->kind != PLUG_KIND_MASTER_INSERT || !PLUG_HAS(d, plug_desc, query) || !d->query) return nullptr;
    if (!inImage(uintptr_t(d->query) & ~1u, base, size)) return nullptr;
    const plug_ext_tempo* t = static_cast<const plug_ext_tempo*>(d->query(PLUG_EXT_TEMPO));
    if (!t || !inImage(uintptr_t(t), base, size) || t->struct_size < offsetof(plug_ext_tempo, set_transport)) return nullptr;
    if (t->set_tempo && (!(uintptr_t(t->set_tempo) & 1u) || !inImage(uintptr_t(t->set_tempo) & ~1u, base, size))) return nullptr;
    if (PLUG_HAS(t, plug_ext_tempo, set_transport) && t->set_transport &&
        (!(uintptr_t(t->set_transport) & 1u) || !inImage(uintptr_t(t->set_transport) & ~1u, base, size)))
        return nullptr;
    return t;
}

const plug_ext_tail* tailExt(const plug_desc* d, uintptr_t base, uint32_t size)
{
    if (!d || d->kind != PLUG_KIND_MASTER_INSERT || !PLUG_HAS(d, plug_desc, query) || !d->query) return nullptr;
    if (!inImage(uintptr_t(d->query) & ~1u, base, size)) return nullptr;
    const plug_ext_tail* t = static_cast<const plug_ext_tail*>(d->query(PLUG_EXT_TAIL));
    if (!t || !inImage(uintptr_t(t), base, size) || !PLUG_HAS(t, plug_ext_tail, quiet) || !t->quiet) return nullptr;
    if (!(uintptr_t(t->quiet) & 1u) || !inImage(uintptr_t(t->quiet) & ~1u, base, size)) return nullptr;
    return t;
}

Err load(FileSystem* fs, const char* path, const Place& pl, uint32_t wantKind, Loaded& out)
{
    out.desc = nullptr;
    out.tempo = nullptr;
    out.tail = nullptr;
    auto [isOpen, f] = fs->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!isOpen) return kNoFile;
    plug_file_header& h = out.hdr;
    Err e = readAll(fs, f, reinterpret_cast<uint8_t*>(&h), sizeof(h)) ? checkHeader(h, wantKind) : kHeader;
    if (e == kOk && pl.wantUid && (h.uid != pl.wantUid || h.crc32 != pl.wantCrc)) e = kStale;   // not the file the index listed
    // text = [0, rw_offset) (code, constants: may run from flash), rw = [rw_offset, image_size) + bss (always RAM)
    const uint32_t rwOff = h.rw_offset ? h.rw_offset : h.image_size;
    uint8_t* const rwDst = pl.rwDst ? pl.rwDst : pl.textDst + rwOff;
    const uintptr_t rwRun = pl.rwRun ? pl.rwRun : pl.textRun + rwOff;
    if (e == kOk) e = checkLayout(h, pl.textLen, pl.rwDst ? pl.rwLen : 0);
    // the CRC covers everything after the 64 byte header: the manifest first
    uint32_t crc = 0;
    for (uint32_t done = sizeof(h); e == kOk && done < h.header_size;)
    {
        uint8_t m[32];
        const uint32_t n = h.header_size - done > sizeof(m) ? sizeof(m) : h.header_size - done;
        if (!readAll(fs, f, m, n)) e = kRead;
        crc = crc32(crc, m, n);
        done += n;
    }
    if (e == kOk && (!readAll(fs, f, pl.textDst, rwOff) || !readAll(fs, f, rwDst, h.image_size - rwOff))) e = kRead;
    if (e != kOk)
    {
        fs->closeFile(f);
        return e;
    }
    crc = crc32(crc32(crc, pl.textDst, rwOff), rwDst, h.image_size - rwOff);
    uint32_t words[32];
    for (uint32_t done = 0; done < h.rebase_count && e == kOk;)
    {
        const uint32_t n = h.rebase_count - done > 32 ? 32 : h.rebase_count - done;
        if (!readAll(fs, f, reinterpret_cast<uint8_t*>(words), n * 4))
        {
            e = kRead;
            break;
        }
        crc = crc32(crc, reinterpret_cast<uint8_t*>(words), n * 4);
        for (uint32_t i = 0; i < n; i++)
        {
            const uint32_t off = words[i];
            if (!rebaseOffsetOk(off, h.image_size))
            {
                e = kRebase;
                break;
            }
            uint32_t* w = reinterpret_cast<uint32_t*>(off < rwOff ? pl.textDst + off : rwDst + (off - rwOff));
            const uint32_t v = *w;
            if (v >= h.image_size + h.bss_size)
            {
                e = kRebase;
                break;
            }
            *w = v < rwOff ? uint32_t(pl.textRun + v) : uint32_t(rwRun + (v - rwOff));
        }
        done += n;
    }
    fs->closeFile(f);
    if (e == kOk && crc != h.crc32) e = kCrc;
    if (e != kOk) return e;
    memset(rwDst + (h.image_size - rwOff), 0, h.bss_size);
    if (!pl.run) return kOk;
    if (pl.commit && !pl.commit(pl.textDst, pl.textRun, rwOff)) return kCommit;   // e.g. program the text into flash
    // the K66 system MPU must let the core fetch from RAM (PtfxLoader does the same)
    *reinterpret_cast<volatile uint32_t*>(0x4000D000) = 0;
    syncCode();
    out.base = pl.textRun;
    const plug_entry_fn entry = reinterpret_cast<plug_entry_fn>((pl.textRun + h.entry_offset) | 1u);
    const plug_desc* d = entry(&s_host, (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
    if (!d) return kEntry;
    // the descriptor, the parameter table and the functions are constants and code: in the text segment
    e = checkDesc(d, pl.textRun, rwOff, wantKind);
    if (e == kOk)
    {
        out.desc = d;
        out.tempo = tempoExt(d, pl.textRun, rwOff);
        out.tail = tailExt(d, pl.textRun, rwOff);
    }
    return e;
}

// ------------------------------------------------------------------------------------------------ index
int indexCount()
{
    return s_count;
}

const IndexEntry* indexAt(int i)
{
    return i >= 0 && i < s_count ? &s_index[i] : nullptr;
}

int indexFind(uint32_t uid)
{
    for (int i = 0; i < s_count; i++)
        if (s_index[i].uid == uid) return i;
    return -1;
}

namespace
{
// FileSystem for scanIndex (plug_index.h)
struct ScanFs
{
    void list(const char* dir, bool (*filter)(const char*), char* names, int namesSize, int maxNameLen, int& count,
              char** list)
    {
        s_fs->listDirContents(dir, FileSystem::kFiles, filter, names, namesSize, maxNameLen, count, list);
    }
    int read(const char* p, void* buf, int cap)
    {
        auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok) return -1;
        const int n = int(s_fs->readFile(buf, size_t(cap), f));
        s_fs->closeFile(f);
        return n;
    }
    // dirSignature (plug_index.h): the platform's single directory walker (nesDir_*, no other user in this firmware), entries
    // with their sizes from the directory itself
    bool dirOpen(const char* dir) { return s_fs->nesDir_open(dir); }
    bool dirNext(char* name, uint32_t& size, bool& skip)
    {
        uint8_t isDir = 0, isHid = 0;
        if (!s_fs->nesDir_readItem(name, &isDir, &isHid, &size)) return false;
        skip = isDir || isHid;
        return true;
    }
    void dirClose() { s_fs->nesDir_close(); }
};
}   // namespace

int scanDir(const char* dir, IndexEntry* index, int cap, ScanStats& st, uint32_t kind)
{
    ScanFs fs;
    return scanIndex(fs, dir, index, cap, s_names, s_nameList, st, kind);
}

// Main loop only (card reads). The signature walk reads the directory entries alone, a scan opens every plugin file: the
// Inserts page opens without a scan when nothing in /Plugins/Master changed
void rescan()
{
    if (!s_fs)
    {
        s_count = 0;
        return;
    }
    ScanFs fs;   // unchanged folder: the index still lists what it holds, no file is opened
    if (refreshIndex(fs, s_scanCache, kDir, s_index, kIndexMax, s_names, s_nameList, s_scan, s_count) && s_scan.listed)
        writeStatus();   // what the scan kept and refused, also when nothing loads
}

void cardChanged()
{
    s_scanCache.invalidate();
}

// ------------------------------------------------------------------------------------------------ slots API
SlotState slotState(int k)
{
    const Slot& s = s_slots[k];
    if (s.pending) return SlotState::kPending;
    if (s.state == SlotState::kRunning && !s_on) return SlotState::kBypassed;
    if (s.state == SlotState::kRunning && plugInsert() && plugInsert()->tripped(k)) return SlotState::kTripped;
    return s.state;
}

Err slotError(int k)
{
    return s_slots[k].err;
}

const char* slotName(int k)
{
    return s_side.slot[k].uid ? s_side.slot[k].name : "";
}

int slotIndex(int k)
{
    return s_side.slot[k].uid ? indexFind(s_side.slot[k].uid) : -1;
}

void assign(int k, int ix)
{
    SlotRef& r = s_side.slot[k];
    const IndexEntry* e = indexAt(ix);
    const SlotState st = slotState(k);
    if (!e && !r.uid && st != SlotState::kPending) return;   // None again (the encoder at the end of the list): no reload
    if (e && r.uid == e->uid && (st == SlotState::kRunning || st == SlotState::kPending)) return;   // a missing, failed,
                                                                                                    // safe or tripped one reloads
    const bool keep = e && r.uid == e->uid;   // the same plugin again: keep its values
    if (keep) syncRef(k);   // a tripped plugin's last values
    if (!keep) memset(&r, 0, sizeof(r));   // a new pick starts at the plugin's defaults
    if (e && !keep)
    {
        r.uid = e->uid;
        r.version = e->version;
        memcpy(r.name, e->name, kNameLen);
    }
    unload(k);   // stop the old one at once, the new one loads when the selection rests (tick)
    s_slots[k].state = SlotState::kEmpty;
    s_slots[k].pending = true;
    s_slots[k].manual = true;
    s_pickMs = now();
}

uint32_t paramCount(int k)
{
    return s_slots[k].desc ? s_slots[k].desc->num_params : 0;
}

const plug_param* param(int k, uint32_t i)
{
    return i < paramCount(k) ? &s_slots[k].desc->params[i] : nullptr;
}

float paramValue(int k, uint32_t i)
{
    return i < paramCount(k) ? s_slots[k].val[i] : 0.0f;
}

void setParam(int k, uint32_t i, float v)
{
    const plug_param* pp = param(k, i);
    if (!pp) return;
    if (pp->step > 0.0f) v = pp->min + pp->step * floorf((v - pp->min) / pp->step + 0.5f);
    v = v < pp->min ? pp->min : (v > pp->max ? pp->max : v);
    s_slots[k].val[i] = v;
    if (plugInsert()) plugInsert()->queue(k, i, v);
}

uint32_t slotCycles(int k)
{
    return s_slots[k].desc && plugInsert() ? plugInsert()->lastCycles(k) : 0;
}

uint32_t slotMaxCycles(int k)
{
    return s_slots[k].desc && plugInsert() ? plugInsert()->maxCycles(k) : 0;
}

uint8_t mixerStage()
{   // validated: the audio engine may ask before boot() cleared s_side (SDRAM, no static init)
    const uint8_t m = mixerStageOf(s_side.mixer);
#ifdef FEATURE_SAT_SOFTKNEE
    return m;
#else
    return m == kSatSoftKnee ? kSatHardClip : m;
#endif
}

void setMixerStage(uint8_t sat)
{
    s_side.mixer = mixerStageOf(sat);
    engine.setSaturationType(SaturationType(mtProject.values.saturationType));   // Inserts: the new stage at once
}

void setInsertsOn(bool on)
{
    if (on == s_on) return;
    s_on = on;
    for (int k = 0; k < kSlots; k++)
        if (s_slots[k].state == SlotState::kRunning && !s_slots[k].pending) connect(k);
}

bool insertsOn()
{
    return s_on;
}

bool safeMode()
{
    return s_safe;
}

const char* safeReason()
{
    return s_reason;
}

// ------------------------------------------------------------------------------------------------ send ducking
const DuckParams& duck()
{
    return s_mtff.duck;
}

void duckSet(const DuckParams& p)
{
    s_mtff.duck = duckClamp(p);
#ifdef FEATURE_SEND_DUCK
    engine.setSendDuck(s_mtff.duck);
#endif
    newFileManager.setProjectStructChanged();
}

// ------------------------------------------------------------------------------------------------ project sidecar
namespace
{
bool s_openStale;   // the last project open could not copy the project's sidecar to the Workspace (sidecarOpen)
}

// ---- project.mtff (plug_mtff.h). The same hooks as plugins.txt, the same file layer (.tmp, read back, .bak swap), the same path rules
namespace
{
// The settings of s_mtff into the sequencer, the audio engine and the Follow chord and Parts state: a load (the project's file, or
// the defaults of a missing one)
void mtffApply()
{
    strumramp::setDepth(s_mtff.depth);
    sequencer.randomSeed = s_mtff.seed;   // seeded random takes: 0 (Off) without a "seed.take" line or a readable file
    static_assert(kSnapTracks == kScaleSnapTracks && sizeof(scaleSnapMode) == sizeof(s_mtff.snap), "scale snap keys and table");
    memcpy(scaleSnapMode, s_mtff.snap, sizeof(scaleSnapMode));   // no file, no snap key, a stale one: every track Off
    sequencer.clearTrackGrooves();
    for (int t = 0; t < kMtffTracks; t++) sequencer.setTrackGroove(uint8_t(t), s_mtff.groove[t]);
    static_assert(followchord::kTracks == kMtffTracks, "follow chord keys");
    for (int t = 0; t < kMtffTracks; t++)
    {
        followchord::setting.mode[t] = s_mtff.followMode[t];
        followchord::setting.source[t] = s_mtff.followSource[t];
    }
    followchord::recalc();
    followchord::resetLive();
#ifdef FEATURE_SEND_DUCK
    engine.setSendDuck(s_mtff.duck.source ? s_mtff.duck : kDuckDefaults);   // no key (or a failed read): ducking off
#endif
    fx34io::applyProjectSwitch(s_mtff.fxSlots == 4);   // FX3 and FX4: off without the key, a file that holds it or a failed read
#ifdef FEATURE_SYNTH_OPEN
    polysynth::open::setProjectMacroMode(s_mtff.macroMode);   // no key: each patch as it always was
#endif
}

// This build as project.mtff names it in the header (plug_mtff.h): the version of mtStructs.h and the git sha of the image. Every load and
// save sets it first (a clear keeps it)
void mtffIdent()
{
    MtffId& id = s_mtff.self;
    id.major = CORE_VER_1;
    id.minor = CORE_VER_2;
    id.patch = CORE_VER_3;
    id.valid = true;
    int b = 0;
    for (const char* q = s_buildId; q && *q && b < kMtffBuildLen; q++)
    {
        const char c = *q;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) break;
        id.build[b++] = c;
    }
    id.build[b] = 0;
}

// The live state of the sequencer and the Follow chord setting into s_mtff (the Parts list is the live one, s_mtff.parts)
void mtffCollect()
{
    mtffIdent();
    s_mtff.depth = uint8_t(strumramp::depthPct());
    s_mtff.seed = sequencer.randomSeed;
    s_mtff.fxSlots = fx34::enabled() ? 4 : 0;
#ifdef FEATURE_SYNTH_OPEN
    s_mtff.macroMode = polysynth::open::projectMacroMode();
#endif
    memcpy(s_mtff.snap, scaleSnapMode, sizeof(s_mtff.snap));   // the Project Settings > Scale snap rows
    for (int t = 0; t < kMtffTracks; t++)
    {
        s_mtff.groove[t] = sequencer.getTrackGroove(uint8_t(t));
        s_mtff.followMode[t] = followchord::setting.mode[t];
        s_mtff.followSource[t] = followchord::setting.source[t];
    }
    s_mtff.hasStamp = s_stamp.valid;
    s_mtff.crc = s_stamp.crc;
    s_mtff.size = s_stamp.size;
}

// Workspace load: the file, the stale check against the project.mt just read, the settings applied (a project without the file or
// the key plays with the ramp off, a new project starts at 100)
void mtffLoaded(const char* dir, bool newProject)
{
    s_mtffNote[0] = 0;
    const bool userOpen = s_mtffUserOpen;   // one open, one notice: a Workspace reload or an autosave never has the flag
    s_mtffUserOpen = false;
    s_mtffNotice = 0;
#ifdef FEATURE_PARTS
    s_mtff.parts = &parts::list();   // the live Parts list: a load fills it in place, a save writes it
#endif
    mtffIdent();
    mtffClear(s_mtff);
    s_mtffRawN = 0;
    if (newProject)
    {   // an empty Workspace: no file. The default is written by the project.mtff save that follows the first project.mt save
        s_mtffOpenStale = false;
        s_mtffState = SidecarState::kNone;
        s_mtff.depth = uint8_t(strumramp::kDefaultNewProject);
        mtffApply();
        return;
    }
    SdOps fs;
    if (sidecarPath(s_pathA, kPathMax, dir, kMtffName))
        s_mtffState = mtffLoadStep(fs, s_pathA, s_mtffRaw, kMtffMax, s_mtff, s_mtffRawN, s_mtffOpenStale, s_stamp, s_mtffTmp, kMtffMax);
    else
    {
        LOGEF("project.mtff path too long: %s", dir);
        s_mtffState = SidecarState::kUnreadable;
        s_mtffOpenStale = false;
    }
#ifdef FEATURE_PARTS
    int blanked = 0;
    uint8_t gone = 0;   // the first pattern found gone, for the status line
    bool more = false;  // and another one
    if (s_mtff.stale && s_mtff.parts)
    {   // project.mt written by another firmware: the Parts apply, a cell whose pattern it deleted is blanked (plug_mtff.h)
        blanked = mtffBlankMissing(
            *s_mtff.parts,
            [](uint8_t pat) {   // s_pathB: free after the load (the main stack stays as it was)
                snprintf(s_pathB, sizeof(s_pathB), cWorkspacePatternFileFormat, int(pat));   // what Parts read (readWorkspacePatternTo)
                return FileSystemAdapter::statPath(s_pathB) != Presence::kAbsent;   // a card error blanks nothing
            },
            [&gone, &more](int part, int track, uint8_t pat) {
                if (!gone) gone = pat;
                more = more || pat != gone;
                LOGEF("Part %d track %d: pattern %u is gone, cell blanked", part + 1, track + 1, unsigned(pat));
            });
    }
#endif
    mtffApply();   // every setting at its old behaviour for every state but a file that holds the key
    // the one status line (plug_mtff.h, Versions): a file this build must not touch, then the older kinds of trouble, then a newer writer
    const bool newerWriter = s_mtffState == SidecarState::kLoaded && mtffWriterVersion(s_mtff) > mtffSelfVersion(s_mtff);
    if (sidecarBlocked(s_mtffState))
    {
        if (s_mtff.newer)
            snprintf(s_mtffNote, sizeof(s_mtffNote), "%s is of a newer format (%u, FF %u.%u.%u): settings not applied, file kept", kMtffName,
                     unsigned(s_mtff.format), unsigned(s_mtff.writer.major), unsigned(s_mtff.writer.minor), unsigned(s_mtff.writer.patch));
        else
            snprintf(s_mtffNote, sizeof(s_mtffNote), "%s not read (card error or over %d bytes): settings not applied, file kept", kMtffName,
                     kMtffMax);
    }
    else if (s_mtff.parked)
        snprintf(s_mtffNote, sizeof(s_mtffNote), "FF settings of %u line%s parked: project.mt was changed outside FF", unsigned(s_mtff.parked),
                 s_mtff.parked == 1 ? "" : "s");
    else if (s_mtff.stale)
        snprintf(s_mtffNote, sizeof(s_mtffNote), "project.mt was changed outside FF, settings applied");
    else if (newerWriter)
        snprintf(s_mtffNote, sizeof(s_mtffNote), "%s was written by FF %u.%u.%u: settings this build does not know are kept", kMtffName,
                 unsigned(s_mtff.writer.major), unsigned(s_mtff.writer.minor), unsigned(s_mtff.writer.patch));
#ifdef FEATURE_PARTS
    if (blanked)
    {
        const size_t at = strlen(s_mtffNote);
        snprintf(s_mtffNote + at, sizeof(s_mtffNote) - at, "%s%d Part cell%s blanked: pattern %u%s gone", at ? ", " : "", blanked, blanked == 1 ? "" : "s",
                 unsigned(gone), more ? " and others" : "");
    }
#endif
    if (fx34io::parkedCount())
    {   // after the other line, if there is one
        const size_t at = strlen(s_mtffNote);
        snprintf(s_mtffNote + at, sizeof(s_mtffNote) - at, "%sFX3/FX4 of the pattern on screen parked: the pattern file was changed outside FF",
                 at ? ", " : "");
    }
    if (s_mtffState == SidecarState::kLoaded || s_mtff.newer)
        LOGEF("mtff format %u ff %lx build %s -> this build format %d ff %lx", unsigned(s_mtff.format), (unsigned long)mtffWriterVersion(s_mtff),
              s_mtff.writer.build, kMtffMajor, (unsigned long)mtffSelfVersion(s_mtff));
    if (s_mtffNote[0]) LOGEF("%s", s_mtffNote);
    if (userOpen && (newerWriter || (sidecarBlocked(s_mtffState) && s_mtff.newer)))
    {   // for the screen (takeOpenNotice): a refused newer format first, then a newer writer. The cap and card errors stay in host.txt
        s_mtffNotice = sidecarBlocked(s_mtffState) ? 2 : 1;
        s_mtffNoticeFf = mtffWriterVersion(s_mtff);
        s_mtffNoticeFormat = uint16_t(s_mtff.format);
    }
}

// Workspace save: the file when a setting differs from the old behaviour or a line is kept, none otherwise
bool mtffSaved(const char* dir)
{
    SdOps fs;
    mtffCollect();
    if (!sidecarPath(s_pathA, kPathMax, dir, kMtffName))
    {
        LOGEF("project.mtff path too long: %s", dir);
        return mtffWrite(s_mtff, s_mtffRaw, s_mtffRawN, s_mtffTmp, kMtffMax) == 0;
    }
    const bool ok = mtffSave(fs, s_pathA, s_mtff, s_mtffState, s_mtffRaw, s_mtffRawN, kMtffMax, s_mtffTmp, kMtffMax);
    if (ok && s_mtffState == SidecarState::kUnreadable && s_mtff.newer && mtffWrite(s_mtff, s_mtffRaw, s_mtffRawN, s_mtffTmp, kMtffMax) != 0)
    {   // a file of a newer format stays as it is (Workspace, project folder, Save As copy): what this build holds for it is dropped, and
        // never reaches the Workspace file, so a reload cannot bring it back. Said once per load (host.txt) and in the log on every save
        LOGEF("%s is of a newer format: FF settings changed in this build are not saved, file kept", kMtffName);
        if (!strstr(s_mtffNote, "not saved"))
        {
            snprintf(s_mtffNote, sizeof(s_mtffNote), "%s is of a newer format (%u): FF settings changed here are not saved, file kept", kMtffName,
                     unsigned(s_mtff.format));
            writeStatus();
        }
    }
    return ok;
}

bool mtffOpened(const char* projectDir, const char* workspaceDir)
{
    SdOps fs;
    const bool wsOk = sidecarPath(s_pathB, kPathMax, workspaceDir, kMtffName);
    if (!sidecarPath(s_pathA, kPathMax, projectDir, kMtffName) || !wsOk)
    {
        LOGEF("project.mtff path too long: %s", projectDir);
        s_mtffOpenStale = true;
        if (wsOk) sidecarRemove(fs, s_pathB);
        return false;
    }
    return sidecarOpenStep(fs, s_pathA, s_pathB, s_mtffTmp, kMtffMax, s_mtffOpenStale);
}

bool mtffToProject(const char* workspaceDir, const char* projectDir)
{
    SdOps fs;
    mtffCollect();
    const bool has = mtffWrite(s_mtff, s_mtffRaw, s_mtffRawN, s_mtffTmp, kMtffMax) != 0;
    if (!sidecarPath(s_pathA, kPathMax, workspaceDir, kMtffName) || !sidecarPath(s_pathB, kPathMax, projectDir, kMtffName))
    {
        LOGEF("project.mtff path too long: %s", projectDir);
        return !has;
    }
    return mtffProjectStep(fs, s_pathA, s_pathB, has, s_mtffState, s_mtffTmp, kMtffMax);
}
}   // namespace

void newProjectSaved(const char* dir)
{
    if (s_fs) mtffSaved(dir);   // the new project's project.mtff (Strum ramp 100) is stamped with the project.mt that was just written
}

bool takeOpenNotice(char* line1, int cap1, char* line2, int cap2)
{
    if (!s_mtffNotice) return false;
    const unsigned a = (s_mtffNoticeFf >> 16) & 0xFF, b = (s_mtffNoticeFf >> 8) & 0xFF, c = s_mtffNoticeFf & 0xFF;
    if (s_mtffNotice == 1)
    {   // the popup is 400 px wide: two short lines
        snprintf(line1, size_t(cap1), "Saved by newer FF %u.%u.%u", a, b, c);
        snprintf(line2, size_t(cap2), "Some settings ignored");
    }
    else
    {
        snprintf(line1, size_t(cap1), "Project from newer FF (format %u)", unsigned(s_mtffNoticeFormat));
        snprintf(line2, size_t(cap2), "FF settings not loaded");
    }
    s_mtffNotice = 0;
    return true;
}

uint32_t mtffWriterVersion()
{
    return plug::mtffWriterVersion(s_mtff);
}

int mtffFormatVersion()
{
    return plug::mtffFormatVersion(s_mtff);
}

void projectStamp(uint32_t crc, uint32_t size)
{
    s_stamp.crc = crc;
    s_stamp.size = size;
    s_stamp.valid = true;
}

int strumRampDepth()
{
    return strumramp::depthPct();
}

void setStrumRampDepth(int pct)
{
    strumramp::setDepth(pct);
}

int macroMode()
{
#ifdef FEATURE_SYNTH_OPEN
    return polysynth::open::projectMacroMode();
#else
    return 0;
#endif
}

void setMacroMode(int mode)
{
#ifdef FEATURE_SYNTH_OPEN
    polysynth::open::setProjectMacroMode(uint8_t(mode));
#else
    (void)mode;
#endif
}

void projectClosing()
{
    setMacroMode(0);   // a patch the load adds (an Init fallback) is a patch of no project yet: project.mtff sets the mode after the load
}

void projectLoaded(const char* dir, bool newProject)
{
    if (!s_fs) return;
    LOGEF("loaded %s %d", dir, int(newProject));
    if (newProject) s_openStale = false;
    SdOps fs;
    // no sidecar: no plugin, as stock. After a failed open copy nothing of the Workspace file is used (kStale)
    if (sidecarPath(s_pathA, kPathMax, dir, kSidecarName))
        s_sideState = sidecarLoadStep(fs, s_pathA, s_text, sizeof(s_text), s_side, s_openStale);
    else
    {   // a path that does not fit: not read, the file (if any) is neither used nor written over
        LOGEF("sidecar path too long: %s", dir);
        memset(&s_side, 0, sizeof(s_side));
        s_sideState = SidecarState::kUnreadable;
        s_openStale = false;
    }
    mtffLoaded(dir, newProject);
    s_on = insertsOnAfterLoad(mtProject.values.saturationType, s_side);
    s_note[0] = 0;
    if (sidecarBlocked(s_sideState))
        snprintf(s_note, sizeof(s_note), "%s not read (card error): inserts empty, the file is kept", kSidecarName);
#ifndef FEATURE_FF_DIRT
    {   // a project saved with a built-in Dirt entry of an FF build: Inserts with Insert 1 = Dirt, or Hard Clip without dirt.plg
        uint8_t& sat = mtProject.values.saturationType;
        const int was = sat - kSatDirtFirst;
        const int ix = indexFind(kDirtUid);
        const int r = dirtMigrate(sat, s_side, ix >= 0, ix >= 0 ? indexAt(ix)->version : 0);
        if (r)
        {
            snprintf(s_note, sizeof(s_note), r == 1 ? "project had %s: now Inserts, insert 1 Dirt" :
                                                      "project had %s: now Hard Clip, no dirt.plg in /Plugins/Master",
                     kDirtEntryNames[was]);
            s_on = r == 1;   // in memory now, the project file and its sidecar change at the next save
        }
    }
#endif
    // the mixer stage under Inserts comes from the sidecar (loadProjectValues set the type before it was read)
    engine.setSaturationType(SaturationType(mtProject.values.saturationType));
    for (int k = 0; k < kSlots; k++)
    {
        unload(k);
        s_slots[k].state = SlotState::kEmpty;
        s_slots[k].pending = s_side.slot[k].uid != 0;   // loads in tick(), after the boot key window and the rest of plug_defer.h
        s_slots[k].manual = false;
    }
    s_openGate.arm(now());
    if (s_note[0] || s_mtffNote[0]) writeStatus();
}

bool projectSave(const char* dir)
{
    if (!s_fs) return true;
    for (int k = 0; k < kSlots; k++) syncRef(k);
    if (s_count || s_side.slot[0].uid || s_side.slot[1].uid || sidecarBlocked(s_sideState)) writeStatus();
    bool ok;
    if (!sidecarPath(s_pathA, kPathMax, dir, kSidecarName))
    {   // no write to a cut name: true only when there is nothing to store
        LOGEF("sidecar path too long: %s", dir);
        ok = sidecarWrite(s_side, s_text, sizeof(s_text)) == 0;
    }
    else
    {
        SdOps fs;
        ok = sidecarSave(fs, s_pathA, s_side, s_sideState, s_text, sizeof(s_text));
    }
    return mtffSaved(dir) && ok;   // both files are tried, whatever the first said
}

bool sidecarOpen(const char* projectDir, const char* workspaceDir)
{
    if (!s_fs) return true;
    s_mtffUserOpen = true;   // the project load that follows is a user's open (projectLoaded reads and clears it)
    SdOps fs;
    const bool mtffOk = mtffOpened(projectDir, workspaceDir);   // first: it uses the path buffers too
    const bool wsOk = sidecarPath(s_pathB, kPathMax, workspaceDir, kSidecarName);
    if (!sidecarPath(s_pathA, kPathMax, projectDir, kSidecarName) || !wsOk)
    {   // the project's file cannot be named: as a failed copy (the Workspace file is not the project's, the project's is kept)
        LOGEF("sidecar path too long: %s", projectDir);
        s_openStale = true;
        if (wsOk) sidecarRemove(fs, s_pathB);
        return false;
    }
    return sidecarOpenStep(fs, s_pathA, s_pathB, s_text, sizeof(s_text), s_openStale) && mtffOk;   // the open's retry calls it again: the last counts
}

bool sidecarToProject(const char* workspaceDir, const char* projectDir)
{
    if (!s_fs) return true;
    const bool mtffOk = mtffToProject(workspaceDir, projectDir);   // first: it uses the path buffers too
    if (!sidecarPath(s_pathA, kPathMax, workspaceDir, kSidecarName) || !sidecarPath(s_pathB, kPathMax, projectDir, kSidecarName))
    {   // nothing is written to a cut name. True when the Workspace has no settings to carry over
        LOGEF("sidecar path too long: %s", projectDir);
        return sidecarWrite(s_side, s_text, sizeof(s_text)) == 0 && mtffOk;
    }
    SdOps fs;
    return sidecarProjectStep(fs, s_pathA, s_pathB, s_side, s_sideState, s_text, sizeof(s_text)) && mtffOk;
}

// ------------------------------------------------------------------------------------------------ boot and tick
void boot(Platform* platform)
{
    FileSystem* fs = platform->getFileSystem();
    s_fs = fs;   // synthBoot set it already (before the synth started)
    s_buildId = platform->getSystemInfo()->buildId;   // the git sha7 of the image, named in the header of project.mtff
    s_timer = platform->getTimer();
    memset(&s_side, 0, sizeof(s_side));
    memset(s_slots, 0, sizeof(s_slots));
    memset(&s_pair, 0, sizeof(s_pair));
    memset(&s_scan, 0, sizeof(s_scan));
#ifdef FEATURE_PLUG_BENCH
    if (fs->exists("/plugbench.plg")) bench(fs);
#endif
    if (fs->exists(kMarker))
    {   // the last session died with a plugin starting: this one loads none on its own and says which
        char name[kNameLen] = {};
        auto [ok, f] = fs->openFile(kMarker, FileSystem::FileOpenMode::kRead);
        if (ok)
        {
            fs->readFile(name, kNameLen - 1, f);
            fs->closeFile(f);
        }
        fs->remove(kMarker);
        char why[48];
        snprintf(why, sizeof(why), "Safe mode: %s crashed", name);
        enterSafe(why);
    }
    rescan();
    synthLogos(platform->getDisplayDriver());
    s_bootMs = now();
    s_windowOpen = true;
}

void tick()
{
    if (!s_fs) return;
    // any path that changes saturationType (the master screen, a new project, Delete on the Saturation column)
    setInsertsOn(mtProject.values.saturationType == kSatInserts);
    if (s_windowOpen)
    {
        if (plugUiShiftHeld()) enterSafe("Safe mode: Shift at power on");
        if (now() - s_bootMs < kKeyWindowMs) return;   // nothing loads before the key window closed
        s_windowOpen = false;
    }
    for (int k = 0; k < kSlots; k++)
    {   // a plugin refused for room loads once the other insert leaves it room (another pick, None, a missing or failed one)
        Slot& s = s_slots[k];
        if (s.state == SlotState::kNoRoom && !s.pending && s_pair.retry(k)) s.pending = true;
    }
    bool openLoaded = false;   // one load that comes from a project load per pass (plug_defer.h)
    for (int k = 0; k < kSlots; k++)
    {
        Slot& s = s_slots[k];
        if (!s.pending) continue;
        if (s.manual)
        {   // a pick on the Inserts page: its own short debounce
            if (now() - s_pickMs >= kDebounceMs) apply(k);
        }
        else if (!openLoaded && s_openGate.ready(now()))
        {
            apply(k);
            s_openGate.took(now());
            openLoaded = true;
        }
    }
    if (s_markerSet && now() - s_markerMs >= kMarkerMs)
    {
        s_fs->remove(kMarker);
        s_markerSet = false;
    }
    synthService();   // ABI 1.2: the synth engines' main thread work (card waves), rate limited inside
}

}   // namespace plug

#endif   // FEATURE_PLUG_HOST
