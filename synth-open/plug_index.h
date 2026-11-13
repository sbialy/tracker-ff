// The plugin index of the host (FEATURE_PLUG_HOST): the directory scan of /Plugins/Master and the checks on a .plg header
// that need no target. Header only and free of the platform, so synth-open/tools/plug_index_test.cpp runs the same code on
// the Mac against the real .plg files. plug_host.cpp is the only firmware user.
#pragma once

#include "plug/plug_abi.h"
#include "plug_sidecar.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace plug
{

enum Err : int32_t
{
    kOk = 0,
    kNoFile,
    kHeader,     // magic / header size
    kAbi,        // abi major, min_host, cpu, kind, an unsupported flag (PLUG_F_S16)
    kTooBig,     // image + bss over the slot
    kRead,
    kCrc,
    kRebase,     // a rebase offset or the word it names is outside the image
    kEntry,      // entry returned NULL (the plugin refused this host)
    kDesc,       // descriptor or one of its pointers outside the image, wrong kind, struct too small
    kCommit,     // Place::commit failed (the flash erase, program or verify of a FlexNVM text segment)
    kInit,       // no memory for the instance or init() failed
    kStale,      // the file is not the one the index listed (Place::wantUid / wantCrc): the folder changed since the scan
    kNoRoom,     // fits the slot alone, not beside the plugin the other insert runs (the two share the 8 KB slot)
};

inline uint32_t crc32(uint32_t c, const uint8_t* p, uint32_t n)
{
    c = ~c;
    while (n--)
    {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

inline Err checkHeader(const plug_file_header& h, uint32_t wantKind)
{
    if (h.magic != PLUG_MAGIC || h.header_size < sizeof(h)) return kHeader;
    if (h.abi_major != PLUG_ABI_MAJOR || h.min_host > PLUG_HOST_LEVEL || h.cpu != PLUG_CPU_CM4F || h.kind != wantKind ||
        (h.flags & PLUG_F_S16))
        return kAbi;
    // the CRC does not cover these 64 bytes: bound every size before any sum (no wrap with a damaged file)
    if (h.header_size > 1024 || h.image_size > 65536 || h.bss_size > 65536 || h.rebase_count > 16384) return kTooBig;
    return kOk;
}

// The segment geometry of a checked header against a slot: text (code, constants) is [0, rw_offset), rw is
// [rw_offset, image_size) + bss. textLen is the text room, rwLen the separate rw room (0: rw follows the text in textLen).
inline Err checkLayout(const plug_file_header& h, uint32_t textLen, uint32_t rwLen)
{
    const uint32_t rwOff = h.rw_offset ? h.rw_offset : h.image_size;
    const uint32_t rw = h.image_size - rwOff + h.bss_size;
    if ((h.image_size & 3u) || (rwOff & 7u) || rwOff > h.image_size || h.rebase_count > h.image_size / 4 ||
        h.entry_offset >= rwOff || rwOff > textLen || (rwLen ? rw > rwLen : rwOff + rw > textLen))
        return kTooBig;
    return kOk;
}

// ---- the two master inserts share one 8 KB SRAM_L slot (plug_host.cpp): insert 1 from the bottom, insert 2 from the top, each
// image + bss in one piece (text, then rw, then bss). Neither ever touches the flash. A pair fits when insert 1's bytes end at or
// below the start of insert 2's, which is 16 byte aligned (down) so its rw and bss stay aligned like at the slot start.
constexpr uint32_t kInsertRoom = 8192;
inline uint32_t insertFoot(const plug_file_header& h)
{
    return h.image_size + h.bss_size;
}
inline uint32_t insertFoot(uint32_t imageSize, uint32_t bssSize)
{
    return imageSize + bssSize;
}
// Start of insert 2's bytes in the slot (foot <= room)
inline uint32_t insertTopAt(uint32_t foot, uint32_t room = kInsertRoom)
{
    return (room - foot) & ~15u;
}
// Where insert k (0 or 1) goes for foot bytes while the other insert holds otherFoot bytes (0: nothing loaded). at: offset in
// the slot, len: the room the loader may fill from there. False: no room beside the other one (or more than the slot).
inline bool insertPlace(int k, uint32_t foot, uint32_t otherFoot, uint32_t& at, uint32_t& len, uint32_t room = kInsertRoom)
{
    at = len = 0;
    if (!foot || foot > room || otherFoot > room) return false;
    if (k == 0)
    {
        const uint32_t end = otherFoot ? insertTopAt(otherFoot, room) : room;
        if (foot > end) return false;
        len = end;
        return true;
    }
    at = insertTopAt(foot, room);
    if (at < otherFoot) return false;
    len = room - at;
    return true;
}
inline bool insertPairFits(uint32_t foot1, uint32_t foot2, uint32_t room = kInsertRoom)
{
    uint32_t at, len;
    return insertPlace(0, foot1, 0, at, len, room) && insertPlace(1, foot2, foot1, at, len, room);
}

// What the host keeps of the shared slot (plug_host.cpp apply, unload and tick), the refuse and retry rules in one place:
//   place():  before any card read. False: no room beside the other insert, the slot shows Full and remembers the size.
//   loaded(): the load went through: insert 1 holds [0, its file's image + bss), insert 2 [at, end of slot).
//   freed():  unloaded (the audio interrupt no longer runs it): its bytes are free.
//   retry():  a refused insert that fits now (tick sets it pending, it loads like a pick).
struct InsertPair
{
    uint32_t foot[2];       // bytes held from the bottom (insert 1) or from the top (insert 2), 0: nothing loaded
    uint32_t want[2];       // a refused insert's image + bss
    bool noRoom[2];

    bool place(int k, uint32_t f, uint32_t& at, uint32_t& len)
    {
        noRoom[k] = !insertPlace(k, f, foot[1 - k], at, len);
        want[k] = noRoom[k] ? f : 0;
        return !noRoom[k];
    }
    void loaded(int k, uint32_t at, uint32_t fileFoot) { foot[k] = k == 0 ? fileFoot : kInsertRoom - at; }
    void freed(int k) { foot[k] = 0; }
    void clearRefusal(int k) { noRoom[k] = false; want[k] = 0; }
    bool retry(int k) const
    {
        uint32_t at, len;
        return noRoom[k] && insertPlace(k, want[k], foot[1 - k], at, len);
    }
};

// A rebase table word: the offset of a whole aligned word inside the image. By subtraction: off + 4 wraps for 0xFFFFFFFC
inline bool rebaseOffsetOk(uint32_t off, uint32_t imageSize)
{
    return !(off & 3u) && imageSize >= 4 && off <= imageSize - 4;
}

// A plugin file name: *.plg in any case (FAT keeps the case it was written with), no macOS AppleDouble "._" twin
inline bool isPlugName(const char* name)
{
    const size_t l = strlen(name);
    return l > 4 && l < 40 && !(name[0] == '.' && name[1] == '_') && strcasecmp(name + l - 4, ".plg") == 0;
}

constexpr int kScanMax = 272;  // names listed per scan (the synth library of 256, plug_host.h kLibMax): rejected files take a place too
// The master insert index keeps every insert a scan lists. A project names its inserts by uid and name (plug_sidecar.h), never by
// position, so nothing stored bounds the count: the scan does. Per entry: IndexEntry in the SDRAM, a picker row (plug_ui.cpp), a
// host.txt line (plug_host.cpp kStatusMax)
constexpr int kIndexMax = kScanMax;
struct IndexEntry
{
    uint32_t uid, version;
    char name[kNameLen];
    char file[40];               // name in /Plugins/Master
    uint32_t imageSize, rwOffset, bssSize;
    uint16_t numParams;
    uint32_t crc;                // header crc32: the identity of the file (the synth bank reinstalls an engine whose file changed)
    uint32_t entry;              // entry_offset
    uint32_t logoOff;            // file offset of a logo trailer (plug_manifest.reserved[0], plug_logo.h), 0 for none
    // A synth engine's numbers from the manifest (build_plug.py --synth, PLUG_SYNTH_MANIFEST): the host lists an engine that is
    // not installed by them. stored -1: the file has none (built for an older host), it cannot be listed until it is rebuilt.
    int32_t synthStored;         // plug_manifest.reserved[1] when reserved[4] is PLUG_SYNTH_MANIFEST
    int32_t synthPatch;          // reserved[2]: the .patch engine id
    int32_t synthType;           // reserved[3]: the type id
};

// What the last scan saw, for /Plugins/host.txt
struct ScanStats
{
    int listed;                  // *.plg names the directory listing returned
    int rejected;
    struct
    {
        char file[24];
        int32_t err;
    } reject[3];                 // the first three rejected files and why
};

// Scans dir into index[0..cap), the files of one kind (/Plugins/Master: master inserts, /Plugins/Synth: engines). Fs:
//   void list(const char* dir, bool (*filter)(const char*), char* names, int namesSize, int maxNameLen, int& count,
//             char** list)    FileSystem::listDirContents(kFiles) rules: count is the room in list on the way in, the
//                             number of names on the way out
//   int read(const char* path, void* buf, int cap)    bytes read from the start of the file, -1 when it does not open
// names holds kScanMax * 40 bytes, nameList kScanMax pointers. Returns the entry count.
template <class Fs>
int scanIndex(Fs& fs, const char* dir, IndexEntry* index, int cap, char* names, char** nameList, ScanStats& st,
              uint32_t kind = PLUG_KIND_MASTER_INSERT)
{
    memset(&st, 0, sizeof(st));
    int n = kScanMax;
    fs.list(dir, isPlugName, names, kScanMax * 40, 40, n, nameList);
    st.listed = n;
    int count = 0;
    for (int i = 0; i < n && count < cap; i++)
    {
        char p[64];
        snprintf(p, sizeof(p), "%s/%s", dir, nameList[i]);
        struct
        {
            plug_file_header h;
            plug_manifest m;
        } hm;
        memset(&hm, 0, sizeof(hm));
        const int got = fs.read(p, &hm, int(sizeof(hm)));
        Err err = got < 0 ? kNoFile : (got < int(sizeof(plug_file_header)) ? kHeader : checkHeader(hm.h, kind));
        if (err != kOk)
        {
            if (st.rejected < 3)
            {
                snprintf(st.reject[st.rejected].file, sizeof(st.reject[0].file), "%s", nameList[i]);
                st.reject[st.rejected].err = err;
            }
            st.rejected++;
            continue;
        }
        IndexEntry& e = index[count];
        e.uid = hm.h.uid;
        e.version = hm.h.version;
        e.imageSize = hm.h.image_size;
        e.rwOffset = hm.h.rw_offset;
        e.bssSize = hm.h.bss_size;
        e.crc = hm.h.crc32;
        e.entry = hm.h.entry_offset;
        const bool man = hm.h.header_size >= sizeof(hm) && got == int(sizeof(hm)) && hm.m.struct_size >= sizeof(plug_manifest);
        e.numParams = man ? hm.m.num_params : 0;
        e.logoOff = man && hm.m.reserved[0] >= hm.h.header_size ? hm.m.reserved[0] : 0;
        const bool syn = man && hm.m.reserved[4] == PLUG_SYNTH_MANIFEST && int32_t(hm.m.reserved[1]) >= 0;
        e.synthStored = syn ? int32_t(hm.m.reserved[1]) : -1;
        e.synthPatch = syn ? int32_t(hm.m.reserved[2]) : -1;
        e.synthType = syn ? int32_t(hm.m.reserved[3]) : -1;
        copyName(e.name, man && hm.m.name[0] ? hm.m.name : nameList[i], man ? kNameLen : int(strlen(nameList[i])) - 4);
        snprintf(e.file, sizeof(e.file), "%s", nameList[i]);
        int dup = -1;   // two files with one uid: the newer version wins
        for (int j = 0; j < count && dup < 0; j++)
            if (index[j].uid == e.uid) dup = j;
        if (dup < 0)
            count++;
        else if (index[dup].version < e.version)
            index[dup] = e;
    }
    return count;
}

// ------------------------------------------------------------------------------------------------ scan cache
// A cheap signature of a plugin folder: one walk of the directory entries, no file is opened (a scan opens and reads every
// plugin file). It covers the count, the names and the sizes of the *.plg entries in directory order, so an added, removed,
// renamed, reordered or resized plugin changes it. A file rewritten in place at the same size does not: the USB storage
// session and the card change hooks invalidate the cache (every way a computer writes the card), and the install path checks
// the file it opens against the listed uid and crc (kStale). Fs:
//   bool dirOpen(const char* dir)
//   bool dirNext(char* name, uint32_t& size, bool& skip)    name: room for 256 bytes (FatFs FF_LFN_BUF + 1), skip: a folder or
//                                                            a hidden or system entry. False at the end or on a card error
//   void dirClose()
struct DirSig
{
    bool ok;          // the folder opened
    uint32_t count;   // *.plg entries
    uint32_t hash;    // crc32 over name, NUL, size (little endian) of each, in directory order
};
constexpr int kDirNameMax = 256;

template <class Fs>
DirSig dirSignature(Fs& fs, const char* dir)
{
    DirSig s = { fs.dirOpen(dir), 0, 0 };
    if (!s.ok) return s;
    char name[kDirNameMax];
    uint32_t size = 0;
    bool skip = false;
    while (fs.dirNext(name, size, skip))
    {
        name[kDirNameMax - 1] = 0;
        if (skip || !isPlugName(name)) continue;
        const uint8_t sz[4] = { uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24) };
        s.hash = crc32(crc32(s.hash, reinterpret_cast<const uint8_t*>(name), uint32_t(strlen(name)) + 1), sz, 4);
        s.count++;
    }
    fs.dirClose();
    return s;
}

// Whether a scan is needed: the first one, after invalidate() (a USB storage session, a card change, a stale install), or when the
// folder's signature differs from the one taken before the last scan. A folder that does not open always rescans (cheap then).
struct ScanCache
{
    bool valid;
    DirSig sig;
    void invalidate() { valid = false; }
    bool fresh(const DirSig& now) const
    {
        return valid && now.ok && sig.ok && now.count == sig.count && now.hash == sig.hash;
    }
    void store(const DirSig& now)   // the signature taken BEFORE the scan: a change during the scan shows up next time
    {
        sig = now;
        valid = now.ok;
    }
};

// rescan() of the host: the index of dir again when the cache says the folder may have changed. True when it scanned (count set)
template <class Fs>
bool refreshIndex(Fs& fs, ScanCache& cache, const char* dir, IndexEntry* index, int cap, char* names, char** nameList,
                  ScanStats& st, int& count, uint32_t kind = PLUG_KIND_MASTER_INSERT)
{
    const DirSig sig = dirSignature(fs, dir);
    if (cache.fresh(sig)) return false;
    count = scanIndex(fs, dir, index, cap, names, nameList, st, kind);
    cache.store(sig);
    return true;
}

}   // namespace plug
