// Host test of the Inserts page scan cache (synth-open/plug_index.h: dirSignature, ScanCache, refreshIndex), the code plug::rescan()
// runs on the device: an unchanged /Plugins/Master is not scanned again, an added, removed, renamed or resized plugin is, and so is
// every folder after a USB storage session or a card change (plug::cardChanged() invalidates the cache). The signature walk opens no
// file. A plugin rewritten in place at the same size is the one change the signature cannot see: the install path refuses it (kStale,
// the same uid and crc compare plug::load() does) and the host rescans.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_scan_cache_test.cpp -o /tmp/plug_scan_cache_test &&
//   /tmp/plug_scan_cache_test
#include "plug_index.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace plug;

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

struct Entry
{
    std::string name, bytes;
    bool folder = false, hidden = false;
};

// One folder of a card. Directory order is insertion order (FAT: a new entry takes the first free slot, here the end, a removed one
// leaves a hole that the next file may take). Counts what a rescan costs: files opened by read(), directory walks.
struct FolderFs
{
    std::string dir = "/Plugins/Master";
    bool present = true;
    std::vector<Entry> e;
    int opens = 0, walks = 0;
    size_t cursor = 0;

    bool here(const char* d) const { return present && dir == d; }

    void list(const char* d, bool (*filter)(const char*), char* names, int namesSize, int maxNameLen, int& count, char** list)
    {
        if (!here(d))
        {
            count = 0;
            return;
        }
        int n = 0;
        for (const Entry& x : e)
        {
            if (n >= count) break;
            if (x.folder || x.hidden || (x.name[0] == '.' && x.name[1] == '_')) continue;
            if (filter && !filter(x.name.c_str())) continue;
            const int len = int(x.name.size()) + 1;
            if (len > namesSize) break;
            if (len > maxNameLen) continue;
            list[n++] = names;
            memcpy(names, x.name.c_str(), size_t(len));
            names += len;
            namesSize -= len;
        }
        count = n;
    }
    int read(const char* p, void* buf, int cap)
    {
        opens++;
        const std::string path = p, name = path.substr(path.rfind('/') + 1);
        for (const Entry& x : e)
            if (!x.folder && strcasecmp(x.name.c_str(), name.c_str()) == 0)
            {
                const int n = std::min(cap, int(x.bytes.size()));
                memcpy(buf, x.bytes.data(), size_t(n));
                return n;
            }
        return -1;
    }
    bool dirOpen(const char* d)
    {
        walks++;
        cursor = 0;
        return here(d);
    }
    bool dirNext(char* name, uint32_t& size, bool& skip)
    {
        if (cursor >= e.size()) return false;
        const Entry& x = e[cursor++];
        snprintf(name, kDirNameMax, "%s", x.name.c_str());
        size = uint32_t(x.bytes.size());
        skip = x.folder || x.hidden;
        return true;
    }
    void dirClose() {}

    Entry* find(const std::string& n)
    {
        for (Entry& x : e)
            if (x.name == n) return &x;
        return nullptr;
    }
    void remove(const std::string& n)
    {
        for (size_t i = 0; i < e.size(); i++)
            if (e[i].name == n)
            {
                e.erase(e.begin() + long(i));
                return;
            }
    }
};

// A master insert .plg as the index reads it: a valid header, a manifest with a name, padding to the wanted size
std::string plg(uint32_t uid, uint32_t version, const char* name, size_t size = 0)
{
    struct
    {
        plug_file_header h;
        plug_manifest m;
    } hm;
    memset(&hm, 0, sizeof(hm));
    hm.h.magic = PLUG_MAGIC;
    hm.h.header_size = uint16_t(sizeof(hm));
    hm.h.abi_major = PLUG_ABI_MAJOR;
    hm.h.kind = PLUG_KIND_MASTER_INSERT;
    hm.h.cpu = PLUG_CPU_CM4F;
    hm.h.uid = uid;
    hm.h.version = version;
    hm.h.min_host = 1;
    hm.h.image_size = 256;
    hm.h.crc32 = uid * 2654435761u + version;   // stands in for the CRC of the body: another build, another value
    hm.m.struct_size = sizeof(plug_manifest);
    snprintf(hm.m.name, sizeof(hm.m.name), "%s", name);
    std::string s(reinterpret_cast<const char*>(&hm), sizeof(hm));
    s.resize(std::max(size, s.size() + 256), '\0');
    return s;
}

IndexEntry g_index[kIndexMax];
char g_names[kScanMax * 40];
char* g_nameList[kScanMax];
ScanStats g_st;
int g_count;
ScanCache g_cache;   // zero, as the firmware's .bss at boot

// plug::rescan(): true when it scanned. opens = files the call opened
bool rescan(FolderFs& fs, int* opens = nullptr)
{
    const int o = fs.opens;
    const bool scanned = refreshIndex(fs, g_cache, "/Plugins/Master", g_index, kIndexMax, g_names, g_nameList, g_st, g_count);
    if (opens) *opens = fs.opens - o;
    return scanned;
}

const IndexEntry* byFile(const char* f)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_index[i].file, f) == 0) return &g_index[i];
    return nullptr;
}

// plug::load()'s identity check (Place::wantUid / wantCrc against the header of the file it opened)
bool installOk(FolderFs& fs, const IndexEntry& ix)
{
    plug_file_header h;
    char p[64];
    snprintf(p, sizeof(p), "/Plugins/Master/%s", ix.file);
    if (fs.read(p, &h, int(sizeof(h))) < int(sizeof(h))) return false;   // gone: kNoFile
    return h.uid == ix.uid && h.crc32 == ix.crc;                           // else kStale
}
}   // namespace

int main()
{
    FolderFs fs;
    for (int i = 0; i < 100; i++)
    {
        char n[32], f[40];
        snprintf(n, sizeof(n), "Fx %02d", i);
        snprintf(f, sizeof(f), "fx%02d.plg", i);
        fs.e.push_back({f, plg(0x1000u + uint32_t(i), 0x10000, n)});
    }
    fs.e.push_back({"._fx00.plg", std::string(4096, 'x')});   // macOS twin: never listed
    fs.e.push_back({"readme.txt", "hello"});
    int opens = 0;

    printf("-- boot\n");
    check(rescan(fs, &opens) && g_count == 100 && opens == 100, "the first rescan (boot) scans: 100 inserts, every file opened");

    printf("-- unchanged folder\n");
    check(!rescan(fs, &opens) && opens == 0 && g_count == 100, "second open: no scan, no file opened, the index kept");
    check(!rescan(fs, &opens) && opens == 0, "third open: no scan");
    const int walks = fs.walks;
    rescan(fs);
    check(fs.walks == walks + 1, "the check is one directory walk");

    printf("-- entries the scan ignores\n");
    fs.e.push_back({"notes.txt", "x"});
    fs.e.push_back({"._fx01.plg", "abc"});
    fs.e.push_back({"old", "", true});
    fs.e.push_back({"hidden.plg", plg(0x9999, 1, "Hid"), false, true});
    check(!rescan(fs), "a text file, a ._ twin, a folder and a hidden .plg: no scan");

    printf("-- added file\n");
    fs.e.push_back({"new.plg", plg(0x2000, 0x10000, "New")});
    check(rescan(fs, &opens) && g_count == 101 && byFile("new.plg") && opens == 101, "added: scan, 101 inserts");
    check(!rescan(fs), "then unchanged: no scan");

    printf("-- removed file\n");
    fs.remove("fx50.plg");
    check(rescan(fs) && g_count == 100 && !byFile("fx50.plg"), "removed: scan, fx50 gone");
    check(!rescan(fs), "then unchanged: no scan");

    printf("-- renamed file\n");
    fs.find("fx10.plg")->name = "fx10b.plg";
    check(rescan(fs) && byFile("fx10b.plg") && !byFile("fx10.plg") && g_count == 100, "renamed: scan, the index has the new name");
    fs.find("fx11.plg")->name = "FX11.plg";
    check(rescan(fs) && byFile("FX11.plg"), "renamed in case only: scan");

    printf("-- replaced at another size\n");
    fs.find("fx20.plg")->bytes = plg(0x1014, 0x10100, "Fx 20", 2048);
    check(rescan(fs) && byFile("fx20.plg") && byFile("fx20.plg")->version == 0x10100, "a new build at another size: scan, new version");

    printf("-- swapped names, same sizes\n");
    {
        Entry* a = fs.find("fx30.plg");
        Entry* b = fs.find("fx31.plg");
        std::swap(a->bytes, b->bytes);   // the names keep their directory slots, contents trade places: not visible to the walk
        check(!rescan(fs), "two files trade contents at equal sizes: the signature cannot see it");
        const IndexEntry* e = byFile("fx30.plg");
        check(e && !installOk(fs, *e), "the install of the stale entry is refused (kStale): uid/crc of the file differ");
        g_cache.invalidate();   // what apply() does on kStale
        check(rescan(fs) && installOk(fs, *byFile("fx30.plg")) && installOk(fs, *byFile("fx31.plg")),
              "after the forced rescan both install");
    }

    printf("-- rewritten in place at the same size\n");
    fs.find("fx40.plg")->bytes = plg(0x1028, 0x10001, "Fx 40");
    check(!rescan(fs), "same name, same size, new version: no scan");
    check(!installOk(fs, *byFile("fx40.plg")), "the install is refused (kStale)");

    printf("-- USB storage session / card change\n");
    g_cache.invalidate();   // plug::cardChanged()
    check(rescan(fs, &opens) && opens == g_st.listed && byFile("fx40.plg")->version == 0x10001,
          "after cardChanged(): scan of every file, the new version listed");
    check(!rescan(fs), "then unchanged: no scan");
    g_cache.invalidate();
    check(rescan(fs), "cardChanged() on an unchanged folder still scans");

    printf("-- deleted file behind the index\n");
    {
        const IndexEntry e = *byFile("fx60.plg");
        fs.remove("fx60.plg");
        check(!installOk(fs, e), "a file deleted after the scan is not installed (kNoFile)");
        check(rescan(fs) && !byFile("fx60.plg"), "the next open scans");
    }

    printf("-- folder missing, then back\n");
    fs.present = false;
    check(rescan(fs) && g_count == 0, "no folder: scan, empty index");
    check(rescan(fs) && g_count == 0, "no folder: always scans (nothing to open, cheap)");
    fs.present = true;
    check(rescan(fs) && g_count > 0, "folder back: scan");
    check(!rescan(fs), "then unchanged: no scan");

    printf("-- order\n");
    std::swap(fs.e[0], fs.e[1]);
    check(rescan(fs), "two entries trade directory slots (a copy tool rewrote the folder): scan");

    printf(g_fail ? "FAILED %d\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
