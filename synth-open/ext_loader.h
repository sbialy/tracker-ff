// Extension module loader rules (FEATURE_EXT_HOOKS): plain C++ templates over a small file operations type, so
// tools/ext_loader_test.cpp runs the very code ext_module.cpp runs on the card, on an in-memory card.
//
//   Fs: bool exists(const char*), bool remove(const char*), bool writeText(const char* path, const char* text, int n),
//       bool open(const char*), bool read(uint8_t* dst, uint32_t n) (exactly n bytes), void close()
//
// boot() in develop mode, in this order (nothing is read or written outside develop mode):
//   1. crash marker kMarker found: the module crashed the last boot before kMarkerMs of clean running. Write kCrash (reset
//      reason, the module's fault record from the slot tail, the tail in hex) so a card reader shows what happened, remove the
//      marker, load nothing.
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
constexpr const char* kCrash = "/Plugins/Ext/crash.txt";    // written on the boot that finds the marker, replaced each time
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
    uint32_t resetReason = 0;   // RCM_SRS1 << 8 | RCM_SRS0 of this start (the device), what ended the boot before it
};

// The fault record a module keeps in the slot tail (ext_abi.h EXT_FAULT_*), 4 aligned anywhere in it. Its index, or -1.
inline uint32_t tailWord(const uint8_t* tail, uint32_t i)
{
    uint32_t v;
    memcpy(&v, tail + i * 4, 4);
    return v;
}
inline int findFaultRecord(const uint8_t* tail, uint32_t words)
{
    for (uint32_t i = 0; i + EXT_FAULT_WORDS <= words; i++)
        if (tailWord(tail, i) == EXT_FAULT_MAGIC && tailWord(tail, i + 5) == ~(tailWord(tail, i + 1) ^ EXT_FAULT_MAGIC))
            return int(i);
    return -1;
}

// Step 1's report: formatted into the slot's load part (free on a boot that loads nothing, no stack or SRAM for 5 KB of text),
// the tail itself is only read. RCM_SRS0 bits: 7 POR, 6 PIN, 5 WDOG, 2 LOC/LOL, 1 LVD. RCM_SRS1: 5 SACKERR, 3 SW (SYSRESETREQ,
// what the module's fault handler and restartMCU do), 2 MDM_AP, 1 LOCKUP.
template <class Fs> int crashReport(Fs& fs, const Slot& s)
{
    char* const out = reinterpret_cast<char*>(s.mem);
    const uint32_t cap = s.loadBytes;
    const uint8_t* const tail = s.mem + s.loadBytes;
    constexpr uint32_t kWords = EXT_NOINIT_BYTES / 4;
    uint32_t n = 0;
    auto put = [&](const char* fmt, auto... a) {
        const int k = snprintf(out + n, cap - n, fmt, a...);
        if (k > 0) n = n + uint32_t(k) < cap ? n + uint32_t(k) : cap - 1;
    };
    put("extension module crash report: the module was loaded on the boot before this one and the unit reset within %lu ms\n",
        static_cast<unsigned long>(kMarkerMs));
    put("reset reason RCM_SRS0 %02lx RCM_SRS1 %02lx\n", static_cast<unsigned long>(s.resetReason & 0xFF),
        static_cast<unsigned long>((s.resetReason >> 8) & 0xFF));
    const int f = findFaultRecord(tail, kWords);
    if (f >= 0)
        put("fault record at %08lx: pc %08lx lr %08lx xpsr %08lx cfsr %08lx\n",
            static_cast<unsigned long>(s.run + s.loadBytes + uint32_t(f) * 4), static_cast<unsigned long>(tailWord(tail, f + 1)),
            static_cast<unsigned long>(tailWord(tail, f + 2)), static_cast<unsigned long>(tailWord(tail, f + 3)),
            static_cast<unsigned long>(tailWord(tail, f + 4)));
    else
        put("%s", "fault record: none (the module died before its handlers were in, it reset without a fault, or the SDRAM lost it)\n");
    put("slot tail %lu bytes at %08lx (the module's map names the records):\n", static_cast<unsigned long>(EXT_NOINIT_BYTES),
        static_cast<unsigned long>(s.run + s.loadBytes));
    for (uint32_t i = 0; i < kWords; i += 8)
    {
        put("%08lx:", static_cast<unsigned long>(s.run + s.loadBytes + i * 4));
        for (uint32_t j = 0; j < 8; j++) put(" %08lx", static_cast<unsigned long>(tailWord(tail, i + j)));
        put("%s", "\n");
    }
    fs.writeText(kCrash, out, int(n));
    return f;
}

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
        crashReport(fs, s);
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
