// FlexNVM installer for master insert 2 (FEATURE_PLUG_HOST). See plug_host.h.
//
// The text of insert 2 runs from the last two 4 KB sectors of the 128 KB data flash (0x1001E000, K66 RM 32.1 "additional code
// store"). Device bench run 3 (PLUGD, d448039) proved this exact path: ERSSCR and PGM8 with address bit 23 set, HSRUN left
// around every command (K66 flash cannot be erased or programmed in HSRUN, run 1), the launch and wait loop run from RAM
// like Teensy's eeprom.c, PGM8 data with FCCOB4 as the most significant byte of each longword (plug_fccob.h, run 2), then
// verify. Rules: only ERSSCR (0x09) and PGM8 (0x07), only the 128 KB data flash (insert 2 in [kNvmRun, kNvmRun + kNvmBytes),
// the synth bank of plug_bank.h below it), never PGMPART, SETRAM or a program flash address, the FlexRAM EEPROM is never
// touched. A load whose bytes are already in the flash issues no command.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "plug_bank.h"
#include "plug_fccob.h"

#include <string.h>

// The audio block clock (platforms/mk66/3rdparty/Audio/output_i2s.cpp): blocks done, DWT at the end of the last one and its length
extern "C" volatile uint32_t ff_audio_blocks;
extern "C" volatile uint32_t ff_audio_end_cyc;
extern "C" volatile uint32_t ff_audio_isr_cyc;

namespace plug
{

namespace
{
// Install crackle (install-crackle, ~/prj/poly/build/install-crackle/REPORT.md). Every flash command used to run with all
// interrupts off and the core out of HSRUN, which gave two faults on the audio output:
//  1. the I2S MCLK comes from the system clock (output_i2s.cpp MCLK_SRC 0 at 180 MHz, 180 MHz * 16 / 255), so dividing the core
//     by two for RUN halved the MCLK, the bit clock and the codec's sample rate for as long as each command took (57 us per
//     PGM8, 14 ms per sector erase, 3,500 PGM8 for a 28 KB engine): the sound slowed down in thousands of short bursts.
//  2. one sector erase (14.4 ms measured, from the 1.2.0 synth.txt rows) held the audio interrupt off for five 2.9 ms blocks,
//     so the DMA played its two halves over and over.
// Now the MCLK divider moves with the core divider (the same 11.29 MHz in RUN), and an erase is cut into slices with the FTFE
// erase suspend (FCNFG ERSSUSP): each slice starts right after an audio block ends, lasts what the next block can spare
// (sliceCycles) and goes back to HSRUN with the interrupts on before the next one. Only the erase is cut: a PGM8 is short.
constexpr uintptr_t kNvmBase = 0x10000000u;
constexpr uint32_t kSector = 4096;
volatile uint8_t* const kFstat = reinterpret_cast<volatile uint8_t*>(0x40020000);
volatile uint8_t* const kFcnfg = reinterpret_cast<volatile uint8_t*>(0x40020001);
constexpr uint8_t kErssusp = 0x10;
volatile uint32_t* const kClkdiv1 = reinterpret_cast<volatile uint32_t*>(0x40048044);   // SIM_CLKDIV1
volatile uint8_t* const kPmctrl = reinterpret_cast<volatile uint8_t*>(0x4007E001);      // SMC_PMCTRL
volatile uint8_t* const kPmstat = reinterpret_cast<volatile uint8_t*>(0x4007E003);      // SMC_PMSTAT
volatile uint32_t* const kI2sMcr = reinterpret_cast<volatile uint32_t*>(0x4002F100);    // I2S0_MCR
volatile uint32_t* const kI2sMdr = reinterpret_cast<volatile uint32_t*>(0x4002F104);    // I2S0_MDR
constexpr uint8_t kPmHsrun = 0x80;
// RUN dividers, the ones of teensy_core kinetis_hsrun_disable for 180 / 90 MHz: core 90, bus 90, FlexBus 90, flash 22.5 MHz
constexpr uint32_t kRunDiv = (1u << 28) | (1u << 24) | (1u << 20) | (7u << 16);
constexpr uint32_t kCpuHz = 180000000u;

inline volatile uint8_t& fccob(int n)
{   // FCCOB0..B at 0x40020004.. in big endian groups of four (kinetis.h FTFL_FCCOBn: FCCOB0 = 0x40020007)
    static const uint8_t kOff[12] = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8};
    return reinterpret_cast<volatile uint8_t*>(0x40020004)[kOff[n]];
}

uint32_t s_cmds;
uint64_t s_cmdCycles;   // DWT cycles inside the commands (HSRUN left: the core runs at 90 MHz there, not 180)
volatile uint32_t* const kDwtCyccnt = reinterpret_cast<volatile uint32_t*>(0xE0001004);
NvmStats s_st;
bool s_legacy;   // nvmLegacy: the 1.2.0 way (A/B measurements only)

// Launches (or resumes) the command in FCCOB and waits for CCIF. budget: DWT cycles after which an erase is suspended (0: never).
// Runs from RAM like Teensy's eeprom.c do_flash_cmd: nothing may be fetched from the flash while it works.
__attribute__((section(".fastrun"), noinline, noclone)) uint8_t ramCommand(uint32_t budget)
{
    volatile uint8_t* const fstat = reinterpret_cast<volatile uint8_t*>(0x40020000);
    volatile uint8_t* const fcnfg = reinterpret_cast<volatile uint8_t*>(0x40020001);
    volatile uint32_t* const cyc = reinterpret_cast<volatile uint32_t*>(0xE0001004);
    const uint32_t c0 = *cyc;
    *fstat = 0x80u;   // clear CCIF: launch, or resume a suspended erase (ERSSUSP still set)
    while (!(*fstat & 0x80u))
        if (budget && *cyc - c0 >= budget)
        {
            *fcnfg = uint8_t(*fcnfg | kErssusp);   // suspend at the next suspend point, CCIF comes back then (or when done)
            while (!(*fstat & 0x80u)) {}
            break;
        }
    return *fstat;
}

// Leaves HSRUN (interrupts are off): the core divider first, then the MCLK fraction doubled in the same few cycles, so the I2S keeps
// its 11.29 MHz (only when the MCLK comes from the system clock and the doubled fraction is valid: MCR MICS 0, FRACT + 1 <= 128)
struct Clocks
{
    uint32_t div, mdr;
    bool left, mclk;
};
inline void waitDuf()
{
    for (uint32_t spin = 0; (*kI2sMcr & 0x80000000u) && spin < 100000u; spin++) {}   // MCR DUF: the MCLK divider is updating
}
void leaveHsrun(Clocks& c)
{
    c.div = *kClkdiv1;
    c.mdr = *kI2sMdr;
    c.left = c.mclk = false;
    if (*kPmstat != kPmHsrun) return;
    const uint32_t fract = (c.mdr >> 12) & 0xFFu, divide = c.mdr & 0xFFFu;
    c.mclk = !s_legacy && ((*kI2sMcr >> 24) & 3u) == 0 && (*kI2sMcr & (1u << 30)) && 2 * (fract + 1) <= 256u &&
             2 * (fract + 1) <= divide + 1;
    const uint32_t mdr2 = ((2 * fract + 1) << 12) | divide;
    if (c.mclk) waitDuf();
    *kClkdiv1 = kRunDiv;
    if (c.mclk) *kI2sMdr = mdr2;
    *kPmctrl = 0;   // RUNM 0
    while (*kPmstat == kPmHsrun) {}
    c.left = true;
}
void backToHsrun(const Clocks& c)
{
    if (c.left)
    {
        *kPmctrl = 0x60;   // RUNM 3: HSRUN
        while (*kPmstat != kPmHsrun) {}
        if (c.mclk) waitDuf();
    }
    if (*kClkdiv1 != c.div) *kClkdiv1 = c.div;
    if (c.mclk) *kI2sMdr = c.mdr;
}

void flushCaches()
{
    syncCode();   // LMEM code cache
    *reinterpret_cast<volatile uint32_t*>(0x4001F004) |= 0x00F80000u;   // FMC_PFB01CR CINV_WAY | S_B_INV
    *reinterpret_cast<volatile uint32_t*>(0x4001F008) |= 0x00F80000u;   // FMC_PFB23CR
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
}

bool valid(uint8_t cmd, uint32_t nvmOff)
{
    return (cmd == 0x09 || cmd == 0x07) && nvmOff < uint32_t(kNvmRun - kNvmBase) + kNvmBytes && !(nvmOff & 7u) &&
           !(cmd == 0x09 && (nvmOff & (kSector - 1)));
}

void loadCmd(uint8_t cmd, uint32_t nvmOff, const uint8_t* data8)
{
    const uint32_t a = 0x800000u | nvmOff;   // bit 23: data flash
    for (uint32_t spin = 0; !(*kFstat & 0x80u) && spin < 10000000u; spin++) {}
    *kFstat = 0x70u;   // clear RDCOLERR ACCERR FPVIOL (write 1)
    fccob(0) = cmd;
    fccob(1) = uint8_t(a >> 16);
    fccob(2) = uint8_t(a >> 8);
    fccob(3) = uint8_t(a);
    if (data8)
        for (int i = 0; i < 8; i++) fccob(4 + i) = pgm8Fccob(data8, i);
}

// One masked window: out of HSRUN, launch or resume, back. Returns FSTAT.
uint8_t window(uint32_t budget)
{
    Clocks c;
    leaveHsrun(c);
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
    const uint32_t c0 = *kDwtCyccnt;
    const uint8_t after = ramCommand(budget);
    s_cmdCycles += *kDwtCyccnt - c0;
    backToHsrun(c);
    return after;
}

// DWT cycles of one interrupts-off stretch into the stats (180 MHz cycles outside the command, 90 MHz inside it)
void masked(uint32_t c0)
{
    const uint32_t dc = *kDwtCyccnt - c0;
    if (dc > s_st.maskMaxCyc) s_st.maskMaxCyc = dc;
}

// The whole command in one window (PGM8, the legacy erase, an erase while no audio block runs)
bool ftfe(uint8_t cmd, uint32_t nvmOff, const uint8_t* data8)
{
    if (!valid(cmd, nvmOff)) return false;
    s_cmds++;
    asm volatile("cpsid i" ::: "memory");
    const uint32_t c0 = *kDwtCyccnt;
    loadCmd(cmd, nvmOff, data8);
    const uint8_t after = window(0);
    masked(c0);
    asm volatile("cpsie i" ::: "memory");
    return (after & 0xF1u) == 0x80u;   // CCIF, no ACCERR FPVIOL RDCOLERR MGSTAT0
}

// Waits (interrupts on) until an audio block has just ended: true then, false when no block came for 8 ms (no audio running)
bool afterBlock()
{
    const uint32_t t0 = *kDwtCyccnt;
    uint32_t b = ff_audio_blocks;
    for (;;)
    {
        const uint32_t nb = ff_audio_blocks;
        if (nb != b)
        {
            if (*kDwtCyccnt - ff_audio_end_cyc < kCpuHz / 1000000u * 200u) return true;   // within 0.2 ms of its end
            b = nb;   // a later interrupt held us up: wait for the next one
        }
        if (*kDwtCyccnt - t0 > kCpuHz / 1000u * 8u) return false;
    }
}

// The slice the next block can spare, in DWT cycles at 90 MHz (RUN). The next audio interrupt waits for the slice, and must still end
// before the DMA reaches its half 5.8 ms after the last one started: slice <= 5.8 ms - 2 x block - 0.2 ms. Taken as 5 ms - 2.5 x the
// last block, kept within 0.3 .. 2 ms.
uint32_t sliceCycles()
{
    const uint32_t blockUs = ff_audio_isr_cyc / (kCpuHz / 1000000u);
    const int32_t us = 5000 - int32_t(blockUs * 5 / 2);
    const uint32_t lim = us < 300 ? 300u : (us > 2000 ? 2000u : uint32_t(us));
    return lim * 90u;
}

bool blank(uintptr_t run, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 4)
        if (*reinterpret_cast<const volatile uint32_t*>(run + i) != 0xFFFFFFFFu) return false;
    return true;
}

// ERSSCR in slices between audio blocks: launch, then resume after each block until the FTFE ends it without a suspend. While it is
// suspended the flash reads normally, so the audio interrupt runs the engines and insert 2 from the other sectors (the one being
// erased holds nothing attached: bankInstall detaches before it erases, insert 2 is detached while it is rewritten, plug_host.cpp apply).
// No audio block within 8 ms, or 120 ms of erase time (the K66 sector erase is 14 ms typical, 113 ms at most): the suspended erase
// is aborted and done again in one window, the 1.2.0 way. Same when the sector does not read blank afterwards.
bool eraseSliced(uint32_t nvmOff)
{
    if (!valid(0x09, nvmOff)) return false;
    if (s_legacy) return ftfe(0x09, nvmOff, nullptr);
    s_st.erases++;
    uint64_t spent = 0;
    bool started = false, ok = false;
    for (int n = 0; n < 2000; n++)
    {
        const bool audio = afterBlock();
        if (!audio || spent > 90000ull * 120u)
        {
            if (audio) s_st.fallbacks++;
            else s_st.noAudio++;
            *kFcnfg = uint8_t(*kFcnfg & ~kErssusp);   // abort a suspended erase: the next launch starts the command anew
            ok = ftfe(0x09, nvmOff, nullptr);
            started = false;
            break;
        }
        asm volatile("cpsid i" ::: "memory");
        const uint32_t m0 = *kDwtCyccnt;
        if (!started)
        {
            s_cmds++;
            loadCmd(0x09, nvmOff, nullptr);
            *kFcnfg = uint8_t(*kFcnfg & ~kErssusp);
        }
        const uint32_t c0 = *kDwtCyccnt;
        const uint8_t after = window(sliceCycles());
        spent += *kDwtCyccnt - c0;
        const bool suspended = (*kFcnfg & kErssusp) != 0;
        masked(m0);
        asm volatile("cpsie i" ::: "memory");
        started = true;
        s_st.slices++;
        if (after & 0x71u)
        {   // ACCERR FPVIOL RDCOLERR MGSTAT0: never leave a suspended erase behind (the next launch would resume it)
            *kFcnfg = uint8_t(*kFcnfg & ~kErssusp);
            return false;
        }
        if (!suspended)
        {
            ok = true;
            started = false;
            break;
        }
    }
    if (started)
    {   // 2,000 slices without an end: abort, one window
        s_st.fallbacks++;
        *kFcnfg = uint8_t(*kFcnfg & ~kErssusp);
        ok = ftfe(0x09, nvmOff, nullptr);
    }
    flushCaches();
    if (ok && !blank(kNvmBase + nvmOff, kSector))
    {
        s_st.fallbacks++;
        ok = ftfe(0x09, nvmOff, nullptr);
        flushCaches();
    }
    return ok;
}
}   // namespace

uint32_t nvmCommands()
{
    return s_cmds;
}

uint64_t nvmCommandCycles()
{
    return s_cmdCycles;
}

const NvmStats& nvmStats()
{
    return s_st;
}

void nvmStatsReset()
{
    s_st = NvmStats{};
}

void nvmLegacy(bool on)
{
    s_legacy = on;
}

// The synth bank: only below insert 2's sectors
bool nvmErase(uint32_t off)
{
    const bool ok = off < kBankBytes && eraseSliced(off);
    flushCaches();
    return ok;
}

bool nvmProgram(uint32_t off, const uint8_t* data8)
{
    const bool ok = off + 8 <= kBankBytes && ftfe(0x07, off, data8);
    flushCaches();
    return ok;
}

bool nvmInstall(const uint8_t* src, uintptr_t run, uint32_t len)
{
    if (run < kNvmRun || (run & 7u) || len > kNvmBytes || run + len > kNvmRun + kNvmBytes) return false;
    flushCaches();
    if (memcmp(reinterpret_cast<const void*>(run), src, len) == 0) return true;   // already there: no flash wear
    const uint32_t off = uint32_t(run - kNvmBase);
    for (uint32_t s = off & ~(kSector - 1); s < off + len; s += kSector)
        if (!blank(kNvmBase + s, kSector) && !eraseSliced(s)) return false;   // ERSSCR
    flushCaches();
    if (!blank(run, (len + 7u) & ~7u)) return false;
    for (uint32_t i = 0; i < len; i += 8)
    {
        uint8_t ph[8];
        for (uint32_t k = 0; k < 8; k++) ph[k] = i + k < len ? src[i + k] : 0xFF;
        if (!ftfe(0x07, off + i, ph)) return false;   // PGM8
    }
    flushCaches();
    return memcmp(reinterpret_cast<const void*>(run), src, len) == 0;   // verify
}

}   // namespace plug

#endif   // FEATURE_PLUG_HOST
