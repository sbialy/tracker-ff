// Host check (NOT firmware) of the FTFE PGM8 data byte order used by synth-open/plug_bench.cpp.
// Models the controller as the RM describes it: FCCOB4..7 = the longword programmed at addr, FCCOB4 = bits 31-24,
// FCCOB8..B = the longword at addr + 4. The flash is little endian, so a byte read at addr is bits 7-0 of the longword.
// Builds the real flash text of a .plg (header, rebase table, run address like the bench: textRun = 0x1001E000), runs
// every 8 byte phrase through pgm8Fccob() (the function the bench uses) and compares the readback with the source.
// The pre-fix mapping (FCCOB(4+i) = data8[i], run 2) is run as well and must FAIL with 29 against 09 at +0 for dirt.plg.
//   usage: plug_fccob_check <dirt.plg> [run address, default 0x1001e000]
#include "../plug_fccob.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static uint32_t rd32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24;
}
static void wr32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}

typedef uint8_t (*MapFn)(const uint8_t*, int);
static uint8_t oldMap(const uint8_t* d, int n)
{
    return d[n];   // run 2: FCCOB(4 + n) = data8[n]
}

// Program text through the model with a mapping, return the number of bad bytes, print the first few
static int program(const std::vector<uint8_t>& text, MapFn map, const char* name)
{
    const size_t len = text.size();
    std::vector<uint8_t> flash((len + 7) & ~size_t(7), 0xFF);   // erased
    for (size_t i = 0; i < flash.size(); i += 8)
    {
        uint8_t ph[8], fccob[8];   // fccob[0..7] = FCCOB4..B
        for (int k = 0; k < 8; k++) ph[k] = i + k < len ? text[i + k] : 0xFF;
        for (int k = 0; k < 8; k++) fccob[k] = map(ph, k);
        const uint32_t lw0 = uint32_t(fccob[0]) << 24 | fccob[1] << 16 | fccob[2] << 8 | fccob[3];   // FCCOB4 = bits 31-24
        const uint32_t lw1 = uint32_t(fccob[4]) << 24 | fccob[5] << 16 | fccob[6] << 8 | fccob[7];
        wr32(&flash[i], lw0);       // program only clears bits, the flash is erased (0xFF) so it equals the data
        wr32(&flash[i + 4], lw1);
    }
    int bad = 0;
    for (size_t i = 0; i < len; i++)
        if (flash[i] != text[i])
        {
            if (bad < 4) printf("  %s: mismatch at +0x%zx: %02x, want %02x\n", name, i, flash[i], text[i]);
            bad++;
        }
    printf("%s: %zu B, %d bad bytes\n", name, len, bad);
    return bad;
}

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    const uint32_t run = argc > 2 ? uint32_t(strtoul(argv[2], nullptr, 0)) : 0x1001E000u;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<uint8_t> d;
    uint8_t b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
    fclose(f);
    if (d.size() < 64 || rd32(&d[0]) != 0x31474C50u) { printf("bad header\n"); return 2; }
    const uint32_t hsz = d[4] | d[5] << 8, img = rd32(&d[0x18]), nreb = rd32(&d[0x20]), crc = rd32(&d[0x30]), rw = rd32(&d[0x34]);
    const uint32_t rwOff = rw ? rw : img;
    if (hsz + img + 4 * nreb > d.size() || crc32(&d[hsz], img + 4 * nreb) != crc) { printf("size or crc\n"); return 2; }
    std::vector<uint8_t> image(d.begin() + hsz, d.begin() + hsz + img);
    const uint32_t rwRun = 0x20010000u;   // the bench keeps rw in SRAM, any value is fine for the text bytes
    for (uint32_t k = 0; k < nreb; k++)
    {
        const uint32_t off = rd32(&d[hsz + img + 4 * k]), v = rd32(&image[off]);
        wr32(&image[off], v < rwOff ? run + v : rwRun + (v - rwOff));
    }
    std::vector<uint8_t> text(image.begin(), image.begin() + rwOff);
    printf("%s: text %zu B (rw at +0x%x), run 0x%08x, %u rebase words, first 8 B %02x %02x %02x %02x %02x %02x %02x %02x\n",
           argv[1], text.size(), rwOff, run, nreb, text[0], text[1], text[2], text[3], text[4], text[5], text[6], text[7]);
    const int oldBad = program(text, oldMap, "old mapping (run 2)");
    const int newBad = program(text, pgm8Fccob, "pgm8Fccob (fixed)");
    // the old mapping must reproduce the device result, otherwise the model is not the same bug
    const bool reproduces = oldBad > 0 && text[3] != text[0];
    printf("model reproduces run 2 (old mapping fails, byte +0 reads %02x = source byte 3, want %02x): %s\n", text[3], text[0],
           reproduces ? "yes" : "NO");
    printf("%s\n", newBad == 0 && reproduces ? "PASS" : "FAIL");
    return newBad == 0 && reproduces ? 0 : 1;
}
