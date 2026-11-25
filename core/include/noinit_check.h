#pragma once
// __NOINIT(EXTERNAL_RAM) objects live in a NOLOAD section: the linker script gives the SDRAM no load image, so nothing may be stored into it
// at start up. Two things break that, and both are only found by the GCC 10.3 image build (the host sims and the 12.3 syntax pass never
// assemble the object):
//   - a non-zero initialiser (`as`: "attempt to store non-zero value in section .noinit.$EXTERNAL_RAM"), a default member initialiser
//     of the type counts, that was ModSlot in SynthAutomation.cpp
//   - a constructor (a _GLOBAL__sub_I_* function that touches 0x7xxxxxxx before the SDRAM is up, the f563f27 boot hang)
// NOINIT_PLAIN(T) next to the declaration turns both into a compile error on every compiler. T may be an array type. A type that needs
// its fields set goes in as raw bytes (alignas + unsigned char[sizeof(T)]) and is built with placement new where it is used.
#include <type_traits>

#define NOINIT_PLAIN(T) \
    static_assert(std::is_trivially_default_constructible_v<std::remove_all_extents_t<T>>, \
                  "__NOINIT(EXTERNAL_RAM): no default member initialiser and no constructor (see core/include/noinit_check.h)")
