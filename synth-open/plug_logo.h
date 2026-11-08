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
// RAM_G (round G): the free window between the last stock bitmap (CHIP med ends at 128,780, core/libs/NewDisplay/display.cpp) and
// the controls cache at 150,000 (displayStructs.h controlsRamStartAddress) is a cache, not a table: 32 list image slots (580 B) and
// 2 editor image slots (1,080 B), 20,720 of 21,220 B, each filled from the card on the main loop when an image is wanted and
// reused least recently used first (LogoCache below). Before round G it held the 12 first engines' records, the others showed text.
// The picker shows 7 rows and plug_synth.cpp prefetches the 15 around the selection, the editor shows one image: the slot that is
// written over is never one on screen (drawing touches its slot, kLogoMiniSlots > 15 + 7 rows of the last frame). Nothing is read
// from the card while the display list is built or in the audio interrupt: bitmapAt only looks the slot up.
//
// Card time per load (estimate, measured on the device by the "logo loads" line of /Plugins/synth.txt): open and read one
// 1,676 B record (a sidecar, else the .plg trailer after a seek) on the 4 bit SDHC bus, FAT lookup in /Plugins/Synth included,
// about 2 to 5 ms, plus 580 or 1,080 B over the FT812 SPI (about 0.3 ms). Only the patch editor's image and a picker list longer
// than 32 engines load while the UI runs, the boot fills the list slots of the first 32 engines as before.
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
constexpr int kLogoMiniSlots = 32;          // list images (picker)
constexpr int kLogoMedSlots = 2;            // editor images (one on screen, the next one)
constexpr uint32_t kLogoMedRamG = kLogoRamG + uint32_t(kLogoMiniSlots) * kLogoMiniBytes;
static_assert(kLogoRamG % 4 == 0 && kLogoMiniBytes % 4 == 0 && kLogoMedBytes % 4 == 0, "RAM_G writes are whole words");
static_assert(kLogoMedRamG + uint32_t(kLogoMedSlots) * kLogoMedBytes <= kLogoRamGEnd, "the logo pool stays below the controls cache");

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

// What the card holds for one engine: probed once, a bad or missing record is not read again this session
struct LogoState
{
    bool probed;
    int8_t src;   // LogoSource of the last probe
    int8_t err;   // LogoErr why the last source tried was refused
};

// The RAM_G slots: two pools (list images, editor images), each slot owned by one library entry or free (-1), least recently used goes
// first. Platform free (plug_logo_test fills it with 20 fake engines). All of it runs on the main loop (bitmapAt only finds and touches).
struct LogoCache
{
    int16_t miniOwner[kLogoMiniSlots], medOwner[kLogoMedSlots];
    uint32_t miniUsed[kLogoMiniSlots], medUsed[kLogoMedSlots];
    uint32_t tick;
    uint32_t loads, loadMs, loadMsMax;   // card loads this session and their time (synth.txt)

    void reset()
    {
        for (int i = 0; i < kLogoMiniSlots; i++) miniOwner[i] = -1, miniUsed[i] = 0;
        for (int i = 0; i < kLogoMedSlots; i++) medOwner[i] = -1, medUsed[i] = 0;
        tick = loads = loadMs = loadMsMax = 0;
    }
    int slots(bool med) const { return med ? kLogoMedSlots : kLogoMiniSlots; }
    int16_t* owner(bool med) { return med ? medOwner : miniOwner; }
    const int16_t* owner(bool med) const { return med ? medOwner : miniOwner; }
    uint32_t* used(bool med) { return med ? medUsed : miniUsed; }
    int find(int entry, bool med) const
    {
        for (int i = 0; i < slots(med); i++)
            if (owner(med)[i] == entry) return i;
        return -1;
    }
    void touch(int slot, bool med) { used(med)[slot] = ++tick; }
    int victim(bool med) const
    {
        int v = 0;
        for (int i = 0; i < slots(med); i++)
        {
            if (owner(med)[i] < 0) return i;
            if ((med ? medUsed : miniUsed)[i] < (med ? medUsed : miniUsed)[v]) v = i;
        }
        return v;
    }
    static uint32_t address(int slot, bool med)
    {
        return med ? kLogoMedRamG + uint32_t(slot) * kLogoMedBytes : kLogoRamG + uint32_t(slot) * kLogoMiniBytes;
    }
    int resident(bool med) const
    {
        int n = 0;
        for (int i = 0; i < slots(med); i++) n += owner(med)[i] >= 0;
        return n;
    }
};

// The slot of entry's list (med false) or editor (med true) image, loaded from the card when it is not in RAM_G: logoFind into rec
// (kLogoRecord + 1 bytes), then ram.write(pixels, bytes, address) of the half that is wanted. -1: the engine has no good record (its
// name is drawn instead), that answer is kept in st so the card is asked once. Fs as logoFind, Ram:
//   void write(const uint8_t* p, uint32_t n, uint32_t ramG)
// now() gives milliseconds for the load time (synth.txt).
template <class Fs, class Ram, class Now>
int logoLoad(LogoCache& c, int entry, bool med, Fs& fs, Ram& ram, Now now, const char* dir, const char* plgFile, uint32_t trailerOff,
             uint8_t* rec, LogoState& st)
{
    int s = c.find(entry, med);
    if (s >= 0)
    {
        c.touch(s, med);
        return s;
    }
    if (st.probed && st.src == kLogoNone) return -1;
    const uint32_t t0 = now();
    LogoErr why = kLogoOk;
    const LogoSource src = logoFind(fs, dir, plgFile, trailerOff, rec, &why);
    st.probed = true;
    st.src = int8_t(src);
    st.err = int8_t(why);
    if (src == kLogoNone) return -1;
    s = c.victim(med);
    c.owner(med)[s] = -1;   // (the old owner's id draws nothing from here on)
    ram.write(rec + kLogoHeader + (med ? kLogoMiniBytes : 0u), med ? kLogoMedBytes : kLogoMiniBytes, LogoCache::address(s, med));
    c.owner(med)[s] = int16_t(entry);
    c.touch(s, med);
    const uint32_t ms = now() - t0;
    c.loads++;
    c.loadMs += ms;
    c.loadMsMax = ms > c.loadMsMax ? ms : c.loadMsMax;
    return s;
}

// The list images of entries[0..n) (the picker rows around the selection, nearest first): the ones already in RAM_G are touched first
// (they become the most recent), then the others are loaded with load(entry) (logoLoad). So a load never takes the slot of an image
// wanted now, and with n + the rows of the last frame <= kLogoMiniSlots never one on screen.
template <class Load>
void logoPrefetch(LogoCache& c, const int* entries, int n, Load load)
{
    for (int i = 0; i < n; i++)
    {
        const int s = c.find(entries[i], false);
        if (s >= 0) c.touch(s, false);
    }
    for (int i = 0; i < n; i++) load(entries[i]);
}

}   // namespace plug
