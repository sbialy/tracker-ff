// Host test of the plugin host's project sidecar (synth-open/plug_sidecar.h): the text codec and the file rules the firmware
// runs on project load, Workspace save, project open and project save (plug_host.cpp calls the same templates on the SD card).
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_sidecar_test.cpp -o /tmp/plug_sidecar_test && /tmp/plug_sidecar_test
#include "plug_sidecar.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace plug;

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

// An in-memory card: path -> bytes
struct MemFs
{
    std::map<std::string, std::string> files;
    int writes = 0, removes = 0;
    int read(const char* p, char* buf, int cap)
    {
        auto it = files.find(p);
        if (it == files.end()) return -1;
        const int n = int(it->second.size()) < cap ? int(it->second.size()) : cap;
        memcpy(buf, it->second.data(), size_t(n));
        return n;
    }
    bool write(const char* p, const char* buf, int n)
    {
        files[p] = std::string(buf, size_t(n));
        writes++;
        return true;
    }
    bool exists(const char* p) { return files.count(p) != 0; }
    bool remove(const char* p)
    {
        removes++;
        return files.erase(p) != 0;
    }
};

void setRef(SlotRef& r, uint32_t uid, uint32_t ver, const char* name, std::initializer_list<std::pair<const char*, float>> ps)
{
    memset(&r, 0, sizeof(r));
    r.uid = uid;
    r.version = ver;
    snprintf(r.name, sizeof(r.name), "%s", name);
    for (auto& [n, v] : ps)
    {
        snprintf(r.pname[r.count], kNameLen, "%s", n);
        r.pval[r.count++] = v;
    }
}

bool sameRef(const SlotRef& a, const SlotRef& b)
{
    if (a.uid != b.uid || a.version != b.version || strcmp(a.name, b.name) || a.count != b.count) return false;
    for (int i = 0; i < a.count; i++)
        if (strcmp(a.pname[i], b.pname[i]) || std::fabs(a.pval[i] - b.pval[i]) > 1e-6f * (1.0f + std::fabs(a.pval[i])))
            return false;
    return true;
}

bool empty(const Sidecar& s)
{
    for (const SlotRef& r : s.slot)
        if (r.uid || r.count) return false;
    return true;
}
}   // namespace

int main()
{
    static char buf[kSidecarMax];
    static Sidecar a, b;

    // ---- codec
    memset(&a, 0, sizeof(a));
    setRef(a.slot[0], 0x3a51c2e0u, 0x00010000u, "Tape", {{"Drive", 30}, {"Wow", 20}, {"Flutter", 10}, {"Mix", 100}});
    setRef(a.slot[1], 0x92b586d4u, 0x00000100u, "Lo-Fi Chip",
           {{"Bits", 8}, {"Rate", 4}, {"Ladder", 0.35f}, {"Speaker", 1}, {"Mix", -2.5f}});
    int n = sidecarWrite(a, buf, sizeof(buf));
    printf("---- written (%d B)\n%.*s----\n", n, n, buf);
    check(n > 0 && memcmp(buf, "tffplug 1\n", 10) == 0, "write: the text starts with the magic line");
    check(sidecarRead(buf, n, b) && sameRef(a.slot[0], b.slot[0]) && sameRef(a.slot[1], b.slot[1]),
          "round trip: both slots, uid, version, name with a space, names and values (0.35, -2.5) come back");
    {
        Sidecar full;
        memset(&full, 0, sizeof(full));
        for (int k = 0; k < kSlots; k++)
        {
            full.slot[k].uid = 0xffffffffu;
            full.slot[k].version = 0xffffffffu;
            memset(full.slot[k].name, 'N', kNameLen - 1);
            full.slot[k].count = kMaxParams;
            for (int i = 0; i < kMaxParams; i++)
            {
                memset(full.slot[k].pname[i], 'p', kNameLen - 1);
                full.slot[k].pname[i][0] = char('A' + i % 26);
                full.slot[k].pname[i][1] = char('a' + i / 26);
                full.slot[k].pval[i] = -123456.789f;
            }
        }
        const int m = sidecarWrite(full, buf, sizeof(buf));
        check(m > 0 && sidecarRead(buf, m, b) && sameRef(full.slot[0], b.slot[0]) && sameRef(full.slot[1], b.slot[1]),
              "the largest sidecar (2 slots x 32 parameters, 15 char names) fits kSidecarMax and round trips");
        check(sidecarWrite(full, buf, 100) == -1, "a buffer too small is reported, not truncated silently");
    }
    {
        Sidecar none;
        memset(&none, 0, sizeof(none));
        check(sidecarWrite(none, buf, sizeof(buf)) == 0, "no slot used: write gives 0 (no file)");
    }
    {
        const char* junk = "not a sidecar\nslot 1 3a51c2e0 00010000 Tape\n";
        check(!sidecarRead(junk, int(strlen(junk)), b) && empty(b), "a foreign file reads as empty");
        const char* odd =
          "tffplug 1\r\np Orphan=1\nslot 3 1 1 Three\nslot 2 0 1 ZeroUid\np X=1\nslot 2 abc 10 Hex\r\np Good=2.5\r\np Bad=2.5x\n"
          "p NoValue=\np =3\nweird line\n";
        check(sidecarRead(odd, int(strlen(odd)), b) && b.slot[1].uid == 0xabcu && b.slot[1].version == 0x10u &&
                strcmp(b.slot[1].name, "Hex") == 0 && b.slot[1].count == 1 && strcmp(b.slot[1].pname[0], "Good") == 0 &&
                b.slot[1].pval[0] == 2.5f && b.slot[0].uid == 0,
              "junk lines, CRLF, a parameter before any slot, slot 3, uid 0 and bad values are skipped");
        float v;
        check(sidecarValue(b.slot[1], "Good", v) && v == 2.5f && !sidecarValue(b.slot[1], "Bad", v), "lookup by parameter name");
    }

    // ---- the file rules (what the firmware does on the card)
    const char* ws = "Workspace/plugins.txt";
    const char* proj = "Projects/Song/plugins.txt";
    {   // a project without a sidecar: open, load, save, save to Projects. No file appears anywhere, slots stay empty.
        MemFs fs;
        fs.files["Projects/Song/project.bin"] = "binary project bytes";
        sidecarCopy(fs, proj, ws, buf, sizeof(buf));          // project open
        sidecarLoad(fs, ws, buf, sizeof(buf), b);              // Workspace load
        const bool loadEmpty = empty(b);
        sidecarSave(fs, ws, b, buf, sizeof(buf));              // Workspace save
        sidecarCopy(fs, ws, proj, buf, sizeof(buf));           // project save
        check(loadEmpty && !fs.exists(ws) && !fs.exists(proj) && fs.writes == 0 && fs.files.size() == 1 &&
                fs.files["Projects/Song/project.bin"] == "binary project bytes",
              "a project without sidecar loads with empty slots, and open + save write nothing: the folder is unchanged");
    }
    {   // a project with plugins: open copies the sidecar into the Workspace, the load sees both slots, save writes it back
        MemFs fs;
        n = sidecarWrite(a, buf, sizeof(buf));
        fs.files[proj] = std::string(buf, size_t(n));
        sidecarCopy(fs, proj, ws, buf, sizeof(buf));
        sidecarLoad(fs, ws, buf, sizeof(buf), b);
        const bool loaded = sameRef(a.slot[0], b.slot[0]) && sameRef(a.slot[1], b.slot[1]);
        b.slot[0].pval[0] = 55.0f;   // the user turns Drive
        sidecarSave(fs, ws, b, buf, sizeof(buf));
        sidecarCopy(fs, ws, proj, buf, sizeof(buf));
        Sidecar c;
        sidecarLoad(fs, proj, buf, sizeof(buf), c);
        check(loaded && c.slot[0].pval[0] == 55.0f && sameRef(b.slot[1], c.slot[1]),
              "sidecar round trip through open, load, edit, Workspace save and project save");
        // the user empties both slots and saves: the stale file goes from the Workspace and from the project
        memset(&b, 0, sizeof(b));
        sidecarSave(fs, ws, b, buf, sizeof(buf));
        sidecarCopy(fs, ws, proj, buf, sizeof(buf));
        check(!fs.exists(ws) && !fs.exists(proj), "emptying both slots removes the sidecar from the Workspace and the project");
    }
    {   // opening a project without sidecar after one with plugins: the Workspace copy of the old one must go
        MemFs fs;
        n = sidecarWrite(a, buf, sizeof(buf));
        fs.files[ws] = std::string(buf, size_t(n));
        sidecarCopy(fs, proj, ws, buf, sizeof(buf));
        sidecarLoad(fs, ws, buf, sizeof(buf), b);
        check(!fs.exists(ws) && empty(b), "opening a project without sidecar removes the previous project's from the Workspace");
    }
    {   // a missing plugin keeps its values: what was read is written back unchanged
        MemFs fs;
        n = sidecarWrite(a, buf, sizeof(buf));
        fs.files[ws] = std::string(buf, size_t(n));
        const std::string before = fs.files[ws];
        sidecarLoad(fs, ws, buf, sizeof(buf), b);
        sidecarSave(fs, ws, b, buf, sizeof(buf));
        check(fs.files[ws] == before, "load then save without changes writes the same bytes (a missing plugin keeps its values)");
    }
    printf("%s: %d failed\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
