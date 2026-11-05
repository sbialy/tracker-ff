// Spike bench (FEATURE_PLUG_BENCH): does DSP code loaded from the SD card run fast enough for the audio block, and from which
// memory? At boot, when /plugbench.plg exists, the same plugin (the Dirt master insert, synth-open/plug/fx_dirt_plug.cpp) runs
//   F  in-image copy of the same source, from flash (LMEM code cache + FMC cache)        = the reference
//   L  loaded into the SRAM_L slot (0x1FFFxxxx, code bus, zero wait, not cached)
//   U  loaded into the heap (SRAM_U, 0x2000xxxx, system bus)
//   D  loaded into the SDRAM (0x7xxxxxxx, system bus, no cache, 90 MHz 16 bit)
//   N  only with /plugbench.nvm: the text programmed into two erased sectors at the end of the FlexNVM data flash (code bus,
//      FMC + LMEM caches, like the image), rw in SRAM, sectors erased again afterwards. Skipped when they are not blank
//   A  only with /plugbench.alias: loaded into the SDRAM and run through the code bus alias 0x08000000 (cached by LMEM),
//      needs SDRAM_CM0[BAM] bits 31..27 set (K66 RM table 5-1), may fault: the safe rows are written to the file first
// 64 blocks of 128 stereo frames each, warm (cache kept) and cold (LMEM and FMC caches invalidated before every block), cycles
// from DWT_CYCCNT with all interrupts off around each process() call. The output checksum must match F on every row.
// Result: /plugbench.txt. The audio interrupt is off during the bench (about 0.1 s at boot).
#ifdef FEATURE_PLUG_BENCH

#include "plug_host.h"

#include "Platform.h"
#include "interruptsControl.h"
static inline void irqOff() { asm volatile("cpsid i" ::: "memory"); }
static inline void irqOn() { asm volatile("cpsie i" ::: "memory"); }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t externalRamScratchBuff[];
extern "C" const plug_desc* plug_entry_dirt(const plug_host* host, uint32_t abi);

namespace plug
{

namespace
{
constexpr int kBlocks = 64;
constexpr int kN = 128;
float s_src[2][kN];
float s_io[2][kN];
alignas(8) uint8_t s_inst[512];

inline uint32_t cyc()
{
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004);
}

void flushCaches()
{
    syncCode();   // LMEM code cache
    *reinterpret_cast<volatile uint32_t*>(0x4001F004) |= 0x00F80000u;   // FMC_PFB01CR CINV_WAY | S_B_INV
    *reinterpret_cast<volatile uint32_t*>(0x4001F008) |= 0x00F80000u;   // FMC_PFB23CR
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
}

struct Stat
{
    uint32_t mn = 0xFFFFFFFFu, mx = 0, avg = 0, chk = 0x811C9DC5u;
};

Stat run(const plug_desc* d, bool cold)
{
    Stat s;
    uint64_t sum = 0;
    memset(s_inst, 0, sizeof(s_inst));
    d->init(s_inst, hostTable());
    float* io[2] = {s_io[0], s_io[1]};
    for (int b = 0; b < kBlocks; b++)
    {
        const float g = 0.25f + 0.05f * float(b & 15);
        for (int ch = 0; ch < 2; ch++)
            for (int i = 0; i < kN; i++) s_io[ch][i] = s_src[ch][i] * g;
        if (cold) flushCaches();
        irqOff();
        const uint32_t t0 = cyc();
        d->process(s_inst, io, 2, kN);
        const uint32_t dt = cyc() - t0;
        irqOn();
        sum += dt;
        s.mn = dt < s.mn ? dt : s.mn;
        s.mx = dt > s.mx ? dt : s.mx;
        const uint32_t* w = reinterpret_cast<const uint32_t*>(s_io);
        for (int i = 0; i < 2 * kN; i++) s.chk = (s.chk ^ w[i]) * 0x01000193u;
    }
    s.avg = uint32_t(sum / kBlocks);
    return s;
}

int row(char* out, int cap, const char* tag, uintptr_t base, int err, uint32_t loadCyc, const plug_desc* d)
{
    if (err != kOk || !d) return snprintf(out, cap, "%s base=0x%08lx err=%d\r\n", tag, (unsigned long)base, err);
    const Stat w = run(d, false);
    const Stat c = run(d, true);
    return snprintf(out, cap,
                    "%s base=0x%08lx load_us=%lu warm min/avg/max=%lu/%lu/%lu cold min/avg/max=%lu/%lu/%lu chk=%08lx/%08lx\r\n",
                    tag, (unsigned long)base, (unsigned long)(loadCyc / 180), (unsigned long)w.mn, (unsigned long)w.avg,
                    (unsigned long)w.mx, (unsigned long)c.mn, (unsigned long)c.avg, (unsigned long)c.mx, (unsigned long)w.chk,
                    (unsigned long)c.chk);
}

void writeOut(FileSystem* fs, const char* txt, bool append)
{
    if (!append) fs->remove("/plugbench.txt");
    auto [ok, f] = fs->openFile("/plugbench.txt", append ? FileSystem::FileOpenMode::kWriteAppend
                                                         : FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok) return;
    fs->writeFile(txt, strlen(txt), f);
    fs->closeFile(f);
}

int loaded(FileSystem* fs, char* out, int cap, const char* tag, const Place& pl)
{
    Loaded l;
    const uint32_t t0 = cyc();
    const Err e = load(fs, "/plugbench.plg", pl, PLUG_KIND_MASTER_INSERT, l);
    const uint32_t t1 = cyc();
    return row(out, cap, tag, pl.textRun, e, t1 - t0, l.desc);
}

int loadedAt(FileSystem* fs, char* out, int cap, const char* tag, uint8_t* dst, uintptr_t run)
{
    Place pl;
    pl.textDst = dst;
    pl.textLen = kSlotBytes;
    pl.textRun = run;
    return loaded(fs, out, cap, tag, pl);
}

// FlexNVM (data flash at 0x10000000, code bus, K66 RM table 5-1 and 32.1: "additional code store"). FTFE commands with
// address bit 23 set target the data flash (RM 32.4.12). Only the bench writes it, only sectors that read all 0xFF, and it
// erases them again afterwards.
constexpr uintptr_t kNvm = 0x10000000u;
constexpr uint32_t kSector = 4096;
volatile uint8_t* const kFstat = reinterpret_cast<volatile uint8_t*>(0x40020000);
inline volatile uint8_t& fccob(int n)
{   // FCCOB0..B live at 0x40020004.. in big endian groups of four (kinetis.h FTFL_FCCOBn)
    static const uint8_t kOff[12] = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8};
    return reinterpret_cast<volatile uint8_t*>(0x40020004)[kOff[n]];
}

bool ftfe(uint8_t cmd, uint32_t nvmOff, const uint8_t* data8)
{
    while (!(*kFstat & 0x80u)) {}
    *kFstat = 0x70u;   // clear RDCOLERR ACCERR FPVIOL
    const uint32_t a = 0x800000u | nvmOff;   // bit 23: data flash
    fccob(0) = cmd;
    fccob(1) = uint8_t(a >> 16);
    fccob(2) = uint8_t(a >> 8);
    fccob(3) = uint8_t(a);
    if (data8)
        for (int i = 0; i < 8; i++) fccob(4 + i) = data8[i];
    irqOff();
    *kFstat = 0x80u;   // launch
    while (!(*kFstat & 0x80u)) {}
    irqOn();
    return (*kFstat & 0x71u) == 0;
}

uint32_t nvmDataBytes()
{
    const uint32_t depart = (*reinterpret_cast<volatile uint32_t*>(0x4004804C) >> 8) & 0xFu;   // SIM_FCFG1[DEPART]
    static const uint16_t kKb[16] = {256, 0, 0, 224, 192, 128, 0, 0, 0, 0, 0, 32, 64, 128, 256, 256};   // RM table 32-4
    return uint32_t(kKb[depart]) * 1024u;
}

uint32_t s_nvmProgCycles;
bool nvmCommit(const uint8_t* src, uintptr_t run, uint32_t len)
{
    const uint32_t off = uint32_t(run - kNvm);
    const uint32_t t0 = cyc();
    for (uint32_t s = off & ~(kSector - 1); s < off + len; s += kSector)
        if (!ftfe(0x09, s, nullptr)) return false;   // ERSSCR
    for (uint32_t i = 0; i < len; i += 8)
    {
        uint8_t ph[8];
        for (int k = 0; k < 8; k++) ph[k] = i + k < len ? src[i + k] : 0xFF;
        if (!ftfe(0x07, off + i, ph)) return false;   // PGM8
    }
    s_nvmProgCycles = cyc() - t0;
    flushCaches();
    return memcmp(reinterpret_cast<const void*>(run), src, len) == 0;
}
}   // namespace

void bench(FileSystem* fs)
{
    static char out[2400];
    int n = 0;
    uint32_t lcg = 12345;
    for (int i = 0; i < kN; i++)
    {   // a saw plus noise, different per channel
        lcg = lcg * 1664525u + 1013904223u;
        const float noise = float(int32_t(lcg) >> 8) * (1.0f / 8388608.0f);
        s_src[0][i] = 2.0f * float(i) / kN - 1.0f + 0.2f * noise;
        s_src[1][i] = 0.8f * noise;
    }
    DISABLE_AUDIO_INTERRUPTS();
    n += snprintf(out + n, sizeof(out) - n, "plugbench abi %d.%d, %d blocks x %d frames stereo, block = 522240 cycles\r\n",
                  PLUG_ABI_MAJOR, PLUG_ABI_MINOR, kBlocks, kN);
    n += snprintf(out + n, sizeof(out) - n, "regs: SIM_FCFG1=%08lx (dflash %lu KB) FMC_PFB23CR=%08lx\r\n",
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4004804C), (unsigned long)(nvmDataBytes() / 1024),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4001F008));
    n += snprintf(out + n, sizeof(out) - n, "regs: MPU_CESR=%08lx LMEM_PCCCR=%08lx PCCRMR=%08lx SDRAM_CM0=%08lx FMC_PFB01CR=%08lx\r\n",
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4000D000),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0xE0082000),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0xE0082020),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4000F04C),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4001F004));
    {
        const plug_desc* d = plug_entry_dirt(hostTable(), (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
        const Err e = checkDesc(d, 0, 0, PLUG_KIND_MASTER_INSERT);
        n += row(out + n, sizeof(out) - n, "F", uintptr_t(d ? d->process : nullptr), e, 0, d);
    }
    n += loadedAt(fs, out + n, sizeof(out) - n, "L", sramSlot(), uintptr_t(sramSlot()));
    uint8_t* heap = static_cast<uint8_t*>(malloc(kSlotBytes + 16));
    if (heap)
    {
        uint8_t* u = reinterpret_cast<uint8_t*>((uintptr_t(heap) + 15) & ~uintptr_t(15));
        n += loadedAt(fs, out + n, sizeof(out) - n, "U", u, uintptr_t(u));
        free(heap);
    }
    n += loadedAt(fs, out + n, sizeof(out) - n, "D", externalRamScratchBuff, uintptr_t(externalRamScratchBuff));
    // the host side int16 <-> float conversion of one stereo block (AudioEffectPlug::update), from flash
    {
        static int16_t blk[2][kN];
        irqOff();
        const uint32_t t0 = cyc();
        for (int ch = 0; ch < 2; ch++)
            for (int i = 0; i < kN; i++) s_io[ch][i] = float(blk[ch][i]) * (1.0f / 32768.0f);
        for (int ch = 0; ch < 2; ch++)
            for (int i = 0; i < kN; i++)
            {
                int32_t v = int32_t(s_io[ch][i] * 32768.0f);
                blk[ch][i] = int16_t(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
            }
        const uint32_t dt = cyc() - t0;
        irqOn();
        n += snprintf(out + n, sizeof(out) - n, "conv int16<->float stereo block: %lu cycles\r\n", (unsigned long)dt);
    }
    // N: opt-in (/plugbench.nvm): text programmed into the last two 4 KB sectors of the data flash, rw in SRAM
    if (fs->exists("/plugbench.nvm"))
    {
        const uint32_t dbytes = nvmDataBytes();
        const uint32_t off = dbytes >= 2 * kSector ? dbytes - 2 * kSector : 0;
        bool blank = dbytes >= 2 * kSector;
        for (uint32_t i = 0; blank && i < 2 * kSector; i += 4)
            blank = *reinterpret_cast<volatile uint32_t*>(kNvm + off + i) == 0xFFFFFFFFu;
        if (!blank)
            n += snprintf(out + n, sizeof(out) - n, "N skipped: data flash %lu B, target not blank\r\n", (unsigned long)dbytes);
        else
        {
            static uint8_t rw[256];
            Place pl;
            pl.textDst = externalRamScratchBuff + 65536;   // staging, rebased for the flash address
            pl.textLen = 2 * kSector;
            pl.textRun = kNvm + off;
            pl.rwDst = rw;
            pl.rwLen = sizeof(rw);
            pl.rwRun = uintptr_t(rw);
            pl.commit = nvmCommit;
            n += loaded(fs, out + n, sizeof(out) - n, "N", pl);
            n += snprintf(out + n, sizeof(out) - n, "N program_us=%lu erase_back=%d\r\n", (unsigned long)(s_nvmProgCycles / 180),
                          int(ftfe(0x09, off, nullptr) && ftfe(0x09, off + kSector, nullptr)));
            flushCaches();
        }
    }
    memset(sramSlot(), 0, kSlotBytes);
    ENABLE_AUDIO_INTERRUPTS();
    writeOut(fs, out, false);

    if (!fs->exists("/plugbench.alias")) return;
    // Opt-in: the SDRAM through the code bus alias. Widen the SDRAMC block 0 decode so 0x88xxxxxx (what 0x08xxxxxx becomes,
    // K66 RM table 5-1) hits the same 16 MB, compare a few words by data reads, then run code there.
    writeOut(fs, "A: trying the code bus alias (a hang here means the alias does not decode)\r\n", true);
    DISABLE_AUDIO_INTERRUPTS();
    volatile uint32_t* cm0 = reinterpret_cast<volatile uint32_t*>(0x4000F04C);
    const uint32_t oldCm = *cm0;
    *cm0 = oldCm | 0xF8000000u;
    uint8_t* dst = externalRamScratchBuff + 16384;
    const uintptr_t alias = 0x08000000u + (uintptr_t(dst) & 0x00FFFFFFu);
    for (int i = 0; i < 64; i++) reinterpret_cast<volatile uint32_t*>(dst)[i] = 0xA5A50000u + uint32_t(i) * 0x01010101u;
    syncCode();
    bool same = true;
    for (int i = 0; i < 64; i++)
        same = same && reinterpret_cast<volatile uint32_t*>(alias)[i] == reinterpret_cast<volatile uint32_t*>(dst)[i];
    n = snprintf(out, sizeof(out), "A alias=0x%08lx data_match=%d\r\n", (unsigned long)alias, int(same));
    if (same) n += loadedAt(fs, out + n, sizeof(out) - n, "A", dst, alias);
    *cm0 = oldCm;
    syncCode();
    ENABLE_AUDIO_INTERRUPTS();
    writeOut(fs, out, true);
}

}   // namespace plug

#endif   // FEATURE_PLUG_BENCH
