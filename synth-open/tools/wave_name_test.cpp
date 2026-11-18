// Host test of waves by NAME (PLUG_EXT_FILES tail: read_path, next_path) on the real AKWF-FREE tree (4,358 files in 65 folders, CC0, a clone
// kept outside the repository). Needs the tree: WAVES_SRC=<folder with the AKWF_* folders> (default ~/prj/poly/ref/akwf/AKWF), without it the test
// says SKIP and exits 0. Card A is /Waves = every AKWF folder, card B is the same after a user removed two folders, added one and deleted a
// file (folders are symlinks into the clone, nothing is copied). What it proves:
//   - the list keeps 1,024 of the 4,358 files (the cap), a path reaches every one of them (4,358 reads),
//   - waveNext walks all 4,358 in the list's order, one entry per call and no table (operation counts are printed),
//   - an index means another file on card B (every index shifts), a path means the same file (sample for sample), a deleted file is "not found",
//   - a name past the cap is as good as any other.
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/wave_name_test.cpp -o /tmp/wave_name_test && /tmp/wave_name_test
#include "plug_files.h"

#include <algorithm>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
int g_fail;
#define CHECK(c)                                                                                                                   \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!(c))                                                                                                                  \
        {                                                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                                    \
            g_fail++;                                                                                                              \
        }                                                                                                                          \
    } while (0)

std::string g_root;
long g_entries, g_opens, g_dirs;   // operation counts of the card access: directory entries returned, files opened, directories opened

struct PosixFs
{
    std::vector<std::string> names;
    std::vector<int> dirs;
    size_t at = 0;
    FILE* f = nullptr;
    bool dirOpen(const char* p)
    {
        names.clear();
        dirs.clear();
        const std::string base = g_root + p;
        DIR* d = opendir(base.c_str());
        if (!d) return false;
        g_dirs++;
        while (dirent* e = readdir(d))
        {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            struct stat st;
            stat((base + "/" + e->d_name).c_str(), &st);   // follows the symlinks of the test card
            names.insert(names.begin(), e->d_name);
            dirs.insert(dirs.begin(), S_ISDIR(st.st_mode));
        }
        closedir(d);
        at = 0;
        return true;
    }
    bool dirNext(char* name, bool& isDir, bool& hidden)
    {
        if (at >= names.size()) return false;
        snprintf(name, plug::kWaveName, "%s", names[at].c_str());
        isDir = dirs[at];
        hidden = false;
        at++;
        g_entries++;
        return true;
    }
    void dirClose() {}
    bool open(const char* p)
    {
        g_opens++;
        f = fopen((g_root + p).c_str(), "rb");
        return f != nullptr;
    }
    int read(void* d, int n) { return int(fread(d, 1, size_t(n), f)); }
    bool seek(uint32_t pos) { return fseek(f, long(pos), SEEK_SET) == 0; }
    void close()
    {
        if (f) fclose(f);
        f = nullptr;
    }
};

// Every wave of a folder tree (the real one), as relative paths one level down, sorted by strcmp
std::vector<std::string> universe(const std::string& tree)
{
    std::vector<std::string> v;
    DIR* d = opendir(tree.c_str());
    while (d)
    {
        dirent* e = readdir(d);
        if (!e) break;
        if (e->d_name[0] == '.') continue;
        const std::string sub = tree + "/" + e->d_name;
        if (DIR* dd = opendir(sub.c_str()))
        {
            while (dirent* f = readdir(dd))
                if (plug::waveName(f->d_name)) v.push_back(std::string(e->d_name) + "/" + f->d_name);
            closedir(dd);
        }
    }
    if (d) closedir(d);
    std::sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) { return strcmp(a.c_str(), b.c_str()) < 0; });
    return v;
}

void card(const std::string& waves, const std::string& src, const std::vector<std::string>& skip)
{
    mkdir(waves.c_str(), 0755);
    DIR* d = opendir(src.c_str());
    while (dirent* e = readdir(d))
    {
        if (e->d_name[0] == '.') continue;
        bool drop = false;
        for (const auto& s : skip) drop = drop || s == e->d_name;
        if (!drop && symlink((src + "/" + e->d_name).c_str(), (waves + "/" + e->d_name).c_str()) != 0) printf("symlink failed\n");
    }
    closedir(d);
}
}   // namespace

int main()
{
    std::string src = getenv("WAVES_SRC") ? getenv("WAVES_SRC") : (std::string(getenv("HOME")) + "/prj/poly/ref/akwf/AKWF");
    std::vector<std::string> all = universe(src);
    if (all.empty())
    {
        printf("SKIP (no AKWF tree at %s, set WAVES_SRC)\n", src.c_str());
        return 0;
    }
    printf("AKWF files: %zu\n", all.size());
    CHECK(all.size() == 4358);

    char tmpl[] = "/tmp/wave_name_test.XXXXXX";
    g_root = mkdtemp(tmpl);
    card(g_root + "/Waves", src, {});
    PosixFs fs;
    static plug::WaveList w;
    plug::waveBuild(fs, w, "/Waves");
    // the cap
    CHECK(w.n == int(PLUG_FILES_MAX));
    CHECK(w.skipped == all.size() - PLUG_FILES_MAX);
    bool capOk = true;
    for (int i = 0; i < w.n; i++) capOk = capOk && !strcmp(w.at(i), all[size_t(i)].c_str());
    CHECK(capOk);   // the list is the 1,024 smallest paths
    printf("list: %d of %zu files (cap %u), last listed %s\n", w.n, all.size(), PLUG_FILES_MAX, w.at(w.n - 1));

    // every file by path, and the first 1,024 equal to the read by index
    int16_t a[2100], b[2100];
    size_t okReads = 0, beyond = 0;
    bool same = true, pathsOk = true;
    g_opens = 0;
    for (size_t i = 0; i < all.size(); i++)
    {
        pathsOk = pathsOk && plug::wavePathOk(all[i].c_str());
        const int n = plug::waveReadPath(fs, all[i].c_str(), 0, a, 2048, "/Waves");
        if (n > 0 && n <= int(PLUG_FILES_FRAMES)) okReads++;
        if (i >= PLUG_FILES_MAX) beyond++;
        if (i < PLUG_FILES_MAX)
        {
            const int m = plug::waveRead(fs, w, uint32_t(i), 0, b, 2048, "/Waves");
            same = same && n == m && !memcmp(a, b, size_t(n) * 2);
        }
    }
    CHECK(pathsOk);
    CHECK(okReads == all.size());
    CHECK(same);
    printf("read by path: %zu of %zu files (%zu of them past the cap), %ld opens\n", okReads, all.size(), beyond, g_opens);

    // next over the whole tree
    {
        char nm[plug::kWaveName], out[PLUG_FILES_PATH];
        bool chain = true;
        long worstEntries = 0, worstDirs = 0, totalEntries = 0, totalDirs = 0;
        for (size_t i = 0; i + 1 < all.size(); i++)
        {
            g_entries = g_dirs = 0;
            const int n = plug::waveNext(fs, nm, all[i].c_str(), out, sizeof(out), "/Waves");
            chain = chain && n == int(all[i + 1].size()) && all[i + 1] == out;
            totalEntries += g_entries;
            totalDirs += g_dirs;
            worstEntries = std::max(worstEntries, g_entries);
            worstDirs = std::max(worstDirs, g_dirs);
        }
        CHECK(chain);
        CHECK(plug::waveNext(fs, nm, all.back().c_str(), out, sizeof(out), "/Waves") == PLUG_FILES_E_NONE);
        printf("next: %zu calls all in list order, directory entries per call mean %.1f worst %ld, directories opened per call mean %.2f worst %ld\n",
               all.size() - 1, double(totalEntries) / double(all.size() - 1), worstEntries, double(totalDirs) / double(all.size() - 1), worstDirs);
        // the index convention of the wave lists: User B Next after the last listed file is the first file the list does not reach
        CHECK(plug::waveNext(fs, nm, w.at(w.n - 1), out, sizeof(out), "/Waves") > 0 && all[PLUG_FILES_MAX] == out);
    }

    // card B: two folders gone, one added, one file deleted
    const std::string gone1 = "AKWF_0001", gone2 = "AKWF_0002";
    card(g_root + "/B", src, {gone1, gone2});
    mkdir((g_root + "/B/AKWF_0000").c_str(), 0755);
    {
        FILE* in = fopen((src + "/" + all[300]).c_str(), "rb");
        FILE* o = fopen((g_root + "/B/AKWF_0000/AKWF_0000.wav").c_str(), "wb");
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, o);
        fclose(in);
        fclose(o);
    }
    static plug::WaveList wb;
    plug::waveBuild(fs, wb, "/B");
    CHECK(wb.n == int(PLUG_FILES_MAX));
    // a fitted patch was made on card A: file X at index iA (1-based as a wave list parameter). On card B the same index is another file.
    const char* picks[] = {all[250].c_str(), all[400].c_str(), all[700].c_str(), all[900].c_str()};   // AKWF_0003 .. AKWF_0010: after the two folders that go
    int shifted = 0;
    for (const char* x : picks)
    {
        int ia = -1;
        for (int i = 0; i < w.n; i++)
            if (!strcmp(w.at(i), x)) ia = i;
        CHECK(ia >= 0);
        const int n0 = plug::waveReadPath(fs, x, 0, a, 2048, "/Waves");    // what the patch means (card A, by name)
        const int nIdx = plug::waveRead(fs, wb, uint32_t(ia), 0, b, 2048, "/B");   // the old firmware's way on card B: the same index
        const bool idxSame = nIdx == n0 && !memcmp(a, b, size_t(n0) * 2);
        const int nName = plug::waveReadPath(fs, x, 0, b, 2048, "/B");    // by name on card B
        const bool nameSame = nName == n0 && !memcmp(a, b, size_t(n0) * 2);
        CHECK(nameSame);
        if (!idxSame) shifted++;
        printf("  %-26s index %4d on A, on B the index plays %s, the name plays %s\n", x, ia + 1, idxSame ? "the same file" : "ANOTHER file",
               nameSame ? "the same file" : "ANOTHER file");
    }
    CHECK(shifted == 4);   // removing two folders and adding one moves every file after them
    // the removed folder's files are gone: not found, never a fault, and nothing else is read in their place
    CHECK(plug::waveReadPath(fs, "AKWF_0001/AKWF_0001.wav", 0, b, 2048, "/B") == PLUG_FILES_E_NONE);
    // a file past the cap on card B is reachable by name, the list does not hold it
    const std::string past = all.back();   // the last file of the tree, 3,300 files past the end of the list
    bool inList = false;
    for (int i = 0; i < wb.n; i++) inList = inList || !strcmp(wb.at(i), past.c_str());
    CHECK(!inList);
    CHECK(plug::waveReadPath(fs, past.c_str(), 0, b, 2048, "/B") > 0);
    // next on card B: after the added folder's file comes the first file of AKWF_0003, and a name that is gone still has a successor there
    {
        char nm[plug::kWaveName], out[PLUG_FILES_PATH];
        std::string first3;   // the smallest path of AKWF_0003
        for (const auto& p : all)
            if (p.rfind("AKWF_0003/", 0) == 0) { first3 = p; break; }
        CHECK(!first3.empty());
        CHECK(plug::waveNext(fs, nm, "AKWF_0000/AKWF_0000.wav", out, sizeof(out), "/B") > 0 && first3 == out);
        CHECK(plug::waveNext(fs, nm, all[0].c_str(), out, sizeof(out), "/B") > 0 && first3 == out);   // the first file of the removed AKWF_0001
    }

    std::string rm = "rm -rf '" + g_root + "'";
    if (system(rm.c_str()) != 0) printf("cleanup failed\n");
    printf("%s (%d failures)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
