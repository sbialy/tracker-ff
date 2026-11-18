// Host driver of font_inflate_test.py: the firmware's own inflate.cpp and the deflated font arrays of core/libs/fonts, compiled natively.
//   font_inflate_check fonts OUTDIR      decode the 10 font arrays, write OUTDIR/<name>.bin and check each is its rawSize
//   font_inflate_check cases DIR         every DIR/N.z is decoded and compared with DIR/N.raw (stored, fixed and dynamic blocks)
//   font_inflate_check fuzz              damaged and truncated font streams: no crash, no write outside the buffer (run under ASan)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "NewDisplay/inflate.h"
#include "LiberationSans-Bold_14_L4.h"
#include "LiberationSans-Bold_17_L4.h"
#include "LiberationSans-Regular_12_L4.h"
#include "LiberationSans-Regular_14_L4.h"
#include "LiberationSans-Regular_17_L4.h"
#include "RobotoMono-Bold_17_L4.h"
#include "RobotoMono-Light_14_L4.h"
#include "RobotoMono-Light_17_L4.h"
#include "RobotoMono-Regular_12_L4.h"
#include "RobotoMono-Regular_14_L4.h"

struct Font
{
    const char* name;
    const uint8_t* z;
    size_t zLen;
    int raw;
};
#define F(file, sym) {file, sym##_deflated, sizeof(sym##_deflated), sym##_rawSize}
static const Font kFonts[] = {
    F("LiberationSans-Bold_14_L4", LiberationSans_Bold_14_L4),       F("LiberationSans-Bold_17_L4", LiberationSans_Bold_17_L4),
    F("LiberationSans-Regular_12_L4", LiberationSans_Regular_12_L4), F("LiberationSans-Regular_14_L4", LiberationSans_Regular_14_L4),
    F("LiberationSans-Regular_17_L4", LiberationSans_Regular_17_L4), F("RobotoMono-Bold_17_L4", Roboto_Mono_Bold_17_L4),
    F("RobotoMono-Light_14_L4", Roboto_Mono_Light_14_L4),            F("RobotoMono-Light_17_L4", Roboto_Mono_Light_17_L4),
    F("RobotoMono-Regular_12_L4", Roboto_Mono_Regular_12_L4),        F("RobotoMono-Regular_14_L4", Roboto_Mono_Regular_14_L4),
};
static const size_t kMaxFontBufferSize = 22396;   // display.cpp

static std::vector<uint8_t> slurp(const std::string& p)
{
    std::vector<uint8_t> v;
    if (FILE* f = fopen(p.c_str(), "rb"))
    {
        uint8_t b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
        fclose(f);
    }
    return v;
}

int main(int argc, char** argv)
{
    InflateScratch* sc = new InflateScratch;
    int bad = 0;
    if (argc == 3 && !strcmp(argv[1], "fonts"))
    {
        for (const Font& f : kFonts)
        {
            // exactly the size of display.cpp's buffer, on the heap so ASan sees an overrun
            uint8_t* out = static_cast<uint8_t*>(malloc(kMaxFontBufferSize));
            int n = inflateRaw(f.z, f.zLen, out, kMaxFontBufferSize, sc);
            printf("%-30s deflated %5zu B -> %d B (rawSize %d)%s\n", f.name, f.zLen, n, f.raw, n == f.raw ? "" : "  MISMATCH");
            if (n != f.raw) bad++;
            else if (FILE* o = fopen((std::string(argv[2]) + "/" + f.name + ".bin").c_str(), "wb")) { fwrite(out, 1, n, o); fclose(o); }
            free(out);
        }
    }
    else if (argc == 3 && !strcmp(argv[1], "cases"))
    {
        for (int i = 0;; i++)
        {
            std::vector<uint8_t> z = slurp(std::string(argv[2]) + "/" + std::to_string(i) + ".z");
            std::vector<uint8_t> raw = slurp(std::string(argv[2]) + "/" + std::to_string(i) + ".raw");
            if (z.empty()) break;
            uint8_t* out = static_cast<uint8_t*>(malloc(raw.size() ? raw.size() : 1));
            int n = inflateRaw(z.data(), z.size(), out, raw.size(), sc);
            bool ok = n == static_cast<int>(raw.size()) && !memcmp(out, raw.data(), raw.size());
            // one byte of room less must fail with -2, never write past the end
            if (raw.size() > 1)
            {
                uint8_t* small = static_cast<uint8_t*>(malloc(raw.size() - 1));
                if (inflateRaw(z.data(), z.size(), small, raw.size() - 1, sc) != -2) ok = false;
                free(small);
            }
            printf("case %d: %zu B -> %d B %s\n", i, z.size(), n, ok ? "ok" : "FAIL");
            if (!ok) bad++;
            free(out);
        }
    }
    else if (argc == 2 && !strcmp(argv[1], "fuzz"))
    {
        uint32_t rng = 12345;
        auto next = [&]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
        size_t cases = 0, rejected = 0;
        for (const Font& f : kFonts)
        {
            std::vector<uint8_t> z(f.zLen);
            for (int k = 0; k < 400; k++)
            {
                memcpy(z.data(), f.z, f.zLen);
                size_t len = f.zLen;
                if (k % 4 == 0) len = next() % f.zLen;   // truncated
                else for (int j = 0, m = 1 + next() % 4; j < m; j++) z[next() % f.zLen] ^= 1u << (next() % 8);   // bit flips
                size_t cap = (k % 3 == 0) ? next() % kMaxFontBufferSize : kMaxFontBufferSize;
                uint8_t* in = static_cast<uint8_t*>(malloc(len ? len : 1));
                memcpy(in, z.data(), len);
                uint8_t* out = static_cast<uint8_t*>(malloc(cap ? cap : 1));
                int n = inflateRaw(in, len, out, cap, sc);
                if (n > static_cast<int>(cap)) { printf("fuzz: %d B written into %zu B\n", n, cap); bad++; }
                if (n < 0) rejected++;
                cases++;
                free(in);
                free(out);
            }
        }
        printf("fuzz: %zu damaged streams, %zu rejected, no write past the buffer\n", cases, rejected);
    }
    else
    {
        fprintf(stderr, "usage: see the top of the file\n");
        return 2;
    }
    delete sc;
    return bad ? 1 : 0;
}
