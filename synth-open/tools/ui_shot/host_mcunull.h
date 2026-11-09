// Host only, NOT firmware. Included into selected firmware objects by `make MCU_NULL=1` (Makefile).
// On the MCU address 0 is readable (the flash vector table), so a read through a null `char*` is harmless there and a SEGV on the Mac.
// The hidden cList controls of the Tracker UI do exactly that when a screenshot refresh updates them (ref/sim-parity.md). This makes the
// Mac read them the way the MCU does, so the rest of the device path of a console shot can run on the host.
#pragma once
#include <string.h>
static inline size_t host_mcu_strlen(const char* s) { return s ? strlen(s) : 0; }
#define strlen host_mcu_strlen
