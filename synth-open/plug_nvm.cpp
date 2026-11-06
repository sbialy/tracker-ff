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

extern "C" int kinetis_hsrun_disable(void);   // teensy_core mk20dx128.c: RUN at 90 MHz core, bus and SDRAM clocks kept
extern "C" int kinetis_hsrun_enable(void);

namespace plug
{

namespace
{
constexpr uintptr_t kNvmBase = 0x10000000u;
constexpr uint32_t kSector = 4096;
volatile uint8_t* const kFstat = reinterpret_cast<volatile uint8_t*>(0x40020000);

inline volatile uint8_t& fccob(int n)
{   // FCCOB0..B at 0x40020004.. in big endian groups of four (kinetis.h FTFL_FCCOBn: FCCOB0 = 0x40020007)
    static const uint8_t kOff[12] = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8};
    return reinterpret_cast<volatile uint8_t*>(0x40020004)[kOff[n]];
}

// eeprom.c's do_flash_cmd, run from RAM: strb 0x80 to FSTAT, loop until CCIF. r0 = &FSTAT.
alignas(4) uint16_t s_flashCmd[8];
const uint16_t kFlashCmd[8] = {0xf06f, 0x037f, 0x7003, 0x7803, 0xf013, 0x0f80, 0xd0fb, 0x4770};
uint32_t s_cmds;
uint64_t s_cmdCycles;   // DWT cycles inside the commands (HSRUN left: the core runs at 90 MHz there, not 180)
volatile uint32_t* const kDwtCyccnt = reinterpret_cast<volatile uint32_t*>(0xE0001004);

void flushCaches()
{
    syncCode();   // LMEM code cache
    *reinterpret_cast<volatile uint32_t*>(0x4001F004) |= 0x00F80000u;   // FMC_PFB01CR CINV_WAY | S_B_INV
    *reinterpret_cast<volatile uint32_t*>(0x4001F008) |= 0x00F80000u;   // FMC_PFB23CR
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
}

bool ftfe(uint8_t cmd, uint32_t nvmOff, const uint8_t* data8)
{
    if ((cmd != 0x09 && cmd != 0x07) || nvmOff >= uint32_t(kNvmRun - kNvmBase) + kNvmBytes || (nvmOff & 7u) ||
        (cmd == 0x09 && (nvmOff & (kSector - 1))))
        return false;
    const uint32_t a = 0x800000u | nvmOff;   // bit 23: data flash
    if (s_flashCmd[0] != kFlashCmd[0]) memcpy(s_flashCmd, kFlashCmd, sizeof(kFlashCmd));
    s_cmds++;
    asm volatile("cpsid i" ::: "memory");
    for (uint32_t spin = 0; !(*kFstat & 0x80u) && spin < 10000000u; spin++) {}
    *kFstat = 0x70u;   // clear RDCOLERR ACCERR FPVIOL (write 1)
    fccob(0) = cmd;
    fccob(1) = uint8_t(a >> 16);
    fccob(2) = uint8_t(a >> 8);
    fccob(3) = uint8_t(a);
    if (data8)
        for (int i = 0; i < 8; i++) fccob(4 + i) = pgm8Fccob(data8, i);
    const uint32_t div = *reinterpret_cast<volatile uint32_t*>(0x40048044);   // SIM_CLKDIV1
    const int left = kinetis_hsrun_disable();
    asm volatile("dsb 0xf\n isb 0xf" ::: "memory");
    const uint32_t c0 = *kDwtCyccnt;
    reinterpret_cast<void (*)(volatile uint8_t*)>(uintptr_t(s_flashCmd) | 1u)(kFstat);
    s_cmdCycles += *kDwtCyccnt - c0;
    const uint8_t after = *kFstat;
    if (left) kinetis_hsrun_enable();
    if (*reinterpret_cast<volatile uint32_t*>(0x40048044) != div) *reinterpret_cast<volatile uint32_t*>(0x40048044) = div;
    asm volatile("cpsie i" ::: "memory");
    return (after & 0xF1u) == 0x80u;   // CCIF, no ACCERR FPVIOL RDCOLERR MGSTAT0
}

bool blank(uintptr_t run, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 4)
        if (*reinterpret_cast<const volatile uint32_t*>(run + i) != 0xFFFFFFFFu) return false;
    return true;
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

// The synth bank: only below insert 2's sectors
bool nvmErase(uint32_t off)
{
    const bool ok = off < kBankBytes && ftfe(0x09, off, nullptr);
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
        if (!blank(kNvmBase + s, kSector) && !ftfe(0x09, s, nullptr)) return false;   // ERSSCR
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
