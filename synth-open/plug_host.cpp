// Runtime plugin host, master insert host v1. See plug_host.h.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"

#include "Platform.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"
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
// SRAM_L, NOLOAD: .dmabuffers is the first RAM output section of mk66fx1m0.ld after the USB descriptor table
__attribute__((section(".dmabuffers.plugslot"), aligned(16))) uint8_t s_slot[kSlotBytes];

constexpr uint32_t kArenaPerSlot = 32 * 1024;   // SDRAM for plugin delay lines and tables (alloc flag 1), per slot
// SDRAM globals are never constructed (no static constructor may touch the SDRAM, f563f27): plain arrays and PODs only,
// boot() clears what needs clearing
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_arena[kSlots][kArenaPerSlot];
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_stage[kNvmBytes];   // insert 2 text, rebased for the flash
__NOINIT(EXTERNAL_RAM) Sidecar s_side;                                            // the project's references and values
__NOINIT(EXTERNAL_RAM) char s_text[kSidecarMax];                                  // sidecar file text
__NOINIT(EXTERNAL_RAM) IndexEntry s_index[kIndexMax];
__NOINIT(EXTERNAL_RAM) char s_names[kScanMax * 40];
__NOINIT(EXTERNAL_RAM) char* s_nameList[kScanMax];
__NOINIT(EXTERNAL_RAM) ScanStats s_scan;
// insert 2's rw and bss at a fixed SRAM address: its text (rebased against this address) is then the same bytes on every
// load and a reload issues no flash command. Plugins with more rw + bss than this are refused for insert 2.
constexpr uint32_t kRw2Bytes = 1024;
__attribute__((aligned(8))) uint8_t s_rw2[kRw2Bytes];

constexpr const char* kDir = "/Plugins/Master";
constexpr uint32_t kBudget = 60000;         // cycles per block and slot: 11.5 % of a 522,240 cycle block, then bypass
constexpr uint32_t kKeyWindowMs = 4000;     // hold Shift this long after power on: no plugin loads this session
constexpr uint32_t kDebounceMs = 400;       // a UI pick loads once the selection rests this long
constexpr uint32_t kMarkerMs = 3000;        // clean running after a load before the crash marker goes (a power off
                                            // inside this window makes the next boot a safe one, harmless)

struct Slot
{
    const plug_desc* desc;
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
int s_allocSlot = -1;
int s_count;
FileSystem* s_fs;
bool s_safe, s_windowOpen, s_on;
char s_reason[48];
char s_note[96];   // what the last project load changed (a Dirt entry migrated), host.txt line 3
Timer* s_timer;
uint32_t s_bootMs, s_pickMs, s_markerMs;
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

const void* hostQuery(uint32_t)
{
    return nullptr;
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

// temp file then rename: a failed write never costs the old file
bool writeText(const char* p, const char* text, int n)
{
    char tmp[72];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    s_fs->remove(tmp);
    auto [ok, f] = s_fs->openFile(tmp, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok) return false;
    const bool wrote = s_fs->writeFile(text, size_t(n), f) == n;
    s_fs->closeFile(f);
    if (!wrote)
    {
        s_fs->remove(tmp);
        return false;
    }
    if (s_fs->exists(p)) s_fs->remove(p);
    return s_fs->rename(tmp, p);
}

namespace
{

// /Plugins/host.txt: what each slot holds, after every load and on every Workspace save (the device test reads it back)
void writeStatus()
{
    char t[800];
    int n = snprintf(t, sizeof(t), "plugin host 1, index %d, inserts %s, safe %d %s, nvm commands %lu\r\n", s_count,
                     s_on ? "on" : "bypassed", int(s_safe), s_reason, (unsigned long)nvmCommands());
    n += snprintf(t + n, sizeof(t) - size_t(n), "scan %s: listed %d, rejected %d\r\n", kDir, s_scan.listed, s_scan.rejected);
    n += snprintf(t + n, sizeof(t) - size_t(n), "mixer %s%s%s\r\n", saturationListTexts[mixerStage()], s_note[0] ? ", " : "", s_note);
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
    }
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
    s.desc = nullptr;
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
    plugInsert()->attach(k, s.desc, s.inst, kBudget);
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
        return;
    }
    if (s_safe && !s.manual)
    {
        s.state = SlotState::kSafe;
        return;
    }
    const IndexEntry& e = s_index[ix];
    char p[64];
    path(p, sizeof(p), kDir, e.file);
    Place pl;
    Err err = kOk;
    if (k == 0)
    {   // insert 1: text and rw in the SRAM_L slot
        pl.textDst = s_slot;
        pl.textLen = kSlotBytes;
        pl.textRun = uintptr_t(s_slot);
    }
    else
    {   // insert 2: text staged in SDRAM, rebased for and programmed into the data flash, rw on the heap
        pl.textDst = s_stage;
        pl.textLen = kNvmBytes;
        pl.textRun = kNvmRun;
        pl.rwLen = (e.imageSize - (e.rwOffset ? e.rwOffset : e.imageSize)) + e.bssSize;
        pl.rwDst = s_rw2;
        pl.rwRun = uintptr_t(s_rw2);
        pl.commit = nvmInstall;
        if (pl.rwLen > kRw2Bytes) err = kTooBig;
        pl.rwLen = kRw2Bytes;
    }
    // crash marker first: a plugin that hangs init or its first block leaves it behind, the next boot runs in safe mode
    writeText(kMarker, ref.name, int(strlen(ref.name)));
    s_markerSet = true;
    s_markerMs = now();
    Loaded l;
    if (err == kOk) err = load(s_fs, p, pl, PLUG_KIND_MASTER_INSERT, l);
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
        writeStatus();
        return;
    }
    s.desc = l.desc;
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
    if (s_on && plugInsert()) plugInsert()->attach(k, s.desc, s.inst, kBudget);   // bypassed: loaded, off the node
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
    }
    if (d->instance_size > 16384) return kDesc;
    return kOk;
}

Err load(FileSystem* fs, const char* path, const Place& pl, uint32_t wantKind, Loaded& out)
{
    out.desc = nullptr;
    auto [isOpen, f] = fs->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!isOpen) return kNoFile;
    plug_file_header& h = out.hdr;
    Err e = readAll(fs, f, reinterpret_cast<uint8_t*>(&h), sizeof(h)) ? checkHeader(h, wantKind) : kHeader;
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
            if ((off & 3u) || off + 4 > h.image_size)
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
    if (e == kOk) out.desc = d;
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
};
}   // namespace

int scanDir(const char* dir, IndexEntry* index, int cap, ScanStats& st, uint32_t kind)
{
    ScanFs fs;
    return scanIndex(fs, dir, index, cap, s_names, s_nameList, st, kind);
}

void rescan()
{
    s_count = 0;
    if (!s_fs) return;
    s_count = scanDir(kDir, s_index, kIndexMax, s_scan, PLUG_KIND_MASTER_INSERT);
    if (s_scan.listed) writeStatus();   // what the scan kept and refused, also when nothing loads
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

// ------------------------------------------------------------------------------------------------ project sidecar
namespace
{
// plug_sidecar.h's file rules on the SD card
struct SdFs
{
    int read(const char* p, char* buf, int cap)
    {
        auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok) return -1;
        const int n = s_fs->readFile(buf, size_t(cap), f);
        s_fs->closeFile(f);
        return n;
    }
    bool write(const char* p, const char* buf, int n) { return writeText(p, buf, n); }
    bool exists(const char* p) { return s_fs->exists(p); }
    bool remove(const char* p) { return s_fs->remove(p); }
};
}   // namespace

void projectLoaded(const char* dir)
{
    if (!s_fs) return;
    char p[64];
    path(p, sizeof(p), dir, kSidecarName);
    SdFs fs;
    sidecarLoad(fs, p, s_text, sizeof(s_text), s_side);   // no sidecar: no plugin, as stock
    s_on = insertsOnAfterLoad(mtProject.values.saturationType, s_side);
    s_note[0] = 0;
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
        s_slots[k].pending = s_side.slot[k].uid != 0;   // loads in tick(), after the boot key window
        s_slots[k].manual = false;
    }
    if (s_note[0]) writeStatus();
}

bool projectSave(const char* dir)
{
    if (!s_fs) return true;
    for (int k = 0; k < kSlots; k++) syncRef(k);
    if (s_count || s_side.slot[0].uid || s_side.slot[1].uid) writeStatus();
    char p[64];
    path(p, sizeof(p), dir, kSidecarName);
    SdFs fs;
    return sidecarSave(fs, p, s_side, s_text, sizeof(s_text));
}

void copySidecar(const char* srcDir, const char* dstDir)
{
    if (!s_fs) return;
    char src[64], dst[64];
    path(src, sizeof(src), srcDir, kSidecarName);
    path(dst, sizeof(dst), dstDir, kSidecarName);
    SdFs fs;
    sidecarCopy(fs, src, dst, s_text, sizeof(s_text));
}

// ------------------------------------------------------------------------------------------------ boot and tick
void boot(Platform* platform)
{
    FileSystem* fs = platform->getFileSystem();
    s_fs = fs;   // synthBoot set it already (before the synth started)
    s_timer = platform->getTimer();
    memset(&s_side, 0, sizeof(s_side));
    memset(s_slots, 0, sizeof(s_slots));
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
        if (s_slots[k].pending && (!s_slots[k].manual || now() - s_pickMs >= kDebounceMs)) apply(k);
    if (s_markerSet && now() - s_markerMs >= kMarkerMs)
    {
        s_fs->remove(kMarker);
        s_markerSet = false;
    }
}

}   // namespace plug

#endif   // FEATURE_PLUG_HOST
