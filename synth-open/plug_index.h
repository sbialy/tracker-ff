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

// A plugin file name: *.plg in any case (FAT keeps the case it was written with), no macOS AppleDouble "._" twin
inline bool isPlugName(const char* name)
{
    const size_t l = strlen(name);
    return l > 4 && l < 40 && !(name[0] == '.' && name[1] == '_') && strcasecmp(name + l - 4, ".plg") == 0;
}

constexpr int kIndexMax = 16;
constexpr int kScanMax = 24;   // names listed per scan: rejected files take a place too
struct IndexEntry
{
    uint32_t uid, version;
    char name[kNameLen];
    char file[40];               // name in /Plugins/Master
    uint32_t imageSize, rwOffset, bssSize;
    uint16_t numParams;
    uint32_t crc;                // header crc32: the identity of the file (the synth bank reinstalls an engine whose file changed)
    uint32_t entry;              // entry_offset
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

}   // namespace plug
