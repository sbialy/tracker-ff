#ifndef LIBS_NEWDISPLAY_INFLATE_H_
#define LIBS_NEWDISPLAY_INFLATE_H_

#include <stddef.h>
#include <stdint.h>

// Raw deflate (RFC 1951) decoder for the boot and font switch paths (the deflated fonts, display.cpp decompressFont). Small, not fast: a
// canonical Huffman code is decoded one bit at a time. Reads the whole stream from memory (flash), writes straight into dst, which is also
// the window of the back references, so nothing else is allocated except the scratch tables below (no stack, no heap).
// It never reads past src + srcLen nor writes past dst + dstCap: a bad stream returns a negative value, never a fault.
struct InflateScratch
{
    uint16_t lcount[16], lsym[288];    // literal / length code
    uint16_t dcount[16], dsym[30];     // distance code
    uint16_t lengths[320];             // code lengths while a dynamic block header is read
};

// The one set of tables of the firmware (SDRAM, display.cpp): the fonts and the random names use it, one after the other on the main loop
extern InflateScratch gInflateScratch;

// Returns the number of bytes written, or -1 (bad stream), -2 (output buffer too small), -3 (stream ends early)
int inflateRaw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap, InflateScratch* scratch);

#endif
