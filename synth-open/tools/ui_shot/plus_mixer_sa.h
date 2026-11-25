// ui_shot PLATFORM=IMX only (Makefile, mixer.o): mixer.cpp compiles with the real static_assert, as on the unit. mtStructs.h comes in first
// with static_assert still blanked, because its strInstrument size assert (372 B) holds only for 32 bit pointers, not on a 64 bit host. Its
// include guard keeps it out later, then static_assert is the keyword again for mixer.h and mixer.cpp (AudioMixer<N> N <= kMaxInputs)
#pragma once
#include "mtStructs.h"
#undef static_assert
