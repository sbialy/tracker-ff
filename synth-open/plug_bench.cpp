// Plugin bench (FEATURE_PLUG_BENCH): does DSP code loaded from the SD card run fast enough for the audio block, and from which
// memory? At boot, when /plugbench.plg exists, the same plugin (the Dirt master insert, src/synth-open/plug/fx_dirt_plug.cpp of tracker-effects-os) runs
//   F  in-image copy of the same source, from flash (LMEM code cache + FMC cache)        = the reference
//   L  loaded into the SRAM_L slot (0x1FFFxxxx, code bus, zero wait, not cached)
//   U  loaded into the heap (SRAM_U, 0x2000xxxx, system bus)
//   D  loaded into the SDRAM (0x7xxxxxxx, system bus, no cache, 90 MHz 16 bit)
//   N  only with /plugbench.nvm: the text programmed into two erased sectors at the end of the FlexNVM data flash (code bus,
//      FMC + LMEM caches, like the image), rw in SRAM, sectors erased again afterwards. Skipped when they are not blank
//   A  only with /plugbench.alias: loaded into the SDRAM and run through the code bus alias 0x08000000 (cached by LMEM),
//      needs SDRAM_CM0[BAM] bits 31..27 set (K66 RM table 5-1), may fault: the safe rows are written to the file first
// 64 blocks of 128 stereo frames each, warm (cache kept) and cold (LMEM and FMC caches invalidated before every block), cycles
// from DWT_CYCCNT with all interrupts off around each process() call. Every row also dumps the plugin instance (Dirt's
// Inst: Coef, State, preset) after init() and after the warm run. The output checksum of a loaded row must equal
// emu_plug.py's checksum of the same .plg, and F must equal L when the .plg was built with the firmware's flags for every
// object (dirt.plg since 1b: ff_dirt_ctl.cpp at -Os like the image, plugin-architecture.md section 11).
// N (since 1b): each FTFE command leaves HSRUN first and runs its launch and wait loop from RAM, as Teensy's eeprom.c does
// (no flash program or erase in HSRUN), and logs FSTAT before and after, the FCCOB bytes read back, SMC_PMSTAT, SIM_CLKDIV1
// and the protection registers, per step: erase, program, verify, erase back, blank check.
// Result: /plugbench.txt. The audio interrupt is off during the bench (about 0.1 s at boot).
#ifdef FEATURE_PLUG_BENCH

#include "plug_fccob.h"
#include "plug_host.h"

#include "Platform.h"
#include "interruptsControl.h"
static inline void irqOff() { asm volatile("cpsid i" ::: "memory"); }
static inline void irqOn() { asm volatile("cpsie i" ::: "memory"); }

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t externalRamScratchBuff[];
extern "C" const plug_desc* plug_entry_dirt(const plug_host* host, uint32_t abi);
extern "C" int kinetis_hsrun_disable(void);   // teensy_core mk20dx128.c: RUN at 90 MHz core, bus and SDRAM clocks kept
extern "C" int kinetis_hsrun_enable(void);

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
        const float g = __builtin_fmaf(float(b & 15), 0.05f, 0.25f);   // fused as in run 1 (-Os), now by the source
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

// the instance as 32 bit words (Dirt: Coef pre post bias biasY dcA g mask half quant tone rate type, State, preset)
int instDump(char* out, int cap, const char* tag, const char* when, const plug_desc* d)
{
    const uint32_t bytes = d->instance_size < sizeof(s_inst) ? d->instance_size : sizeof(s_inst);
    int n = snprintf(out, cap, "%s inst %s (%lu B):", tag, when, (unsigned long)d->instance_size);
    for (uint32_t i = 0; i + 4 <= bytes && n < cap; i += 4)
    {
        uint32_t w;
        memcpy(&w, s_inst + i, 4);
        n += snprintf(out + n, cap - n, " %08lx", (unsigned long)w);
    }
    if (n < cap) n += snprintf(out + n, cap - n, "\r\n");
    return n < cap ? n : cap - 1;
}

int row(char* out, int cap, const char* tag, uintptr_t base, int err, uint32_t loadCyc, const plug_desc* d)
{
    if (err != kOk || !d) return snprintf(out, cap, "%s base=0x%08lx err=%d\r\n", tag, (unsigned long)base, err);
    memset(s_inst, 0, sizeof(s_inst));
    d->init(s_inst, hostTable());
    int n = instDump(out, cap, tag, "init", d);
    const Stat w = run(d, false);
    n += instDump(out + n, cap - n, tag, "after warm", d);
    const Stat c = run(d, true);
    n += snprintf(out + n, cap - n,
                  "%s base=0x%08lx load_us=%lu warm min/avg/max=%lu/%lu/%lu cold min/avg/max=%lu/%lu/%lu chk=%08lx/%08lx\r\n",
                  tag, (unsigned long)base, (unsigned long)(loadCyc / 180), (unsigned long)w.mn, (unsigned long)w.avg,
                  (unsigned long)w.mx, (unsigned long)c.mn, (unsigned long)c.avg, (unsigned long)c.mx, (unsigned long)w.chk,
                  (unsigned long)c.chk);
    return n < cap ? n : cap - 1;
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
// address bit 23 set target the data flash (RM 32.4.12). Only the bench writes it, only the last two sectors of the data
// flash, only when they read all 0xFF, only ERSSCR (0x09) and PGM8 (0x07), and it erases them again afterwards. Never a
// program flash address, never PGMPART or SETRAM (the FlexRAM stays the firmware's EEPROM).
// A first bench run failed the very first ERSSCR: it was launched in HSRUN (180 MHz), where the K66 flash cannot be
// programmed or erased. Teensy's eeprom.c leaves HSRUN around every flash operation (kinetis_hsrun_disable), so does this.
constexpr uintptr_t kNvm = 0x10000000u;
constexpr uint32_t kSector = 4096;
volatile uint8_t* const kFstat = reinterpret_cast<volatile uint8_t*>(0x40020000);
inline volatile uint8_t& ftfeReg(uint32_t off)
{
    return reinterpret_cast<volatile uint8_t*>(0x40020000)[off];
}
inline volatile uint8_t& fccob(int n)
{   // FCCOB0..B live at 0x40020004.. in big endian groups of four (kinetis.h FTFL_FCCOBn: FCCOB0 = 0x40020007)
    static const uint8_t kOff[12] = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8};
    return reinterpret_cast<volatile uint8_t*>(0x40020004)[kOff[n]];
}
inline uint32_t reg32(uintptr_t a)
{
    return *reinterpret_cast<volatile uint32_t*>(a);
}
inline uint8_t reg8(uintptr_t a)
{
    return *reinterpret_cast<volatile uint8_t*>(a);
}

uint32_t nvmDataBytes()
{
    const uint32_t depart = (reg32(0x4004804C) >> 8) & 0xFu;   // SIM_FCFG1[DEPART]
    static const uint16_t kKb[16] = {256, 0, 0, 224, 192, 128, 0, 0, 0, 0, 0, 32, 64, 128, 256, 256};   // RM table 32-4
    return uint32_t(kKb[depart]) * 1024u;
}

// The launch and the wait run from RAM (eeprom.c's do_flash_cmd, the same 8 halfwords): strb 0x80 to FSTAT, loop until
// CCIF. r0 = &FSTAT.
alignas(4) uint16_t s_flashCmd[8];
const uint16_t kFlashCmd[8] = {0xf06f, 0x037f, 0x7003, 0x7803, 0xf013, 0x0f80, 0xd0fb, 0x4770};

// The FTFE log: the bench's text buffer, set by bench()
char* s_log;
int s_logCap, s_logN;
uint32_t s_nvmLo, s_nvmHi;   // the only data flash offsets a command may name: [lo, hi)
uint32_t s_cmds, s_fails;
const char* s_step = "";

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...)
{
    if (!s_log || s_logN >= s_logCap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(s_log + s_logN, s_logCap - s_logN, fmt, ap);
    va_end(ap);
    if (k > 0) s_logN = s_logN + k < s_logCap ? s_logN + k : s_logCap - 1;
}

void logProt()
{
    logf(" FCNFG=%02x FSEC=%02x FOPT=%02x FPROT0-3=%02x%02x%02x%02x FEPROT=%02x FDPROT=%02x", reg8(0x40020001),
         reg8(0x40020002), reg8(0x40020003), reg8(0x40020013), reg8(0x40020012), reg8(0x40020011), reg8(0x40020010),
         reg8(0x40020016), reg8(0x40020017));
}

// verbose: log this command even when it succeeds (the erases, the first program, every failure is logged anyway)
bool ftfe(uint8_t cmd, uint32_t nvmOff, const uint8_t* data8, bool verbose)
{
    const uint32_t a = 0x800000u | nvmOff;   // bit 23: data flash
    if ((cmd != 0x09 && cmd != 0x07) || !(a & 0x800000u) || nvmOff < s_nvmLo || nvmOff >= s_nvmHi || (nvmOff & 7u) ||
        (cmd == 0x09 && (nvmOff & (kSector - 1))))
    {
        s_fails++;
        logf("N %s cmd=%02x off=0x%05lx REFUSED (outside the two bench sectors or not a bench command)\r\n", s_step, cmd,
             (unsigned long)nvmOff);
        return false;
    }
    s_cmds++;
    irqOff();
    uint32_t spin = 0;
    while (!(*kFstat & 0x80u) && ++spin < 10000000u) {}
    const uint8_t before = *kFstat;
    *kFstat = 0x70u;   // clear RDCOLERR ACCERR FPVIOL (write 1)
    fccob(0) = cmd;
    fccob(1) = uint8_t(a >> 16);
    fccob(2) = uint8_t(a >> 8);
    fccob(3) = uint8_t(a);
    if (data8)   // FCCOB4 is the MSB of each longword: the source bytes go in reversed per longword (run 2 wrote 09 0c 01 29 and read 29 back)
        for (int i = 0; i < 8; i++) fccob(4 + i) = pgm8Fccob(data8, i);
    uint8_t cc[12];
    for (int i = 0; i < 12; i++) cc[i] = fccob(i);
    const uint8_t pmBefore = reg8(0x4007E003);   // SMC_PMSTAT: 0x80 HSRUN, 0x01 RUN
    const uint32_t div = reg32(0x40048044);       // SIM_CLKDIV1
    const int left = kinetis_hsrun_disable();
    const uint8_t pmDuring = reg8(0x4007E003);
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
    reinterpret_cast<void (*)(volatile uint8_t*)>(uintptr_t(s_flashCmd) | 1u)(kFstat);
    const uint8_t after = *kFstat;
    if (left) kinetis_hsrun_enable();
    if (reg32(0x40048044) != div) *reinterpret_cast<volatile uint32_t*>(0x40048044) = div;
    const uint8_t pmAfter = reg8(0x4007E003);
    irqOn();
    const bool ok = (after & 0xF1u) == 0x80u;   // CCIF, no ACCERR FPVIOL RDCOLERR MGSTAT0
    if (!ok) s_fails++;
    if (verbose || !ok)
    {
        logf("N %s cmd=%02x a=0x%06lx fccob=", s_step, cmd, (unsigned long)a);
        for (int i = 0; i < (data8 ? 12 : 4); i++) logf("%02x", cc[i]);
        logf(" FSTAT %02x->%02x pmstat %02x/%02x/%02x clkdiv1=%08lx hsrun_left=%d", before, after, pmBefore, pmDuring, pmAfter,
             (unsigned long)div, left);
        logProt();
        logf(" %s\r\n", ok ? "ok" : "FAIL");
    }
    return ok;
}

bool blankCheck(uint32_t off, uint32_t len, uint32_t* firstBad)
{
    for (uint32_t i = 0; i < len; i += 4)
        if (reg32(kNvm + off + i) != 0xFFFFFFFFu)
        {
            *firstBad = off + i;
            return false;
        }
    return true;
}

uint32_t s_nvmProgCycles;
bool nvmCommit(const uint8_t* src, uintptr_t run, uint32_t len)
{
    const uint32_t off = uint32_t(run - kNvm);
    const uint32_t t0 = cyc();
    s_step = "erase";
    for (uint32_t s = off & ~(kSector - 1); s < off + len; s += kSector)
        if (!ftfe(0x09, s, nullptr, true)) return false;   // ERSSCR
    flushCaches();
    uint32_t bad = 0;
    if (!blankCheck(off & ~(kSector - 1), ((off + len + kSector - 1) & ~(kSector - 1)) - (off & ~(kSector - 1)), &bad))
    {
        logf("N erase: not blank after erase at 0x%05lx\r\n", (unsigned long)bad);
        return false;
    }
    s_step = "program";
    for (uint32_t i = 0; i < len; i += 8)
    {
        uint8_t ph[8];
        for (int k = 0; k < 8; k++) ph[k] = i + k < len ? src[i + k] : 0xFF;
        if (!ftfe(0x07, off + i, ph, i == 0)) return false;   // PGM8
    }
    s_nvmProgCycles = cyc() - t0;
    flushCaches();
    s_step = "verify";
    {   // word compare (the padding past len reads back 0xFF), log the first 8 mismatching words with got and want
        uint32_t bad = 0, firstOff = 0;
        for (uint32_t i = 0; i < len; i += 4)
        {
            uint32_t want = 0;
            for (int k = 3; k >= 0; k--) want = (want << 8) | (i + k < len ? src[i + k] : 0xFFu);
            const uint32_t got = reg32(run + i);
            if (got == want) continue;
            if (bad == 0)
            {
                firstOff = i;
                while (firstOff < len && reinterpret_cast<const volatile uint8_t*>(run)[firstOff] == src[firstOff]) firstOff++;
            }
            if (bad < 8) logf("N verify: +0x%lx got %08lx want %08lx\r\n", (unsigned long)i, (unsigned long)got, (unsigned long)want);
            bad++;
        }
        if (bad)
        {
            logf("N verify: mismatch at +0x%lx: %02x, want %02x (%lu bad words of %lu)\r\n", (unsigned long)firstOff,
                 reinterpret_cast<const volatile uint8_t*>(run)[firstOff], src[firstOff], (unsigned long)bad,
                 (unsigned long)((len + 3) / 4));
            return false;
        }
    }
    logf("N verify: %lu B ok\r\n", (unsigned long)len);
    return true;
}
}   // namespace

void bench(FileSystem* fs)
{
    // the text (row lines, instance dumps, the FTFE log) goes to the SDRAM scratch, away from the D, A and N staging areas
    char* const out = reinterpret_cast<char*>(externalRamScratchBuff + 196608);
    constexpr int kOut = 16384;   // about 6 KB used
    int n = 0;
    uint32_t lcg = 12345;
    for (int i = 0; i < kN; i++)
    {   // a saw plus noise, different per channel. Spelled with fmaf: the same bits run 1 got from -Os contraction, and
        // what emu_plug.py reproduces
        lcg = lcg * 1664525u + 1013904223u;
        const float noise = float(int32_t(lcg) >> 8) * (1.0f / 8388608.0f);
        const float saw = float(2 * i - kN) * (1.0f / kN);
        s_src[0][i] = __builtin_fmaf(noise, 0.2f, saw);
        s_src[1][i] = noise * 0.8f;
    }
    for (int i = 0; i < 8; i++) s_flashCmd[i] = kFlashCmd[i];
    DISABLE_AUDIO_INTERRUPTS();
    n += snprintf(out + n, kOut - n, "plugbench 1b, abi %d.%d, %d blocks x %d frames stereo, block = 522240 cycles\r\n",
                  PLUG_ABI_MAJOR, PLUG_ABI_MINOR, kBlocks, kN);
    {
        uint32_t h = 0x811C9DC5u;
        const uint32_t* w = reinterpret_cast<const uint32_t*>(s_src);
        for (int i = 0; i < 2 * kN; i++) h = (h ^ w[i]) * 0x01000193u;
        uint32_t fpscr;
        asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
        n += snprintf(out + n, kOut - n, "input chk=%08lx (unscaled block, L then R) FPSCR=%08lx\r\n", (unsigned long)h,
                      (unsigned long)fpscr);
    }
    n += snprintf(out + n, kOut - n, "regs: SIM_FCFG1=%08lx (dflash %lu KB) FMC_PFB23CR=%08lx\r\n",
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4004804C), (unsigned long)(nvmDataBytes() / 1024),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4001F008));
    n += snprintf(out + n, kOut - n, "regs: MPU_CESR=%08lx LMEM_PCCCR=%08lx PCCRMR=%08lx SDRAM_CM0=%08lx FMC_PFB01CR=%08lx\r\n",
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4000D000),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0xE0082000),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0xE0082020),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4000F04C),
                  (unsigned long)*reinterpret_cast<volatile uint32_t*>(0x4001F004));
    n += snprintf(out + n, kOut - n, "regs: SMC_PMSTAT=%02x SMC_PMCTRL=%02x SIM_CLKDIV1=%08lx FSTAT=%02x", reg8(0x4007E003),
                  reg8(0x4007E001), (unsigned long)reg32(0x40048044), *kFstat);
    s_log = out;
    s_logN = n;
    s_logCap = kOut;
    logProt();
    logf("\r\n");
    n = s_logN;
    {
        const plug_desc* d = plug_entry_dirt(hostTable(), (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
        const Err e = checkDesc(d, 0, 0, PLUG_KIND_MASTER_INSERT);
        n += row(out + n, kOut - n, "F", uintptr_t(d ? d->process : nullptr), e, 0, d);
    }
    n += loadedAt(fs, out + n, kOut - n, "L", sramSlot(), uintptr_t(sramSlot()));
    uint8_t* heap = static_cast<uint8_t*>(malloc(kSlotBytes + 16));
    if (heap)
    {
        uint8_t* u = reinterpret_cast<uint8_t*>((uintptr_t(heap) + 15) & ~uintptr_t(15));
        n += loadedAt(fs, out + n, kOut - n, "U", u, uintptr_t(u));
        free(heap);
    }
    n += loadedAt(fs, out + n, kOut - n, "D", externalRamScratchBuff, uintptr_t(externalRamScratchBuff));
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
        n += snprintf(out + n, kOut - n, "conv int16<->float stereo block: %lu cycles\r\n", (unsigned long)dt);
    }
    // N: opt-in (/plugbench.nvm): text programmed into the last two 4 KB sectors of the data flash, rw in SRAM
    if (fs->exists("/plugbench.nvm"))
    {
        const uint32_t dbytes = nvmDataBytes();
        const uint32_t off = dbytes >= 2 * kSector ? dbytes - 2 * kSector : 0;
        uint32_t bad = 0;
        const bool blank = dbytes >= 2 * kSector && blankCheck(off, 2 * kSector, &bad);
        if (!blank)
            n += snprintf(out + n, kOut - n, "N skipped: data flash %lu B, target not blank (0x%05lx)\r\n", (unsigned long)dbytes,
                          (unsigned long)bad);
        else
        {
            s_nvmLo = off;
            s_nvmHi = off + 2 * kSector;
            s_cmds = s_fails = 0;
            s_nvmProgCycles = 0;
            s_logN = n;
            logf("N target 0x%08lx..0x%08lx (data flash %lu KB, blank)\r\n", (unsigned long)(kNvm + off),
                 (unsigned long)(kNvm + off + 2 * kSector - 1), (unsigned long)(dbytes / 1024));
            static uint8_t rw[256];
            Place pl;
            pl.textDst = externalRamScratchBuff + 65536;   // staging, rebased for the flash address
            pl.textLen = 2 * kSector;
            pl.textRun = kNvm + off;
            pl.rwDst = rw;
            pl.rwLen = sizeof(rw);
            pl.rwRun = uintptr_t(rw);
            pl.commit = nvmCommit;
            Loaded l;
            const uint32_t t0 = cyc();
            const Err e = load(fs, "/plugbench.plg", pl, PLUG_KIND_MASTER_INSERT, l);
            const uint32_t t1 = cyc();
            n = s_logN;
            n += row(out + n, kOut - n, "N", pl.textRun, e, t1 - t0, l.desc);
            s_logN = n;
            s_step = "erase_back";
            const bool back = ftfe(0x09, off, nullptr, true) && ftfe(0x09, off + kSector, nullptr, true);
            flushCaches();
            const bool blankAgain = blankCheck(off, 2 * kSector, &bad);
            n = s_logN;
            n += snprintf(out + n, kOut - n,
                          "N program_cyc=%lu (core at 90 MHz inside each command) erase_back=%d blank_after=%d cmds=%lu "
                          "fails=%lu\r\n",
                          (unsigned long)s_nvmProgCycles, int(back), int(blankAgain), (unsigned long)s_cmds,
                          (unsigned long)s_fails);
            s_nvmLo = s_nvmHi = 0;   // no command may name the data flash after this
        }
    }
    memset(sramSlot(), 0, kSlotBytes);
    ENABLE_AUDIO_INTERRUPTS();
    s_log = nullptr;
    out[n < kOut ? n : kOut - 1] = 0;
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
    n = snprintf(out, kOut, "A alias=0x%08lx data_match=%d\r\n", (unsigned long)alias, int(same));
    if (same) n += loadedAt(fs, out + n, kOut - n, "A", dst, alias);
    *cm0 = oldCm;
    syncCode();
    ENABLE_AUDIO_INTERRUPTS();
    writeOut(fs, out, true);
}

}   // namespace plug

#endif   // FEATURE_PLUG_BENCH
