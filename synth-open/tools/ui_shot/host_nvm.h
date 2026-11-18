// ui_shot BRIDGE=1 only (emu parity plan T5): the FlexNVM synth bank as a model behind the bridge's guest window. See host_nvm.cpp.
#pragma once

#include <stdint.h>

namespace plug
{
// The bank byte at offset off as the unit's flash reads it (kBankRun + off): a host pointer into the guest window 0x10000000, so the
// installed engine's text the guest runs and the bytes plug_bank.h reads are one copy. nullptr outside the 128 KB.
const uint8_t* nvmHostAt(uint32_t off);
// Commands that wrote a 0 bit back to 1 or programmed a phrase twice since boot (the flash cannot: the model ANDs, as the cells do)
uint32_t nvmOverPrograms();
}   // namespace plug
