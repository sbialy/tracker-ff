// ui_shot BRIDGE=1 only (emu parity plan T5, ref/emu-parity-plan.md): the FlexNVM synth bank (plug_bank.h, [0x10000000, 0x10020000), the whole 128 KB
// data flash) modelled on the bridge's guest window at its device address, in place of plug_nvm.cpp (FTFE commands, K66 registers) and of
// the host_stubs.cpp stubs a plain build keeps (no flash, nothing installs). plug_synth's bank code (plug_bank.h bankOpen and bankInstall)
// runs as on the unit: it erases and programs through nvmErase/nvmProgram and reads back through the window, the guest runs an installed
// engine's text from the same bytes, so the overlay's start() places nothing itself.
//
// The model: the bank powers up erased (0xFF), or with the bytes of UISHOT_NVM=<file> (128 KB, written back after every command: the
// bank of the unit survives a power cycle, so a sim session that names one file keeps its installs as the unit keeps them). ERSSCR (nvmErase)
// takes a 4 KB sector at a sector boundary, PGM8 (nvmProgram) an 8 byte phrase at an 8 byte boundary, as plug_nvm.cpp valid() demands. A
// program ANDs (a cell only goes 1 to 0, as the flash), a phrase programmed over 0 bits is counted (nvmOverPrograms) and logged once. Every
// command drops the translated guest blocks of every lane (ucb::codeChanged, the unit's cache invalidation after a command). Commands
// cost no time here: nvmCommandCycles stays 0, the install-crackle statistics count the erases only (the audio lead is a device matter).
#include "host_nvm.h"

#include "host_ucbridge.h"
#include "plug_bank.h"
#include "plug_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace plug
{
namespace
{
static_assert(kBankRun == ucb::kNvm && kBankBytes == ucb::kNvmSize, "the synth bank is the bridge's FlexNVM window");

uint8_t* s_nvm;           // the guest window's host bytes
const char* s_file;       // UISHOT_NVM, nullptr: the bank lives as long as the process
uint32_t s_commands, s_over;
NvmStats s_stats;

uint8_t* bank()
{
    if (s_nvm) return s_nvm;
    s_nvm = ucb::g2h(ucb::kNvm, ucb::kNvmSize);
    memset(s_nvm, 0xFF, kBankBytes);   // erased
    s_file = getenv("UISHOT_NVM");
    if (s_file && !*s_file) s_file = nullptr;
    if (s_file)
        if (FILE* f = fopen(s_file, "rb"))
        {
            const size_t n = fread(s_nvm, 1, kBankBytes, f);
            fclose(f);
            if (n != kBankBytes)
            {
                fprintf(stderr, "ui_shot: UISHOT_NVM %s holds %zu B, not %u: the bank starts erased\n", s_file, n, kBankBytes);
                memset(s_nvm, 0xFF, kBankBytes);
            }
        }
    ucb::codeChanged();
    return s_nvm;
}

// the bytes [off, off + n) back to UISHOT_NVM (the file is created whole on its first write)
void persist(uint32_t off, uint32_t n)
{
    if (!s_file) return;
    FILE* f = fopen(s_file, "r+b");
    if (!f)
    {
        f = fopen(s_file, "wb");
        if (!f) return;
        fwrite(s_nvm, 1, kBankBytes, f);
        fclose(f);
        return;
    }
    if (fseek(f, long(off), SEEK_SET) == 0) fwrite(s_nvm + off, 1, n, f);
    fclose(f);
}
}   // namespace

const uint8_t* nvmHostAt(uint32_t off)
{
    return off < kBankBytes ? bank() + off : nullptr;
}

uint32_t nvmOverPrograms()
{
    return s_over;
}

uint32_t nvmCommands()
{
    return s_commands;
}
uint64_t nvmCommandCycles()
{
    return 0;
}
const NvmStats& nvmStats()
{
    return s_stats;
}
void nvmStatsReset()
{
    s_stats = NvmStats{};
}
void nvmLegacy(bool) {}

bool nvmErase(uint32_t off)
{
    uint8_t* const b = bank();
    if (off >= kBankBytes || (off & (kBankSector - 1))) return false;
    s_commands++;
    s_stats.erases++;
    s_stats.leadOk++;
    memset(b + off, 0xFF, kBankSector);
    persist(off, kBankSector);
    ucb::codeChanged();
    return true;
}

bool nvmProgram(uint32_t off, const uint8_t* data8)
{
    uint8_t* const b = bank();
    if (off + 8 > kBankBytes || (off & 7u) || !data8) return false;
    s_commands++;
    bool over = false;
    for (int i = 0; i < 8; i++)
    {
        over = over || (data8[i] & ~b[off + i]) != 0 || b[off + i] != 0xFF;
        b[off + i] &= data8[i];
    }
    if (over && s_over++ == 0) fprintf(stderr, "ui_shot: FlexNVM phrase 0x%05x programmed while not erased (the unit cannot either)\n", off);
    persist(off, 8);
    ucb::codeChanged();
    return true;
}
}   // namespace plug
