#pragma once
// FTFE PGM8 (cmd 0x07) data placement, shared by the bench and its host check (synth-open/tools/plug_fccob_check.cpp).
// RM, FTFE command FCCOB: FCCOB4..7 hold the first longword (FCCOB4 = bits 31-24, FCCOB7 = bits 7-0) and FCCOB8..B
// the second. The longword lands in flash as a little endian 32 bit word, so source byte 0 (flash byte +0) belongs in
// FCCOB7, not FCCOB4. Teensy's eeprom.c does the same by storing a uint32_t at &FTFL_FCCOB7.
#include <stdint.h>

// The byte for FCCOB(4 + n), n = 0..7, from the 8 source bytes in flash order (what the flash must read back)
inline uint8_t pgm8Fccob(const uint8_t* data8, int n)
{
    return data8[(n & ~3) + 3 - (n & 3)];
}
