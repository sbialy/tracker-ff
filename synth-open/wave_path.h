// The path of a wave file as a patch stores it and a plugin names it (PLUG_EXT_FILES, ABI 1.4 tail): relative to the card folder /Waves,
// '/' between folders. Header only and free of every other include, so the .patch codec (open_patchio.cpp) and plug_files.h use the same rule.
#pragma once

#include <stddef.h>

namespace plug
{

constexpr size_t kWavePathMax = 48;   // PLUG_FILES_PATH: bytes with the NUL, so 47 characters

// True for a path the host will open under /Waves: 5 to 47 printable ASCII characters ending in ".wav" (any case), no leading or trailing '/',
// no empty segment and no segment that starts with '.' (".", "..", hidden and "._" AppleDouble names), none of \ : * ? " < > | (not valid in a FAT name, and '\' and ':' would walk out of the folder on a host
// that treats them as separators). The card is not asked: a path that passes may name a file that is not there.
inline bool wavePathOk(const char* p)
{
    if (!p)
    {
        return false;
    }
    size_t n = 0;
    size_t seg = 0;   // length of the current segment
    for (; p[n]; n++)
    {
        if (n >= kWavePathMax - 1)
        {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(p[n]);
        if (c < 0x20 || c > 0x7E || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            return false;
        }
        if (c == '/')
        {
            if (seg == 0)
            {
                return false;   // leading '/' or an empty segment
            }
            seg = 0;
            continue;
        }
        if (seg == 0 && c == '.')
        {
            return false;       // ".", "..", hidden and AppleDouble names: the list skips them too
        }
        seg++;
    }
    if (n < 5 || seg == 0)
    {
        return false;
    }
    const char* e = p + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'w' && (e[2] | 0x20) == 'a' && (e[3] | 0x20) == 'v';
}

}   // namespace plug
