// Card logos of the synth engine plugins (FEATURE_PLUG_HOST, phase 3 round B, ~/prj/poly/ref/synth-art-plan.md). Header only
// and free of the platform: synth-open/tools/plug_logo_test.cpp runs the same code on the Mac. plug_synth.cpp is the only
// firmware user.
//
// A logo record (PLGI 1) is a 16 byte header and the two FT812 L2 bitmaps the stock engine logos use, exactly as RAM_G wants
// them (2 bit alpha, pixel 0 in the top bits of a byte, the theme colour is applied at draw time):
//   0  u32 magic "PLGI"        8  u8 med width 108
//   4  u8  version 1           9  u8 med height 40
//   5  u8  format 2 (L2)      10  u16 flags 0
//   6  u8  mini width 80      12  u32 CRC-32 of the 1,660 pixel bytes
//   7  u8  mini height 29     16  mini 80x29 (stride 20, 580 B), then med 108x40 (stride 27, 1,080 B)
// Two carriers, one record:
//   - a sidecar /Plugins/Synth/<name>.plgi next to <name>.plg, the whole file is the record (1,676 B, any other size is refused)
//   - a trailer inside the .plg after the rebase table, at the file offset in plug_manifest.reserved[0] (build_plug.py --logo).
//     The .plg CRC does not cover it, the record's own CRC does. Hosts before round B never read it.
// The sidecar wins over the trailer, the trailer over the text name. A bad or missing record goes to the next source, the last
// one is the engine's name drawn as text (round A), so nothing here can stop the picker.
//
// RAM_G: the free window between the last stock bitmap (CHIP med ends at 128,780, core/libs/NewDisplay/display.cpp) and the
// controls cache at 150,000 (displayStructs.h controlsRamStartAddress) holds 12 records' pixels, one per bank entry.
#pragma once

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "plug_index.h"

namespace plug
{

constexpr uint32_t kLogoMagic = 0x49474C50u;   // "PLGI"
constexpr uint8_t kLogoVersion = 1;
constexpr uint8_t kLogoFormatL2 = 2;
constexpr int kLogoMiniW = 80, kLogoMiniH = 29, kLogoMedW = 108, kLogoMedH = 40;
constexpr uint32_t kLogoMiniStride = (kLogoMiniW + 3) / 4, kLogoMedStride = (kLogoMedW + 3) / 4;
constexpr uint32_t kLogoMiniBytes = kLogoMiniStride * kLogoMiniH;        // 580
constexpr uint32_t kLogoMedBytes = kLogoMedStride * kLogoMedH;           // 1,080
constexpr uint32_t kLogoPixels = kLogoMiniBytes + kLogoMedBytes;         // 1,660
constexpr uint32_t kLogoHeader = 16;
constexpr uint32_t kLogoRecord = kLogoHeader + kLogoPixels;              // 1,676
constexpr uint32_t kLogoRamG = 128780;      // end of the last stock bitmap
constexpr uint32_t kLogoRamGEnd = 150000;   // controlsRamStartAddress: never written
constexpr int kLogoSlots = 12;
static_assert(kLogoRamG % 4 == 0 && kLogoMiniBytes % 4 == 0 && kLogoPixels % 4 == 0, "RAM_G writes are whole words");
static_assert(kLogoRamG + kLogoSlots * kLogoPixels <= kLogoRamGEnd, "the logo pool stays below the controls cache");

struct LogoHeader
{
    uint32_t magic;
    uint8_t version, format, miniW, miniH, medW, medH;
    uint16_t flags;
    uint32_t crc;
};
static_assert(sizeof(LogoHeader) == kLogoHeader, "PLGI header is 16 bytes");

enum LogoErr : int
{
    kLogoOk = 0,
    kLogoShort,     // fewer bytes than a record (truncated file or trailer)
    kLogoBadMagic,  // magic or version
    kLogoBadSize,   // format or dimensions, or a sidecar that is not exactly one record
    kLogoBadCrc,
};

// rec holds n bytes read from the start of a record
inline LogoErr logoCheck(const uint8_t* rec, int n)
{
    if (n < int(kLogoRecord)) return kLogoShort;
    LogoHeader h;
    memcpy(&h, rec, sizeof(h));
    if (h.magic != kLogoMagic || h.version != kLogoVersion) return kLogoBadMagic;
    if (h.format != kLogoFormatL2 || h.miniW != kLogoMiniW || h.miniH != kLogoMiniH || h.medW != kLogoMedW || h.medH != kLogoMedH)
        return kLogoBadSize;
    if (crc32(0, rec + kLogoHeader, kLogoPixels) != h.crc) return kLogoBadCrc;
    return kLogoOk;
}

// "<dir>/<name without .plg>.plgi" for a .plg file name, false for anything else (and for a macOS "._" twin)
inline bool logoSidecarPath(char* out, int cap, const char* dir, const char* plgFile)
{
    const int l = int(strlen(plgFile));
    if (l <= 4 || (plgFile[0] == '.' && plgFile[1] == '_') || strcasecmp(plgFile + l - 4, ".plg") != 0) return false;
    const int n = snprintf(out, size_t(cap), "%s/%.*s.plgi", dir, l - 4, plgFile);
    return n > 0 && n < cap;
}

enum LogoSource : int
{
    kLogoNone = 0,   // text name
    kLogoSidecar,
    kLogoTrailer,
};

// The record of one plugin into rec (kLogoRecord + 1 bytes of room): the sidecar of plgFile in dir, else the trailer at
// trailerOff of <dir>/<plgFile> (0: none). Fs:
//   int readAt(const char* path, uint32_t off, void* buf, int cap)   bytes read at off, -1 when it does not open
// A sidecar is read with one byte of room past the record, so a longer file is seen. err (optional) gets why the last
// source tried was refused (kLogoShort also when there was none).
template <class Fs>
LogoSource logoFind(Fs& fs, const char* dir, const char* plgFile, uint32_t trailerOff, uint8_t* rec, LogoErr* err = nullptr)
{
    LogoErr e = kLogoShort;
    char p[64];
    if (logoSidecarPath(p, int(sizeof(p)), dir, plgFile))
    {
        const int n = fs.readAt(p, 0, rec, int(kLogoRecord) + 1);
        if (n >= 0)
        {
            e = n > int(kLogoRecord) ? kLogoBadSize : logoCheck(rec, n);
            if (e == kLogoOk) return kLogoSidecar;
        }
    }
    if (trailerOff)
    {   // plgFile is a .plg name here (logoSidecarPath refused anything else, and the scan lists no "._" names)
        snprintf(p, sizeof(p), "%s/%s", dir, plgFile);
        e = logoCheck(rec, fs.readAt(p, trailerOff, rec, int(kLogoRecord)));
        if (e == kLogoOk) return kLogoTrailer;
    }
    if (err) *err = e;
    return kLogoNone;
}

}   // namespace plug
