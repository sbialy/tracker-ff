// Runtime plugin host (spike). See plug_host.h.
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
// SRAM_L, NOLOAD: .dmabuffers is the first RAM output section of mk66fx1m0.ld after the USB descriptor table
__attribute__((section(".dmabuffers.plugslot"), aligned(16))) uint8_t s_slot[kSlotBytes];

constexpr uint32_t kArenaBytes = 64 * 1024;   // SDRAM arena for plugin delay lines and tables (alloc flag 1)
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) uint8_t s_arena[kArenaBytes];
uint32_t s_arenaUsed;

void* hostAlloc(uint32_t bytes, uint32_t flags)
{
    bytes = (bytes + 7u) & ~7u;
    if (flags & 1u)
    {
        if (bytes > kArenaBytes - s_arenaUsed) return nullptr;
        void* p = s_arena + s_arenaUsed;
        s_arenaUsed += bytes;
        memset(p, 0, bytes);
        return p;
    }
    void* p = malloc(bytes);   // SRAM heap, may fail (never an exception on MK66)
    if (p) memset(p, 0, bytes);
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
}   // namespace

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
    if (d->num_params > 32 || (d->num_params && !inImage(uintptr_t(d->params), base, size))) return kDesc;
    if (d->instance_size > 16384) return kDesc;
    return kOk;
}

Err load(FileSystem* fs, const char* path, const Place& pl, uint32_t wantKind, Loaded& out)
{
    out.desc = nullptr;
    auto [isOpen, f] = fs->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!isOpen) return kNoFile;
    plug_file_header& h = out.hdr;
    Err e = kOk;
    if (!readAll(fs, f, reinterpret_cast<uint8_t*>(&h), sizeof(h)) || h.magic != PLUG_MAGIC || h.header_size < sizeof(h))
        e = kHeader;
    else if (h.abi_major != PLUG_ABI_MAJOR || h.min_host > PLUG_HOST_LEVEL || h.cpu != PLUG_CPU_CM4F || h.kind != wantKind)
        e = kAbi;
    // text = [0, rw_offset) (code, constants: may run from flash), rw = [rw_offset, image_size) + bss (always RAM)
    const uint32_t rwOff = h.rw_offset ? h.rw_offset : h.image_size;
    const uint32_t rwLen = h.image_size - rwOff + h.bss_size;
    uint8_t* const rwDst = pl.rwDst ? pl.rwDst : pl.textDst + rwOff;
    const uintptr_t rwRun = pl.rwDst ? pl.rwRun : pl.textRun + rwOff;
    if (e == kOk && ((h.image_size & 3u) || (rwOff & 7u) || rwOff > h.image_size || h.rebase_count > h.image_size / 4 ||
                     h.entry_offset >= rwOff || rwOff > pl.textLen || (pl.rwDst ? rwLen > pl.rwLen : rwOff + rwLen > pl.textLen)))
        e = kTooBig;
    if (e == kOk && (!fs->seekFile(h.header_size, f) || !readAll(fs, f, pl.textDst, rwOff) ||
                     !readAll(fs, f, rwDst, h.image_size - rwOff)))
        e = kRead;
    if (e != kOk)
    {
        fs->closeFile(f);
        return e;
    }
    uint32_t crc = crc32(crc32(0, pl.textDst, rwOff), rwDst, h.image_size - rwOff);
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
    if (pl.commit && !pl.commit(pl.textDst, pl.textRun, rwOff)) return kRead;   // e.g. program the text into flash
    // the K66 system MPU must let the core fetch from RAM (PtfxLoader does the same)
    *reinterpret_cast<volatile uint32_t*>(0x4000D000) = 0;
    syncCode();
    out.base = pl.textRun;
    const plug_entry_fn entry = reinterpret_cast<plug_entry_fn>((pl.textRun + h.entry_offset) | 1u);
    const plug_desc* d = entry(&s_host, (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
    if (!d) return kEntry;
    // the descriptor and the functions are constants and code: in the text segment
    e = checkDesc(d, pl.textRun, rwOff, wantKind);
    if (e == kOk) out.desc = d;
    return e;
}

void boot(FileSystem* fs)
{
#ifdef FEATURE_PLUG_BENCH
    if (fs->exists("/plugbench.plg")) bench(fs);
#endif
    if (!fs->exists("/plugins/master.plg") || !plugInsert()) return;
    Loaded l;
    Place pl;
    pl.textDst = s_slot;
    pl.textLen = kSlotBytes;
    pl.textRun = uintptr_t(s_slot);
    Err e = load(fs, "/plugins/master.plg", pl, PLUG_KIND_MASTER_INSERT, l);
    void* inst = nullptr;
    if (e == kOk)
    {
        inst = hostAlloc(l.desc->instance_size, 0);
        if (!inst || l.desc->init(inst, &s_host) != 0) e = kDesc;
    }
    if (e == kOk) plugInsert()->attach(l.desc, inst, 60000);   // 11.5 % of a 522,240 cycle block
    char msg[96];
    const int n = snprintf(msg, sizeof(msg), "master.plg: err %ld, image %lu B, rebase %lu, slot 0x%08lx\r\n", long(e),
                           (unsigned long)l.hdr.image_size, (unsigned long)l.hdr.rebase_count, (unsigned long)uintptr_t(s_slot));
    fs->remove("/plugins/master.txt");
    auto [ok, f] = fs->openFile("/plugins/master.txt", FileSystem::FileOpenMode::kWriteCreateNew);
    if (ok)
    {
        fs->writeFile(msg, size_t(n), f);
        fs->closeFile(f);
    }
}

}   // namespace plug

#endif   // FEATURE_PLUG_HOST
