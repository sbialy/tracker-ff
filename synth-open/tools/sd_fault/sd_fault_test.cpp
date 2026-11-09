// Host fault injection for the SdDir list builders of platforms/mk66/3rdparty/SD/SD.cpp. Host only, NOT firmware.
//
// run_sd_fault_test.sh extracts the SdDir half of SD.cpp verbatim (from "bool SdDir::open(" to the end) and compiles it against the real
// FatFs headers with a fake f_opendir/f_readdir/f_closedir over an in-memory tree. operator new is replaced by a counting allocator that
// returns nullptr on the Nth call, like the MK66's operator new (a plain malloc that returns null on a short heap). ui_shot does not
// contain this code (its HFileSystem is a host stand-in), so this is the only Mac check of SD.cpp.
//
// Per case and per N (every allocation of the call, one at a time, and "all from N on") the call runs in a forked child under
// ASan/UBSan. Verdicts: CLEAN = no fault and the returned list is a valid prefix of the unfailed list (createFilesList) or a
// valid subset with the right count (createInstrumentsList), CRASH = signal or sanitizer exit, BAD = survived but wrong.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#include "ff.h"
#include "SD.h"

// ---- counting allocator -----------------------------------------------------------------------------------------------------
static bool g_armed = false;
static long g_count = 0;     // allocations seen while armed
static long g_failAt = 0;    // fail exactly this one (1 based)
static long g_failFrom = 0;  // fail this one and every later one
static size_t g_failSize = 0;// fail every allocation of exactly this size
static bool shouldFail(size_t n)
{
    if (!g_armed)
        return false;
    g_count++;
    return (g_failAt && g_count == g_failAt) || (g_failFrom && g_count >= g_failFrom) || (g_failSize && n == g_failSize);
}
// optnone: the throwing operator new is declared non-null, so with optimisation the compiler would drop the "return nullptr" path as undefined
#define NOOPT __attribute__((noinline, optnone))
NOOPT static void* alloc(size_t n) { return shouldFail(n) ? nullptr : malloc(n ? n : 1); }
NOOPT void* operator new(size_t n) { return alloc(n); }     // MK66 behaviour: null, not bad_alloc
NOOPT void* operator new[](size_t n) { return alloc(n); }
NOOPT void* operator new(size_t n, const std::nothrow_t&) noexcept { return alloc(n); }
NOOPT void* operator new[](size_t n, const std::nothrow_t&) noexcept { return alloc(n); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

// ---- fake FatFs --------------------------------------------------------------------------------------------------------------
struct Ent { std::string name; bool dir; bool hidden; uint32_t size; };
static std::map<std::string, std::vector<Ent>> g_fs;
static std::map<const DIR*, std::pair<const std::vector<Ent>*, size_t>> g_open;
// the fake's own bookkeeping (the map nodes) must not take part in the counting or the failing
struct Quiet
{
    bool was;
    Quiet() : was(g_armed) { g_armed = false; }
    ~Quiet() { g_armed = was; }
};

extern "C" {
FRESULT f_opendir(DIR* dp, const TCHAR* path)
{
    Quiet q;
    auto it = g_fs.find(path);
    if (it == g_fs.end())
        return FR_NO_PATH;
    g_open[dp] = { &it->second, 0 };
    return FR_OK;
}
FRESULT f_readdir(DIR* dp, FILINFO* fno)
{
    Quiet q;
    auto it = g_open.find(dp);
    if (it == g_open.end())
        return FR_INVALID_OBJECT;
    if (fno == nullptr)
    {
        it->second.second = 0;
        return FR_OK;
    }
    if (it->second.second >= it->second.first->size())
    {
        fno->fname[0] = 0;
        return FR_OK;
    }
    const Ent& e = (*it->second.first)[it->second.second++];
    memset(fno, 0, sizeof(*fno));
    strncpy(fno->fname, e.name.c_str(), sizeof(fno->fname) - 1);
    fno->fattrib = (e.dir ? AM_DIR : 0) | (e.hidden ? AM_HID : 0);
    fno->fsize = e.size;
    return FR_OK;
}
FRESULT f_closedir(DIR* dp)
{
    Quiet q;
    if (dp == nullptr)
        return FR_INVALID_OBJECT;
    g_open.erase(dp);
    return FR_OK;
}
FRESULT f_stat(const TCHAR*, FILINFO*) { return FR_NO_FILE; }
}

#include "sd_dir_part.inc" // SdDir half of SD.cpp, verbatim

void reportSdError(const char*, uint16_t) {}

// ---- cases -------------------------------------------------------------------------------------------------------------------
enum { kListMax = 64 };
struct Case
{
    const char* name;
    int kind; // 0 createFilesList, 1 createInstrumentsList
    const char* dir;
    uint8_t startLine;
    uint8_t listLength;
    uint16_t maxMem;
    SdDir::enListFileFilter filter;
    uint16_t skip;
    uint16_t maxInSub;
    bool lenient = false; // only well formed, not a prefix of the unfailed list (a folder that is not split any more lists plain)
};

static std::vector<std::string> runCall(const Case& c, int& nOut, bool& wellFormed)
{
    SdDir d;
    g_armed = false;
    if (!d.open(c.dir))
    {
        nOut = -1;
        wellFormed = false;
        return {};
    }
    char* list[kListMax] = {};
    g_count = 0;
    g_armed = true;
    uint32_t sz = 0;
    uint16_t n = c.kind == 0
                   ? d.createFilesList(c.startLine, list, c.listLength, c.maxMem, c.filter, c.skip, c.maxInSub, SdDir::enListFileFilter_wave)
                   : d.createInstrumentsList(list, c.listLength, &sz);
    g_armed = false;
    nOut = n;
    wellFormed = true;
    std::vector<std::string> out;
    if (c.kind == 0)
    {
        for (int i = 0; i < n; i++)
        {
            if (list[i] == nullptr && i >= c.startLine)
                wellFormed = false;
            out.push_back(list[i] ? list[i] : "<null>");
        }
    }
    else
    {
        int cnt = 0;
        for (int i = 0; i < c.listLength; i++)
        {
            out.push_back(list[i] ? list[i] : "");
            cnt += list[i] != nullptr;
        }
        if (cnt != n)
            wellFormed = false;
    }
    for (int i = 0; i < kListMax; i++)
        if (list[i] != nullptr)
            delete list[i];
    d.close();
    return out;
}

static void buildTree()
{
    auto& root = g_fs["/lib"];
    // 12 files before, a folder with 25 wavs (split into 3 ranges at 10), a folder with 4, 30 files after
    for (int i = 0; i < 12; i++)
        root.push_back({ "pre" + std::to_string(i) + ".wav", false, false, 100 });
    root.push_back({ "bigfolder", true, false, 0 });
    root.push_back({ "smallfolder", true, false, 0 });
    root.push_back({ "hidden.wav", false, true, 100 });
    for (int i = 0; i < 30; i++)
        root.push_back({ "post" + std::to_string(i) + ".wav", false, false, 100 });
    for (int i = 0; i < 25; i++)
        g_fs["/lib/bigfolder"].push_back({ "s" + std::to_string(i) + ".wav", false, false, 10 });
    for (int i = 0; i < 4; i++)
        g_fs["/lib/smallfolder"].push_back({ "t" + std::to_string(i) + ".wav", false, false, 10 });
    // a folder whose first entry is a split folder (the first slot is the one the counter underflowed on)
    g_fs["/first"].push_back({ "big", true, false, 0 });
    for (int i = 0; i < 25; i++)
        g_fs["/first/big"].push_back({ "s" + std::to_string(i) + ".wav", false, false, 10 });
    g_fs["/first"].push_back({ "a.wav", false, false, 1 });
    // instruments: "<n> name.pti"
    for (int i = 1; i <= 48; i += 3)
        g_fs["/inst"].push_back({ std::to_string(i) + " inst" + std::to_string(i) + ".pti", false, false, 500 });
    g_fs["/inst"].push_back({ "junk.txt", false, false, 1 });
}

// "/folder (1-10)", "/folder (11-20)" ... collapse to "/folder": a failed folder count lists the folder plain, which is the fallback
static std::vector<std::string> collapse(const std::vector<std::string>& v)
{
    std::vector<std::string> o;
    for (const std::string& e : v)
    {
        std::string x = e;
        size_t p = x.rfind(" (");
        if (x.size() > 1 && x[0] == '/' && p != std::string::npos && x.back() == ')')
            x.resize(p);
        if (!o.empty() && x[0] == '/' && o.back() == x)
            continue;
        o.push_back(x);
    }
    return o;
}

static int verdictOf(const Case& c, const std::vector<std::string>& ref, int n, bool wf, const std::vector<std::string>& got)
{
    if (!wf)
        return 2;
    if (c.kind == 0)
    {
        std::vector<std::string> g(got.begin() + c.startLine, got.end()), r(ref.begin() + c.startLine, ref.end());
        g = collapse(g);
        r = collapse(r);
        // a plain folder uses one slot where the unfailed list used three, so the failed list may reach further down the directory
        for (size_t i = 0; i < g.size() && i < r.size(); i++)
            if (g[i] != r[i])
                return 2;
    }
    else
    {
        for (size_t i = 0; i < got.size(); i++)
            if (!got[i].empty() && got[i] != ref[i])
                return 2;
    }
    return 0;
}

// one fault run in a child, exit 0 clean / 2 bad / signal or 77 crash
static int child(const Case& c, const std::vector<std::string>& ref, long failAt, long failFrom, size_t failSize)
{
    fflush(stdout);
    pid_t p = fork();
    if (p == 0)
    {
        g_failAt = failAt;
        g_failFrom = failFrom;
        g_failSize = failSize;
        int n;
        bool wf;
        auto got = runCall(c, n, wf);
        int v = verdictOf(c, ref, n, wf, got);
        if (v && getenv("SDF_VERBOSE"))
        {
            fprintf(stderr, "DBG n=%d wf=%d got:", n, (int)wf);
            for (auto& e : got)
                fprintf(stderr, " [%s]", e.c_str());
            fprintf(stderr, "\n");
        }
        _exit(v);
    }
    int st = 0;
    waitpid(p, &st, 0);
    if (WIFSIGNALED(st))
        return 100 + WTERMSIG(st);
    return WEXITSTATUS(st);
}

int main(int argc, char** argv)
{
    const char* label = argc > 1 ? argv[1] : "?";
    buildTree();
    const Case cases[] = {
        { "files.plain", 0, "/lib", 0, 20, 4000, SdDir::enListFileFilter_wave, 0, 0 },
        { "files.split(folder 25>10 -> 3 ranges)", 0, "/lib", 0, 20, 4000, SdDir::enListFileFilter_all, 0, 10 },
        { "files.split.first_slot", 0, "/first", 0, 20, 4000, SdDir::enListFileFilter_all, 0, 10 },
        { "files.limit(30 entries, list 21, skip 12)", 0, "/lib", 0, 21, 4000, SdDir::enListFileFilter_wave, 12, 0 },
        { "files.startLine3", 0, "/lib", 3, 20, 4000, SdDir::enListFileFilter_all, 0, 10 },
        { "instruments", 1, "/inst", 0, 48, 0, SdDir::enListFileFilter_all, 0, 0 },
    };
    int crashTotal = 0, badTotal = 0, cleanTotal = 0;
    printf("== %s\n", label);
    for (const Case& c : cases)
    {
        int n;
        bool wf;
        g_failAt = g_failFrom = 0;
        g_failSize = 0;
        auto ref = runCall(c, n, wf);
        long allocs = g_count;
        printf("%-44s unfailed: n=%d, %ld allocations in the call\n", c.name, n, allocs);
        if (c.maxInSub)
        {
            // the folder entries of the unfailed list: ranges must be contiguous and cover every file of the folder
            std::string folders;
            for (const std::string& e : ref)
                if (e[0] == '/' && e.find(" (") != std::string::npos)
                    folders += " [" + e + "]";
            printf("    split entries:%s\n", folders.c_str());
        }
        int crash = 0, bad = 0, clean = 0;
        std::string crashAt;
        for (long k = 1; k <= allocs; k++)
        {
            for (int mode = 0; mode < 2; mode++) // exactly the kth / the kth and all later
            {
                int r = child(c, ref, mode == 0 ? k : 0, mode == 1 ? k : 0, 0);
                if (r == 0)
                    clean++;
                else if (r == 2)
                {
                    bad++;
                    if (getenv("SDF_VERBOSE"))
                        printf("      BAD %s %ld\n", mode ? "from" : "at", k);
                }
                else
                {
                    crash++;
                    if (crashAt.size() < 60)
                        crashAt += (mode ? " from" : " at") + std::to_string(k);
                }
            }
        }
        printf("    fail Nth / from Nth over %ld points: CLEAN %d, BAD %d, CRASH %d%s\n", allocs, clean, bad, crash, crashAt.c_str());
        crashTotal += crash;
        badTotal += bad;
        cleanTotal += clean;
    }
    // the folder count allocation by size: strlen(name)+strlen(dir_path)+2 for "bigfolder" under "/lib" and the other
    for (size_t sz : { strlen("bigfolder") + strlen("/lib") + 2, strlen("smallfolder") + strlen("/lib") + 2 })
    {
        int n;
        bool wf;
        g_failAt = g_failFrom = 0;
        g_failSize = 0;
        Case c = cases[1];
        c.lenient = true;
        auto ref = runCall(c, n, wf);
        int r = child(c, ref, 0, 0, sz);
        printf("countFilesInSubDirectory path alloc (size %zu) fails every time: %s\n", sz, r == 0 ? "CLEAN" : r == 2 ? "BAD" : "CRASH");
        (r == 0 ? cleanTotal : r == 2 ? badTotal : crashTotal)++;
    }
    // memory accounting: n returned for max_used_memory sweeps over the split folder (the counter fix, 14 B per split folder)
    printf("accounting sweep, /first (split folder first), list 20, n by max_used_memory:");
    for (int mm = 255 + 20; mm <= 255 + 120; mm += 8)
    {
        Case c = { "acct", 0, "/first", 0, 20, (uint16_t)mm, SdDir::enListFileFilter_all, 0, 10 };
        int n;
        bool wf;
        g_failAt = g_failFrom = 0;
        g_failSize = 0;
        runCall(c, n, wf);
        printf(" %d:%d", mm, n);
    }
    printf("\nRESULT %s CLEAN %d BAD %d CRASH %d\n", label, cleanTotal, badTotal, crashTotal);
    return 0;
}
