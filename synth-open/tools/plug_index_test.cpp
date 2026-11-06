// Host test of the plugin index (synth-open/plug_index.h): the /Plugins/Master scan and the header, layout and CRC checks the
// firmware runs, against the real .plg files on a card replica laid out like the user's (lowercase "plugins" from an older
// copy, macOS "._" twins next to every file, an old plugin one level up).
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_index_test.cpp -o /tmp/plug_index_test &&
//   /tmp/plug_index_test build/plugin-host
#include "plug_index.h"

#include <algorithm>
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

std::string lower(std::string s)
{
    for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Entry
{
    std::string dir, name, bytes;   // dir as stored on the card, "" for the root
    bool folder = false, hidden = false;
};

// A FAT card: names keep the case they were written with, every lookup ignores case (FatFs with LFN)
struct CardFs
{
    std::vector<Entry> e;

    bool sameDir(const std::string& stored, const std::string& asked) const
    {
        std::string a = asked;
        while (!a.empty() && a.front() == '/') a.erase(0, 1);
        return lower(stored) == lower(a);
    }

    // FileSystemImpl::listDirContents (platforms/mk66/source/FileSystemImpl.cpp) step for step: count is the room in
    // list on the way in, the loop stops there, hidden entries and "._" files never reach the filter
    void list(const char* dir, bool (*filter)(const char*), char* names, int namesSize, int maxNameLen, int& count,
              char** list)
    {
        bool dirFound = false;
        for (const Entry& x : e)
            if (x.folder && sameDir(x.dir.empty() ? x.name : x.dir + "/" + x.name, dir)) dirFound = true;
        if (!dirFound)
        {
            count = 0;
            return;
        }
        int listPos = 0;
        for (size_t i = 0; i < e.size() && listPos < count; i++)
        {
            const Entry& x = e[i];
            if (!sameDir(x.dir, dir) || x.hidden || x.folder) continue;
            if (x.name[0] == '.' && x.name[1] == '_') continue;   // isMacMetaFile
            if (filter && !filter(x.name.c_str())) continue;
            const int nameLen = int(x.name.size()) + 1;
            if (nameLen > namesSize) break;
            if (nameLen > maxNameLen) continue;
            list[listPos] = names;
            memcpy(names, x.name.c_str(), size_t(nameLen));
            names += nameLen;
            namesSize -= nameLen;
            listPos++;
        }
        count = listPos;
        std::sort(list, list + count, [](const char* a, const char* b) { return strcasecmp(a, b) < 0; });
    }

    int read(const char* p, void* buf, int cap)
    {
        std::string path = p;
        const size_t slash = path.rfind('/');
        const std::string dir = path.substr(0, slash), name = path.substr(slash + 1);
        for (const Entry& x : e)
            if (!x.folder && sameDir(x.dir, dir) && lower(x.name) == lower(name))
            {
                const int n = std::min(cap, int(x.bytes.size()));
                memcpy(buf, x.bytes.data(), size_t(n));
                return n;
            }
        return -1;
    }

    void file(const std::string& dir, const std::string& name, const std::string& bytes, bool hidden = false)
    {
        e.push_back({dir, name, bytes, false, hidden});
    }
    void folder(const std::string& dir, const std::string& name) { e.push_back({dir, name, "", true, false}); }
};

std::string slurp(const std::string& p)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}

std::string appleDouble()
{
    std::string s(4096, '\0');
    const unsigned char m[] = {0x00, 0x05, 0x16, 0x07, 0x00, 0x02, 0x00, 0x00};   // AppleDouble magic and version 2
    memcpy(&s[0], m, sizeof(m));
    return s;
}

const char* kPlugs[] = {"dirt.plg", "tape.plg", "lofi.plg"};

int scan(CardFs& fs, IndexEntry* ix, ScanStats& st)
{
    static char names[kScanMax * 40];
    static char* list[kScanMax];
    return scanIndex(fs, "/Plugins/Master", ix, kIndexMax, names, list, st);
}

bool has(const IndexEntry* ix, int n, const char* file)
{
    for (int i = 0; i < n; i++)
        if (lower(ix[i].file) == lower(file)) return true;
    return false;
}
}   // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "build/plugin-host";
    std::string bytes[3];
    for (int i = 0; i < 3; i++)
    {
        bytes[i] = slurp(dir + "/" + kPlugs[i]);
        if (bytes[i].size() < sizeof(plug_file_header))
        {
            printf("FAIL  cannot read %s/%s\n", dir.c_str(), kPlugs[i]);
            return 1;
        }
    }

    // ---- the rebase offset bound (load(): a damaged table word must not reach outside the image)
    check(rebaseOffsetOk(0, 4) && rebaseOffsetOk(1020, 1024) && !rebaseOffsetOk(1024, 1024) && !rebaseOffsetOk(1022, 1024),
          "rebase offset: the last whole word is in, one past and an unaligned one are out");
    check(!rebaseOffsetOk(0xFFFFFFFCu, 1024) && !rebaseOffsetOk(0xFFFFFFF8u, 65536),
          "rebase offset 0xFFFFFFFC (off + 4 wraps to 0) and 0xFFFFFFF8 are out");
    check(!rebaseOffsetOk(0, 0) && !rebaseOffsetOk(0, 3), "rebase offset: an image smaller than a word has none");

    // ---- each real file through the checks load() runs before anything executes
    for (int i = 0; i < 3; i++)
    {
        const std::string& b = bytes[i];
        plug_file_header h;
        memcpy(&h, b.data(), sizeof(h));
        char what[96];
        snprintf(what, sizeof(what), "%s: header (uid %08x v %u, image %u, rw %u, bss %u, rebase %u)", kPlugs[i], h.uid,
                 h.version, h.image_size, h.rw_offset, h.bss_size, h.rebase_count);
        check(checkHeader(h, PLUG_KIND_MASTER_INSERT) == kOk, what);
        snprintf(what, sizeof(what), "%s: file size = header + image + rebase table", kPlugs[i]);
        check(b.size() == size_t(h.header_size) + h.image_size + 4u * h.rebase_count, what);
        snprintf(what, sizeof(what), "%s: crc32 of everything after the 64 byte header", kPlugs[i]);
        check(crc32(0, reinterpret_cast<const uint8_t*>(b.data()) + sizeof(h), uint32_t(b.size() - sizeof(h))) == h.crc32,
              what);
        snprintf(what, sizeof(what), "%s: fits insert 1 (8 KB SRAM_L slot)", kPlugs[i]);
        check(checkLayout(h, 8192, 0) == kOk, what);
        const uint32_t rwOff = h.rw_offset ? h.rw_offset : h.image_size;
        snprintf(what, sizeof(what), "%s: fits insert 2 (8 KB data flash, rw + bss %u of 1024 B)", kPlugs[i],
                 h.image_size - rwOff + h.bss_size);
        check(h.image_size - rwOff + h.bss_size <= 1024 && checkLayout(h, 8192, 1024) == kOk, what);
    }

    // ---- the user's card: /plugins (lowercase, from the spike) with the new Master inside, "._" twins everywhere
    CardFs card;
    card.folder("", "plugins");
    card.file("", "._plugins", appleDouble());
    card.file("plugins", "master.plg", bytes[0]);
    card.file("plugins", "._master.plg", appleDouble());
    card.file("plugins", "master.txt", "x");
    card.folder("plugins", "Master");
    card.file("plugins", "._Master", appleDouble());
    for (int i = 0; i < 3; i++)
    {
        card.file("plugins/Master", std::string("._") + kPlugs[i], appleDouble());
        card.file("plugins/Master", kPlugs[i], bytes[i]);
    }
    card.file("", "plugbench.plg", bytes[0]);

    IndexEntry ix[kIndexMax];
    ScanStats st;
    int n = scan(card, ix, st);
    printf("      scan: listed %d, rejected %d, index %d\n", st.listed, st.rejected, n);
    for (int i = 0; i < n; i++) printf("      %-15s uid %08x file %s params %u\n", ix[i].name, ix[i].uid, ix[i].file, ix[i].numParams);
    check(n == 3 && st.listed == 3 && st.rejected == 0, "user card: /plugins/Master gives Dirt, Tape and Lo-Fi");
    check(has(ix, n, "dirt.plg") && has(ix, n, "tape.plg") && has(ix, n, "lofi.plg"), "user card: all three files indexed");
    bool named = n == 3;
    for (int i = 0; i < n; i++) named = named && ix[i].name[0] && ix[i].numParams > 0;
    check(named, "user card: every entry has a manifest name and parameters");
    check(n < 2 || (ix[0].uid != ix[1].uid && ix[1].uid != ix[2].uid && ix[0].uid != ix[2].uid), "three different uids");
    {   // the master screen with the Inserts entry: every pair of the shipped plugins names both in the 9 character field
        bool ok = n == 3;
        for (int i = 0; i < n; i++)
            for (int j = -1; j < n; j++)
            {
                char out[16];
                const char* b2 = j < 0 ? "" : ix[j].name;
                insertsLabel(ix[i].name, b2, out, 9);
                const char* plus = strchr(out, '+');
                ok = ok && strlen(out) <= 9 &&
                     (j < 0 ? strncmp(out, "Ins:", 4) == 0 && strstr(out, ix[i].name) != nullptr
                            : plus && plus - out >= 2 && strlen(plus + 1) >= 2 && strncmp(out, ix[i].name, 2) == 0 &&
                                strncmp(plus + 1, ix[j].name, 2) == 0);
                if (i == 0) printf("      Saturation value with %s%s%s: \"%s\"\n", ix[i].name, j < 0 ? "" : " + ", b2, out);
            }
        check(ok, "user card: one plugin shows Ins:<full name>, every pair shows both names (2+ letters each) in 9 chars");
    }

    // ---- the call the firmware made before the fix: count 0 on the way in lists nothing
    {
        char names[kScanMax * 40];
        char* list[kScanMax];
        int zero = 0;
        card.list("/Plugins/Master", isPlugName, names, sizeof(names), 40, zero, list);
        check(zero == 0, "listDirContents with count 0 lists nothing (the 810b316 bug, guarded by scanIndex)");
    }

    // ---- any case of directory and file names
    CardFs up;
    up.folder("", "PLUGINS");
    up.folder("PLUGINS", "MASTER");
    up.file("PLUGINS/MASTER", "DIRT.PLG", bytes[0]);
    up.file("PLUGINS/MASTER", "Tape.Plg", bytes[1]);
    up.file("PLUGINS/MASTER", "lofi.pLG", bytes[2]);
    n = scan(up, ix, st);
    check(n == 3 && has(ix, n, "DIRT.PLG") && has(ix, n, "Tape.Plg") && has(ix, n, "lofi.pLG"), "upper and mixed case names");

    // ---- a damaged file is listed, rejected and reported, the good ones stay
    CardFs bad;
    bad.folder("", "Plugins");
    bad.folder("Plugins", "Master");
    std::string broken = bytes[1];
    broken[0] ^= 0x55;
    bad.file("Plugins/Master", "dirt.plg", bytes[0]);
    bad.file("Plugins/Master", "broken.plg", broken);
    bad.file("Plugins/Master", "short.plg", "PLG1");
    bad.file("Plugins/Master", "hidden.plg", bytes[2], true);
    bad.file("Plugins/Master", "readme.txt", "x");
    n = scan(bad, ix, st);
    check(n == 1 && st.listed == 3 && st.rejected == 2, "damaged files rejected, hidden and non-.plg files not listed");
    check(strcmp(st.reject[0].file, "broken.plg") == 0 && st.reject[0].err == kHeader &&
            strcmp(st.reject[1].file, "short.plg") == 0 && st.reject[1].err == kHeader,
          "rejects reported with file and error");

    // ---- no directory at all
    CardFs empty;
    n = scan(empty, ix, st);
    check(n == 0 && st.listed == 0, "no /Plugins/Master: empty index");

    // ---- the isPlugName filter
    check(isPlugName("a.plg") && isPlugName("A.PLG") && isPlugName("x.Plg"), "filter takes .plg in any case");
    check(!isPlugName("._dirt.plg") && !isPlugName(".plg") && !isPlugName("dirt.plgx") && !isPlugName("dirt.txt"),
          "filter refuses AppleDouble twins and other names");

    printf("%s\n", g_fail ? "FAILED" : "all passed");
    return g_fail ? 1 : 0;
}
