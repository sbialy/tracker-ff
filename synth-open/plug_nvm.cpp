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

// The audio output (platforms/mk66/3rdparty/Audio/output_i2s.cpp): blocks done
extern "C" volatile uint32_t ff_audio_blocks;
extern "C" volatile uint32_t ff_audio_lead;          // blocks written after the one the DMA plays
extern "C" volatile uint32_t ff_audio_lead_target;   // what the audio interrupt renders ahead (1 normally)

namespace plug
{

namespace
{
// Install crackle (install-crackle, ~/prj/poly/build/install-crackle/REPORT.md). Every flash command used to run with all
// interrupts off and the core out of HSRUN, which gave two faults on the audio output:
//  1. the I2S MCLK comes from the system clock (output_i2s.cpp MCLK_SRC 0 at 180 MHz, 180 MHz * 16 / 255), so dividing the core
//     by two for RUN halved the MCLK, the bit clock and the codec's sample rate for as long as each command took (about 57 us
//     per PGM8, 1,000 to 3,500 of them per engine): the sound slowed down in thousands of short bursts.
//  2. one sector erase holds every interrupt off for 6.3 to 7.4 ms (measured on the unit), two to three audio blocks, and the
//     DMA replayed its two halves. The FlexNVM is one flash block and the engines run from it, so the audio interrupt cannot
//     run during an erase. The FTFE erase suspend does not help: slices of 1 or 2 ms never finish an erase, 4 ms slices finish
//     in two but only fit between audio blocks below about 30 % audio load (measured, REPORT.md).
// Now the MCLK divider moves with the core divider (the same 11.29 MHz in RUN), and before an erase the audio interrupt renders
// ahead (output_i2s.cpp, an 8 block ring): with 5 blocks (14.5 ms) written the DMA plays through the erase.
constexpr uintptr_t kNvmBase = 0x10000000u;
constexpr uint32_t kSector = 4096;
volatile uint8_t* const kFstat = reinterpret_cast<volatile uint8_t*>(0x40020000);
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

// Launches the command in FCCOB and waits for CCIF. Runs from RAM like Teensy's eeprom.c do_flash_cmd: nothing may be fetched
// from the flash while it works.
__attribute__((section(".fastrun"), noinline, noclone)) uint8_t ramCommand()
{
    volatile uint8_t* const fstat = reinterpret_cast<volatile uint8_t*>(0x40020000);
    *fstat = 0x80u;   // clear CCIF: launch
    while (!(*fstat & 0x80u)) {}
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

// One masked window: out of HSRUN, launch, back. Returns FSTAT.
uint8_t window()
{
    Clocks c;
    leaveHsrun(c);
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
    const uint32_t c0 = *kDwtCyccnt;
    const uint8_t after = ramCommand();
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
    const uint8_t after = window();
    masked(c0);
    asm volatile("cpsie i" ::: "memory");
    return (after & 0xF1u) == 0x80u;   // CCIF, no ACCERR FPVIOL RDCOLERR MGSTAT0
}

bool blank(uintptr_t run, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 4)
        if (*reinterpret_cast<const volatile uint32_t*>(run + i) != 0xFFFFFFFFu) return false;
    return true;
}

// Blocks the audio interrupt renders ahead before an erase: the DMA then has 5 x 2.9 ms plus the rest of the playing block,
// against 7.4 ms of erase and one block to render after it. The ring holds 8 (output_i2s.cpp, at most 6 ahead).
constexpr uint32_t kEraseLead = 5;

// ERSSCR with the audio rendered ahead. The lead builds by one block per audio interrupt (about 10 ms from the normal lead at low
// load, longer near full load). It does not matter where in a block the erase starts: with 5 blocks written after the playing one
// the DMA has at least 14.5 ms, the erase takes 7.4 at most, and the audio interrupt that was held off runs right at the cpsie,
// so the lead read here is never older than one block. No audio block for 8 ms (no audio running) or no lead after 300 ms: the
// erase goes ahead anyway (counted in the stats).
bool eraseAhead(uint32_t nvmOff)
{
    if (!valid(0x09, nvmOff)) return false;
    if (s_legacy) return ftfe(0x09, nvmOff, nullptr);
    s_st.erases++;
    ff_audio_lead_target = kEraseLead;
    const uint32_t t0 = *kDwtCyccnt;
    uint32_t b = ff_audio_blocks, tb = t0;
    bool audio = true, ready = false;
    for (;;)
    {
        if (ff_audio_lead >= kEraseLead)
        {
            ready = true;
            break;
        }
        const uint32_t now = *kDwtCyccnt, nb = ff_audio_blocks;
        if (nb != b)
        {
            b = nb;
            tb = now;
        }
        else if (now - tb > kCpuHz / 1000u * 8u)
        {
            audio = false;
            break;
        }
        if (now - t0 > kCpuHz / 1000u * 300u) break;
    }
    const uint32_t waitUs = (*kDwtCyccnt - t0) / (kCpuHz / 1000000u);
    if (waitUs > s_st.waitMaxUs) s_st.waitMaxUs = waitUs;
    if (ready) s_st.leadOk++;
    else if (audio) s_st.leadShort++;
    else s_st.noAudio++;
    const uint32_t lead = ff_audio_lead;
    if (lead < s_st.leadMin) s_st.leadMin = lead;
    const bool ok = ftfe(0x09, nvmOff, nullptr);
    ff_audio_lead_target = 1;   // drains back to the normal latency
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
    s_st.leadMin = ~0u;
}

void nvmLegacy(bool on)
{
    s_legacy = on;
}


// The synth bank: only below insert 2's sectors
bool nvmErase(uint32_t off)
{
    const bool ok = off < kBankBytes && eraseAhead(off);
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
        if (!blank(kNvmBase + s, kSector) && !eraseAhead(s)) return false;   // ERSSCR
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
