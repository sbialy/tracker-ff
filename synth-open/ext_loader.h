// Extension module loader rules (FEATURE_EXT_HOOKS): plain C++ templates over a small file operations type, so
// tools/ext_loader_test.cpp runs the very code ext_module.cpp runs on the card, on an in-memory card.
//
//   Fs: bool exists(const char*), bool remove(const char*), bool writeText(const char* path, const char* text, int n),
//       bool open(const char*), bool read(uint8_t* dst, uint32_t n) (exactly n bytes), void close()
//
// boot() in develop mode, in this order (nothing is read or written outside develop mode):
//   1. crash marker kMarker found: the module crashed the last boot before kMarkerMs of clean running. Remove it, load nothing.
//   2. no kPath: nothing to load.
//   3. header (magic, ABI, CPU, kind EXT, size bounds), no rebase table, the layout inside the slot's load part, the run address
//      in reserved[1], then the file CRC over manifest and image, then the image CRC in reserved[0] (the costly one, last).
//   4. bss cleared, kMarker written, the entry called. A non zero entry result removes the marker again.
// Every develop boot ends with one line in kStatus.
#pragma once

#include "plug_index.h"   // checkHeader, checkLayout, crc32
#include "ext_abi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace ext
{
constexpr const char* kPath = "/Plugins/Ext/module.plg";
constexpr const char* kMarker = "/Plugins/Ext/.loading";   // written before the entry, removed after kMarkerMs of clean running
constexpr const char* kStatus = "/Plugins/Ext/status.txt";
constexpr uint32_t kMarkerMs = 3000;

enum class Status : uint8_t
{
    kOff,          // no develop mode: not looked at
    kNone,         // no module file
    kCrashed,      // the marker of the last boot was there: skipped once
    kHeader,       // not a plugin file, another ABI, CPU or kind, sizes out of bounds
    kLayout,       // does not fit the slot, or carries a rebase table
    kRead,         // short read
    kCrc,          // file CRC mismatch (damaged)
    kOtherBase,    // linked for another run address
    kOtherImage,   // linked against another image
    kEntry,        // the entry refused (returned non zero)
    kLoaded,
};

inline const char* statusText(Status s)
{
    switch (s)
    {
    case Status::kOff: return "off";
    case Status::kNone: return "none: no module file";
    case Status::kCrashed: return "skipped: the module crashed the last boot";
    case Status::kHeader: return "refused: not an extension module of this ABI";
    case Status::kLayout: return "refused: does not fit the slot";
    case Status::kRead: return "refused: read error";
    case Status::kCrc: return "refused: damaged file (CRC)";
    case Status::kOtherBase: return "refused: linked for another address";
    case Status::kOtherImage: return "refused: built for another firmware image";
    case Status::kEntry: return "refused: the module entry failed";
    case Status::kLoaded: return "loaded";
    }
    return "?";
}

struct Slot
{
    uint8_t* mem;        // where text, data and bss are written
    uint32_t loadBytes;  // EXT_LOAD_BYTES on the device: the tail after it is the module's reset surviving area
    uintptr_t run;       // the address the module must be linked for (the device: mem)
};

// Steps 2 and 3: the file into the slot. h is the header on return (also on a refusal, as far as it was read).
template <class Fs, class ImageCrc> Status load(Fs& fs, const Slot& s, ImageCrc imageCrc, plug_file_header& h)
{
    if (!fs.open(kPath)) return Status::kNone;
    Status st = Status::kLoaded;
    memset(&h, 0, sizeof(h));
    if (!fs.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) || plug::checkHeader(h, EXT_PLUG_KIND) != plug::kOk)
        st = Status::kHeader;
    else if (h.rebase_count != 0 || plug::checkLayout(h, s.loadBytes, 0) != plug::kOk)
        st = Status::kLayout;
    else if (h.reserved[1] != uint32_t(s.run))
        st = Status::kOtherBase;
    uint32_t crc = 0;
    for (uint32_t done = sizeof(h); st == Status::kLoaded && done < h.header_size;)
    {
        uint8_t m[32];
        const uint32_t n = h.header_size - done > sizeof(m) ? sizeof(m) : h.header_size - done;
        if (!fs.read(m, n)) st = Status::kRead;
        crc = plug::crc32(crc, m, n);
        done += n;
    }
    if (st == Status::kLoaded && !fs.read(s.mem, h.image_size)) st = Status::kRead;
    fs.close();
    if (st != Status::kLoaded) return st;
    if (plug::crc32(crc, s.mem, h.image_size) != h.crc32) return Status::kCrc;
    if (h.reserved[0] != imageCrc()) return Status::kOtherImage;
    memset(s.mem + h.image_size, 0, h.bss_size);
    return Status::kLoaded;
}

// The whole boot step. enter(h) calls the entry and returns true when it filled the hooks.
template <class Fs, class ImageCrc, class Enter> Status boot(Fs& fs, bool develop, const Slot& s, ImageCrc imageCrc, Enter enter)
{
    if (!develop) return Status::kOff;
    Status st;
    plug_file_header h;
    memset(&h, 0, sizeof(h));
    if (fs.exists(kMarker))
    {
        fs.remove(kMarker);
        st = Status::kCrashed;
    }
    else
    {
        st = load(fs, s, imageCrc, h);
        if (st == Status::kLoaded)
        {
            fs.writeText(kMarker, "ext", 3);
            if (!enter(h))
            {
                fs.remove(kMarker);
                st = Status::kEntry;
            }
        }
    }
    char line[96];
    const int n = snprintf(line, sizeof(line), "%s (module image %08lx at %08lx)\n", statusText(st),
                           static_cast<unsigned long>(h.reserved[0]), static_cast<unsigned long>(h.reserved[1]));
    fs.writeText(kStatus, line, n > 0 && n < int(sizeof(line)) ? n : 0);
    return st;
}

// Step 4's end: the main loop calls it while the marker is set. True once the marker is removed (kMarkerMs of clean running).
template <class Fs> bool markerExpired(Fs& fs, uint32_t sinceMs, uint32_t nowMs)
{
    if (nowMs - sinceMs < kMarkerMs) return false;
    fs.remove(kMarker);
    return true;
}
}   // namespace ext
