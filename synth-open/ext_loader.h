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
// Every develop boot ends with one line in kStatus. A refusal by the image id (kOtherImage) appends what this image computed
// (the Diag callable of boot), so the card alone says which part of the flash differs from the one the module was packed for.
#pragma once

#include "plug_index.h"   // checkHeader, checkLayout, crc32
#include "ext_abi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace ext
{
constexpr const char* kPath = "/Plugins/Ext/module.plg";
constexpr const char* kExtrasPath = "/Plugins/Ext/extras.plg";   // the release modules (MOD/IT, ...): loaded on first use, never at boot
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
    const char* path = kPath;   // the module file (extras.plg is loaded by the same rules from kExtrasPath into the release region)
    ext_rel_table_t* table = nullptr;   // extras.plg: its sub-slot table (after the manifest) is required, checked and copied here
};

// The sub-slot table of extras.plg (ext_abi.h ext_rel_table_t) against the file's header and the region it is loaded into: this version,
// every tenant at an EXT_SUB_ALIGN boundary in order without overlap, the first at 0, text, data and bss inside used_bytes, used_bytes
// inside the region. False refuses the file.
inline bool checkRelTable(const ext_rel_table_t& t, const plug_file_header& h, uint32_t regionBytes)
{
    if (t.magic != EXT_REL_TABLE_MAGIC || t.version != EXT_REL_TABLE_VERSION || t.count != EXT_SUB_COUNT || t.region_bytes != regionBytes)
        return false;
    uint32_t end = 0;
    for (uint32_t i = 0; i < EXT_SUB_COUNT; i++)
    {
        const ext_sub_t& u = t.sub[i];
        if (u.at % EXT_SUB_ALIGN || u.at != end || u.bytes % EXT_SUB_ALIGN || u.bytes > regionBytes - u.at) return false;
        end = u.at + u.bytes;
    }
    return end == t.used_bytes && end <= regionBytes && h.image_size + h.bss_size <= end;
}

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
    if (!fs.open(s.path)) return Status::kNone;
    Status st = Status::kLoaded;
    memset(&h, 0, sizeof(h));
    if (!fs.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) || plug::checkHeader(h, EXT_PLUG_KIND) != plug::kOk)
        st = Status::kHeader;
    else if (h.rebase_count != 0 || plug::checkLayout(h, s.loadBytes, 0) != plug::kOk)
        st = Status::kLayout;
    else if (h.reserved[1] != uint32_t(s.run))
        st = Status::kOtherBase;
    // extras.plg: the sub-slot table sits right after the 96 B manifest and ends the header part (a round 1 file has none: not this ABI)
    constexpr uint32_t kTableAt = sizeof(h) + 96;
    if (st == Status::kLoaded && s.table && h.header_size != kTableAt + sizeof(ext_rel_table_t)) st = Status::kHeader;
    uint32_t crc = 0;
    for (uint32_t done = sizeof(h); st == Status::kLoaded && done < h.header_size;)
    {
        uint8_t m[32];
        const uint32_t n = h.header_size - done > sizeof(m) ? sizeof(m) : h.header_size - done;
        if (!fs.read(m, n)) st = Status::kRead;
        crc = plug::crc32(crc, m, n);
        if (s.table)
            for (uint32_t i = 0; i < n; i++)
                if (done + i >= kTableAt && done + i < kTableAt + sizeof(ext_rel_table_t))
                    reinterpret_cast<uint8_t*>(s.table)[done + i - kTableAt] = m[i];
        done += n;
    }
    if (st == Status::kLoaded && s.table && !checkRelTable(*s.table, h, s.loadBytes)) st = Status::kLayout;
    if (st == Status::kLoaded && !fs.read(s.mem, h.image_size)) st = Status::kRead;
    fs.close();
    if (st != Status::kLoaded) return st;
    if (plug::crc32(crc, s.mem, h.image_size) != h.crc32) return Status::kCrc;
    if (h.reserved[0] != imageCrc()) return Status::kOtherImage;
    memset(s.mem + h.image_size, 0, h.bss_size);
    return Status::kLoaded;
}

// No extra status text.
struct NoDiag
{
    const char* operator()(Status) const { return ""; }
};

// The whole boot step. enter(h) calls the entry and returns true when it filled the hooks. diag(status) returns text for the
// end of the status line (kOtherImage: the image id this firmware computed, in parts).
template <class Fs, class ImageCrc, class Enter, class Diag = NoDiag>
Status boot(Fs& fs, bool develop, const Slot& s, ImageCrc imageCrc, Enter enter, Diag diag = Diag())
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
    char line[256];
    const int n = snprintf(line, sizeof(line), "%s (module image %08lx at %08lx)%s\n", statusText(st),
                           static_cast<unsigned long>(h.reserved[0]), static_cast<unsigned long>(h.reserved[1]), diag(st));
    fs.writeText(kStatus, line, n > 0 && n < int(sizeof(line)) ? n : 0);
    return st;
}

// The release modules (extras.plg): the same file rules, a second file, a second SDRAM region of its own (ext_abi.h EXT_REL_BYTES, image symbol
//   s_extRel). The console module's slot is never touched, so both are loaded at the same time in any order (the console at the end of the
//   key window of a develop boot, extras.plg on the first use of one of its tenants). Every tenant sits at the sub-slot the file's own table
//   names (ext_abi.h ext_rel_table_t, checked by checkRelTable), the bundle is one load, one file CRC and one image id check for all of them. The recovery console keeps the first bytes of the console slot.
enum class ModitState : uint8_t
{
    kReady,        // loaded, the hook is set
    kMissing,      // no /Plugins/Ext/extras.plg
    kOtherImage,   // built for another firmware image
    kDamaged,      // anything else: not a module, damaged, too big, linked elsewhere, the entry refused
};

// where the recovery console may write its reply (ext_recovery.h, kReplyBytes): the console slot start, idle while no console module runs
constexpr uint32_t kRecoveryReplyBytes = 320;

// A module's hooks into the image's table, field by field: a null field keeps what an earlier module set, so the console module and
// extras.plg never wipe each other's pointers. struct_size stays the image's.
inline void mergeHooks(ext_hooks_t& dst, const ext_hooks_t& src)
{
    if (src.update) dst.update = src.update;
    if (src.usbSof) dst.usbSof = src.usbSof;
    if (src.usbLineCoding) dst.usbLineCoding = src.usbLineCoding;
    if (src.spinExpired) dst.spinExpired = src.spinExpired;
    if (src.coprocReset) dst.coprocReset = src.coprocReset;
    if (src.log) dst.log = src.log;
    if (src.logv) dst.logv = src.logv;
    if (src.modit) dst.modit = src.modit;
    if (src.capture) dst.capture = src.capture;
    if (src.theme) dst.theme = src.theme;
    if (src.selftest) dst.selftest = src.selftest;
}

inline ModitState moditStateOf(Status st)
{
    switch (st)
    {
    case Status::kLoaded: return ModitState::kReady;
    case Status::kNone: return ModitState::kMissing;
    case Status::kOtherImage: return ModitState::kOtherImage;
    default: return ModitState::kDamaged;
    }
}

// The two lines of the popup of the click that needs the module (empty for kReady). Short: the popup is one screen line each.
inline const char* moditLine1(ModitState s)
{
    switch (s)
    {
    case ModitState::kMissing: return "MOD/IT module missing";
    case ModitState::kOtherImage: return "extras.plg is for another firmware";
    case ModitState::kDamaged: return "extras.plg is damaged";
    default: return "";
    }
}
inline const char* moditLine2(ModitState s)
{
    switch (s)
    {
    case ModitState::kMissing: return "Copy extras.plg to Plugins/Ext";
    case ModitState::kOtherImage: return "Copy the one of this release";
    case ModitState::kDamaged: return "Copy it from the release again";
    default: return "";
    }
}

// Load /Plugins/Ext/extras.plg into the release region on the first use of a tenant. s is the region (path set here). enter(h) calls the
// entry and returns true when it filled the hook of the tenant asked for. No crash marker (never loaded at boot: a crash costs one reset,
// the next use tries again), no status file.
template <class Fs, class ImageCrc, class Enter>
ModitState extrasLoad(Fs& fs, const Slot& s, ImageCrc imageCrc, Enter enter, ext_rel_table_t* table)
{
    Slot m = s;
    m.path = kExtrasPath;
    m.table = table;
    plug_file_header h;
    const Status st = load(fs, m, imageCrc, h);
    if (st != Status::kLoaded) return moditStateOf(st);
    return enter(h) ? ModitState::kReady : ModitState::kDamaged;
}

// Step 4's end: the main loop calls it while the marker is set. True once the marker is removed (kMarkerMs of clean running).
template <class Fs> bool markerExpired(Fs& fs, uint32_t sinceMs, uint32_t nowMs)
{
    if (nowMs - sinceMs < kMarkerMs) return false;
    fs.remove(kMarker);
    return true;
}
}   // namespace ext
