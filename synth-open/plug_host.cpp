// Runtime plugin host, master insert host v1. See plug_host.h.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"

#include "Platform.h"
#include "cr_section_macros.h"
#include "interruptsControl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
__NOINIT(EXTERNAL_RAM) char s_names[kIndexMax * 40];
__NOINIT(EXTERNAL_RAM) char* s_nameList[kIndexMax];

constexpr const char* kDir = "/Plugins/Master";
constexpr const char* kMarker = "/Plugins/.loading";
constexpr uint32_t kBudget = 60000;         // cycles per block and slot: 11.5 % of a 522,240 cycle block, then bypass
constexpr uint32_t kKeyWindowMs = 4000;     // hold Shift this long after power on: no plugin loads this session
constexpr uint32_t kDebounceMs = 400;       // a UI pick loads once the selection rests this long
constexpr uint32_t kMarkerMs = 10000;       // clean audio after a load before the crash marker goes

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
bool s_safe, s_windowOpen;
char s_reason[48];
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

const plug_host s_host = {
    sizeof(plug_host), PLUG_HOST_LEVEL, 44117.64706f, hSin, hCos, hTan, hTanh, hExp, hLog, hPow, memcpy, memset,
    hostAlloc,         hostCycles,      hostQuery,
};

uint32_t crc32(uint32_t c, const uint8_t* p, uint32_t n)
{
    c = ~c;
    while (n--)
    {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

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

Err checkHeader(const plug_file_header& h, uint32_t wantKind)
{
    if (h.magic != PLUG_MAGIC || h.header_size < sizeof(h)) return kHeader;
    if (h.abi_major != PLUG_ABI_MAJOR || h.min_host > PLUG_HOST_LEVEL || h.cpu != PLUG_CPU_CM4F || h.kind != wantKind ||
        (h.flags & PLUG_F_S16))
        return kAbi;
    return kOk;
}

void path(char* out, int cap, const char* dir, const char* name)
{
    snprintf(out, size_t(cap), "%s/%s", dir, name);
}

void writeText(const char* p, const char* text, int n)
{
    s_fs->remove(p);
    auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok) return;
    s_fs->writeFile(text, size_t(n), f);
    s_fs->closeFile(f);
}

// ------------------------------------------------------------------------------------------------ slots
void unload(int k)
{
    Slot& s = s_slots[k];
    if (plugInsert()) plugInsert()->attach(k, nullptr, nullptr, 0);   // audio interrupt off inside: the node lets go first
    for (uint8_t i = 0; i < s.nHeap; i++) free(s.heap[i]);
    s.nHeap = 0;
    s.arenaUsed = 0;
    s.desc = nullptr;
    s.inst = nullptr;
    s.err = kOk;
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
        s_allocSlot = k;
        pl.rwDst = static_cast<uint8_t*>(hostAlloc(pl.rwLen ? pl.rwLen : 8, 0));
        s_allocSlot = -1;
        pl.rwRun = uintptr_t(pl.rwDst);
        pl.commit = nvmInstall;
        if (!pl.rwDst) err = kInit;
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
    if (plugInsert()) plugInsert()->attach(k, s.desc, s.inst, kBudget);
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
    if (d->num_params > uint32_t(kMaxParams) || (d->num_params && !inImage(uintptr_t(d->params), base, size))) return kDesc;
    for (uint32_t i = 0; i < d->num_params; i++)
        if (!inImage(uintptr_t(d->params[i].name), base, size) || d->params[i].max < d->params[i].min) return kDesc;
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
    const uint32_t rwLen = h.image_size - rwOff + h.bss_size;
    uint8_t* const rwDst = pl.rwDst ? pl.rwDst : pl.textDst + rwOff;
    const uintptr_t rwRun = pl.rwDst ? pl.rwRun : pl.textRun + rwOff;
    if (e == kOk && ((h.image_size & 3u) || (rwOff & 7u) || rwOff > h.image_size || h.rebase_count > h.image_size / 4 ||
                     h.entry_offset >= rwOff || rwOff > pl.textLen || (pl.rwDst ? rwLen > pl.rwLen : rwOff + rwLen > pl.textLen)))
        e = kTooBig;
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

void rescan()
{
    s_count = 0;
    if (!s_fs) return;
    int n = 0;
    s_fs->listDirContents(
      kDir, FileSystem::kFiles,
      [](const char* name) {
          const size_t l = strlen(name);
          return l > 4 && l < 40 && (strcmp(name + l - 4, ".plg") == 0 || strcmp(name + l - 4, ".PLG") == 0);
      },
      s_names, sizeof(s_names), 40, n, s_nameList);
    for (int i = 0; i < n && s_count < kIndexMax; i++)
    {
        char p[64];
        path(p, sizeof(p), kDir, s_nameList[i]);
        auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok) continue;
        struct
        {
            plug_file_header h;
            plug_manifest m;
        } hm;
        memset(&hm, 0, sizeof(hm));
        const int32_t got = s_fs->readFile(&hm, sizeof(hm), f);
        s_fs->closeFile(f);
        if (got < int32_t(sizeof(plug_file_header)) || checkHeader(hm.h, PLUG_KIND_MASTER_INSERT) != kOk) continue;
        IndexEntry& e = s_index[s_count];
        e.uid = hm.h.uid;
        e.version = hm.h.version;
        e.imageSize = hm.h.image_size;
        e.rwOffset = hm.h.rw_offset;
        e.bssSize = hm.h.bss_size;
        const bool man = hm.h.header_size >= sizeof(hm) && got == int32_t(sizeof(hm)) && hm.m.struct_size >= sizeof(plug_manifest);
        e.numParams = man ? hm.m.num_params : 0;
        copyName(e.name, man && hm.m.name[0] ? hm.m.name : s_nameList[i], man ? kNameLen : int(strlen(s_nameList[i])) - 4);
        snprintf(e.file, sizeof(e.file), "%s", s_nameList[i]);
        const int dup = indexFind(e.uid);   // two files with one uid: the newer version wins
        if (dup < 0)
            s_count++;
        else if (s_index[dup].version < e.version)
            s_index[dup] = e;
    }
}

// ------------------------------------------------------------------------------------------------ slots API
SlotState slotState(int k)
{
    const Slot& s = s_slots[k];
    if (s.pending) return SlotState::kPending;
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
    if (e && r.uid == e->uid) return;
    memset(&r, 0, sizeof(r));   // a new pick starts at the plugin's defaults
    if (e)
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

bool safeMode()
{
    return s_safe;
}

const char* safeReason()
{
    return s_reason;
}

// ------------------------------------------------------------------------------------------------ project sidecar
void projectLoaded(const char* dir)
{
    if (!s_fs) return;
    char p[64];
    path(p, sizeof(p), dir, kSidecarName);
    int n = 0;
    auto [ok, f] = s_fs->openFile(p, FileSystem::FileOpenMode::kRead);
    if (ok)
    {
        n = s_fs->readFile(s_text, sizeof(s_text), f);
        s_fs->closeFile(f);
    }
    if (n <= 0 || !sidecarRead(s_text, n, s_side)) memset(&s_side, 0, sizeof(s_side));   // no sidecar: no plugin, as stock
    for (int k = 0; k < kSlots; k++)
    {
        unload(k);
        s_slots[k].state = SlotState::kEmpty;
        s_slots[k].pending = s_side.slot[k].uid != 0;   // loads in tick(), after the boot key window
        s_slots[k].manual = false;
    }
}

bool projectSave(const char* dir)
{
    if (!s_fs) return true;
    for (int k = 0; k < kSlots; k++) syncRef(k);
    char p[64];
    path(p, sizeof(p), dir, kSidecarName);
    const int n = sidecarWrite(s_side, s_text, sizeof(s_text));
    if (n <= 0)
    {   // no plugin in any slot: no file, the project is what stock firmware would write
        if (s_fs->exists(p)) s_fs->remove(p);
        return n == 0;
    }
    writeText(p, s_text, n);
    return true;
}

void copySidecar(const char* srcDir, const char* dstDir)
{
    if (!s_fs) return;
    char src[64], dst[64];
    path(src, sizeof(src), srcDir, kSidecarName);
    path(dst, sizeof(dst), dstDir, kSidecarName);
    int n = -1;
    auto [ok, f] = s_fs->openFile(src, FileSystem::FileOpenMode::kRead);
    if (ok)
    {
        n = s_fs->readFile(s_text, sizeof(s_text), f);
        s_fs->closeFile(f);
    }
    if (n > 0)
        writeText(dst, s_text, n);
    else if (s_fs->exists(dst))
        s_fs->remove(dst);   // the source has no plugin: neither has the copy
}

// ------------------------------------------------------------------------------------------------ boot and tick
void boot(Platform* platform)
{
    FileSystem* fs = platform->getFileSystem();
    s_fs = fs;
    s_timer = platform->getTimer();
    memset(&s_side, 0, sizeof(s_side));
    memset(s_slots, 0, sizeof(s_slots));
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
    s_bootMs = now();
    s_windowOpen = true;
}

void tick()
{
    if (!s_fs) return;
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
