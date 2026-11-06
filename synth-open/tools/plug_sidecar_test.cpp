// Host test of the plugin host's project sidecar (synth-open/plug_sidecar.h): the text codec and the file rules the firmware
// runs on project load, Workspace save, project open and project save (plug_host.cpp calls the same templates, plug_sidecar_io.h,
// on the SD card), on a card with FatFs rename rules that fails an open, a read, a write or a rename, or loses power at any step.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_sidecar_test.cpp \
//       -o /tmp/plug_sidecar_test && /tmp/plug_sidecar_test
#include "plug_sidecar_io.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace plug;

namespace
{
int g_fail = 0;
SidecarState g_st = SidecarState::kNone;   // what the last Workspace load found, as plug_host.cpp's s_sideState
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

// An in-memory card: path -> bytes, FatFs rules (rename fails onto an existing name). Faults:
//   unknown         stat of these paths answers kUnknown (a card error), openFail: readFile fails, readFailAfter: a read
//                   fails after that many bytes (the SD read error mid file, SdFile::read gives -1)
//   writeFail       writeNew of these paths fails after writing half (a partial file is left, as a full card does)
//   renameFail      the n-th rename from now fails (0: none), cut: the power goes before the n-th mutation from now (0: none):
//                   that one and every later operation fails, the card keeps what it had
struct MemFs
{
    std::map<std::string, std::string> files;
    std::set<std::string> unknown, openFail, writeFail;
    int readFailAfter = -1;
    int renameFail = 0, cut = 0;
    bool dead = false;
    int writes = 0, removes = 0, renames = 0, mutations = 0;
    bool step()   // one mutation: false when the power is gone
    {
        if (dead) return false;
        mutations++;
        if (cut && --cut == 0)
        {
            dead = true;
            return false;
        }
        return true;
    }
    void reboot()   // power back, every fault gone
    {
        dead = false;
        cut = renameFail = 0;
        readFailAfter = -1;
        unknown.clear();
        openFail.clear();
        writeFail.clear();
    }
    Presence stat(const char* p)
    {
        if (dead || unknown.count(p)) return Presence::kUnknown;
        return files.count(p) ? Presence::kPresent : Presence::kAbsent;
    }
    int readFile(const char* p, char* buf, int cap)
    {
        auto it = files.find(p);
        if (dead || unknown.count(p) || openFail.count(p) || it == files.end()) return -1;
        if (readFailAfter >= 0 && int(it->second.size()) > readFailAfter) return -1;
        if (int(it->second.size()) > cap) return -1;
        memcpy(buf, it->second.data(), it->second.size());
        return int(it->second.size());
    }
    bool writeNew(const char* p, const char* buf, int n)
    {
        if (!step()) return false;
        writes++;
        if (writeFail.count(p))
        {
            files[p] = std::string(buf, size_t(n / 2));
            return false;
        }
        files[p] = std::string(buf, size_t(n));
        return true;
    }
    bool remove(const char* p)
    {
        if (!files.count(p) || !step()) return false;
        removes++;
        files.erase(p);
        return true;
    }
    bool rename(const char* from, const char* to)
    {
        if (!files.count(from) || files.count(to) || !step()) return false;
        renames++;
        if (renameFail && --renameFail == 0) return false;
        files[to] = files[from];
        files.erase(from);
        return true;
    }
    bool exists(const char* p) { return files.count(p) != 0; }
    // only the canonical names: no .tmp or .bak left over
    bool clean(std::initializer_list<const char*> keep)
    {
        for (auto& [k, v] : files)
        {
            bool ok = false;
            for (const char* q : keep) ok = ok || k == q;
            if (!ok) return false;
        }
        return true;
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
    check(n > 0 && memcmp(buf, "tffplug 1\nformat 2\n", 19) == 0,
          "write: the magic line (older hosts read it), then format 2 (the Inserts entry exists)");
    check(sidecarRead(buf, n, b) && sameRef(a.slot[0], b.slot[0]) && sameRef(a.slot[1], b.slot[1]) && b.format == 2,
          "round trip: format 2, both slots, uid, version, name with a space, names and values (0.35, -2.5) come back");
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
    {   // a project saved by the Dirt with the single Preset parameter: the host asks for each parameter of the plugin by
        // name, so the stored Preset is never asked for (the new Dirt starts at its defaults) and nothing fails
        const char* old = "tffplug 1\nformat 2\nslot 1 92b586d4 10000 Dirt\np Preset=3\n";
        float v = -1.0f;
        check(sidecarRead(old, int(strlen(old)), b) && b.slot[0].uid == 0x92b586d4u && b.slot[0].count == 1 &&
                !sidecarValue(b.slot[0], "Type", v) && !sidecarValue(b.slot[0], "Drive", v) && !sidecarValue(b.slot[0], "Mix", v) &&
                v == -1.0f && sidecarValue(b.slot[0], "Preset", v) && v == 3.0f,
              "an old Dirt sidecar (Preset only) reads, the new parameter names find no value and keep their defaults");
    }

    // ---- the file rules (what the firmware does on the card)
    const char* ws = "Workspace/plugins.txt";
    const char* proj = "Projects/Song/plugins.txt";
    {   // a project without a sidecar: open, load, save, save to Projects. No file appears anywhere, slots stay empty.
        MemFs fs;
        fs.files["Projects/Song/project.bin"] = "binary project bytes";
        sidecarCopy(fs, proj, ws, buf, sizeof(buf));          // project open
        g_st = sidecarLoad(fs, ws, buf, sizeof(buf), b);              // Workspace load
        const bool loadEmpty = empty(b);
        sidecarSave(fs, ws, b, g_st, buf, sizeof(buf));              // Workspace save
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
        g_st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
        const bool loaded = sameRef(a.slot[0], b.slot[0]) && sameRef(a.slot[1], b.slot[1]);
        b.slot[0].pval[0] = 55.0f;   // the user turns Drive
        sidecarSave(fs, ws, b, g_st, buf, sizeof(buf));
        sidecarCopy(fs, ws, proj, buf, sizeof(buf));
        Sidecar c;
        sidecarLoad(fs, proj, buf, sizeof(buf), c);
        check(loaded && c.slot[0].pval[0] == 55.0f && sameRef(b.slot[1], c.slot[1]),
              "sidecar round trip through open, load, edit, Workspace save and project save");
        // the user empties both slots and saves: the stale file goes from the Workspace and from the project
        memset(&b, 0, sizeof(b));
        sidecarSave(fs, ws, b, g_st, buf, sizeof(buf));
        sidecarCopy(fs, ws, proj, buf, sizeof(buf));
        check(!fs.exists(ws) && !fs.exists(proj), "emptying both slots removes the sidecar from the Workspace and the project");
    }
    {   // opening a project without sidecar after one with plugins: the Workspace copy of the old one must go
        MemFs fs;
        n = sidecarWrite(a, buf, sizeof(buf));
        fs.files[ws] = std::string(buf, size_t(n));
        sidecarCopy(fs, proj, ws, buf, sizeof(buf));
        g_st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
        check(!fs.exists(ws) && empty(b), "opening a project without sidecar removes the previous project's from the Workspace");
    }
    {   // a missing plugin keeps its values: what was read is written back unchanged
        MemFs fs;
        n = sidecarWrite(a, buf, sizeof(buf));
        fs.files[ws] = std::string(buf, size_t(n));
        const std::string before = fs.files[ws];
        g_st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
        sidecarSave(fs, ws, b, g_st, buf, sizeof(buf));
        check(fs.files[ws] == before, "load then save without changes writes the same bytes (a missing plugin keeps its values)");
    }

    // ---- card errors and power cuts (B2, B3, B4 of the 1.2.0 review): a sidecar the card holds is never lost or overwritten
    // by a failure, every failure is reported (the file manager then keeps the project unsaved and shows the error)
    {
        static Sidecar A, B, P;
        memset(&A, 0, sizeof(A));
        memset(&B, 0, sizeof(B));
        memset(&P, 0, sizeof(P));
        setRef(A.slot[0], 0x3a51c2e0u, 0x00010000u, "Tape", {{"Drive", 30}});
        setRef(B.slot[0], 0x3a51c2e0u, 0x00010000u, "Tape", {{"Drive", 77}});
        setRef(B.slot[1], 0x92b586d4u, 0x00000100u, "Dirt", {{"Mix", 50}});
        setRef(P.slot[1], 0x11111111u, 1, "Other", {{"Mix", 1}});
        auto text = [&](const Sidecar& x) {
            const int m = sidecarWrite(x, buf, sizeof(buf));
            return std::string(buf, size_t(m));
        };
        const std::string tA = text(A), tB = text(B), tP = text(P);
        auto is = [&](const Sidecar& x, const Sidecar& y) { return sameRef(x.slot[0], y.slot[0]) && sameRef(x.slot[1], y.slot[1]); };
        const std::string wsTmp = std::string(ws) + ".tmp", wsBak = std::string(ws) + ".bak";

        // B3: the power goes before every mutation of a Workspace save of B over A (write .tmp, A to .bak, .tmp to the name,
        // remove .bak). After the reboot the load finds A or B, never nothing; the next save completes and leaves no .tmp/.bak
        int cuts = 0, sawOld = 0, sawNew = 0;
        bool never = true, recovers = true;
        for (int k = 1; k <= 12; k++)
        {
            MemFs fs;
            fs.files[ws] = tA;
            SidecarState st = SidecarState::kLoaded;
            fs.cut = k;
            const bool ok = sidecarSave(fs, ws, B, st, buf, sizeof(buf));
            if (!fs.dead) break;   // k is past the last mutation: the save ran to the end
            cuts++;
            fs.reboot();
            st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
            const bool old = st == SidecarState::kLoaded && is(b, A), nw = st == SidecarState::kLoaded && is(b, B);
            sawOld += old;
            sawNew += nw;
            never = never && (old || nw) && (!ok || nw);   // true only once the new file is in place (the .bak removal is best effort)
            recovers = recovers && sidecarSave(fs, ws, B, st, buf, sizeof(buf)) && fs.files[ws] == tB && fs.clean({ws});
        }
        check(cuts >= 4 && never && sawOld && sawNew,
              "power cut at each step of a Workspace save: the load finds the old or the new settings, never none (B3)");
        printf("      %d cut points, old %d, new %d\n", cuts, sawOld, sawNew);
        check(recovers, "after each cut the next save completes, no .tmp or .bak is left");
        {
            MemFs fs;
            fs.files[wsBak] = tA;   // a cut between the two renames: only the .bak
            check(sidecarLoad(fs, ws, buf, sizeof(buf), b) == SidecarState::kLoaded && is(b, A) && fs.files[ws] == tA &&
                    !fs.exists(wsBak.c_str()),
                  "the load moves an interrupted replace's plugins.txt.bak back");
            MemFs t;
            t.files[wsTmp] = tB;   // a .tmp is never promoted: it may be half written
            check(sidecarLoad(t, ws, buf, sizeof(buf), b) == SidecarState::kNone && empty(b), "a lone plugins.txt.tmp is not read");
        }
        {   // a failed rename at each of the three renames, a failed write of the .tmp: false, A stays readable
            bool all = true;
            for (int r = 1; r <= 3; r++)
            {
                MemFs fs;
                fs.files[ws] = tA;
                SidecarState st = SidecarState::kLoaded;
                fs.renameFail = r;
                const bool ok = sidecarSave(fs, ws, B, st, buf, sizeof(buf));
                fs.reboot();
                const SidecarState l = sidecarLoad(fs, ws, buf, sizeof(buf), b);
                all = all && l == SidecarState::kLoaded && (ok ? is(b, B) : is(b, A));
            }
            MemFs fs;
            fs.files[ws] = tA;
            fs.writeFail.insert(wsTmp);
            SidecarState st = SidecarState::kLoaded;
            const bool ok = sidecarSave(fs, ws, B, st, buf, sizeof(buf));
            check(all && !ok && fs.files[ws] == tA && !fs.exists(wsTmp.c_str()),
                  "a failed rename or a failed write (card full) returns false and the old file stays (B3, B4)");
        }

        // B2: a read error on load is not "no file"
        for (int mode = 0; mode < 3; mode++)
        {
            MemFs fs;
            fs.files[ws] = tA;
            if (mode == 0) fs.openFail.insert(ws);
            if (mode == 1) fs.readFailAfter = 4;
            if (mode == 2) fs.unknown.insert(ws);
            const SidecarState st0 = sidecarLoad(fs, ws, buf, sizeof(buf), b);
            fs.reboot();   // the card recovers, the user goes on
            SidecarState st = st0;
            const bool emptySave = sidecarSave(fs, ws, b, st, buf, sizeof(buf));   // an ordinary edit, no insert touched
            const bool keptEmpty = fs.files[ws] == tA;
            Sidecar picked = b;
            setRef(picked.slot[0], 0x22222222u, 1, "New", {{"Mix", 3}});
            const bool pickSave = sidecarSave(fs, ws, picked, st, buf, sizeof(buf));
            static const char* what[3] = {"failed open", "read error mid file", "card error on stat"};
            char line[160];
            snprintf(line, sizeof(line),
                     "%s on load: unreadable, empty, a later save keeps the file (true), one with a slot used fails (B2)",
                     what[mode]);
            check(st0 == SidecarState::kUnreadable && empty(b) && emptySave && keptEmpty && !pickSave && fs.files[ws] == tA &&
                    st == SidecarState::kUnreadable && fs.writes == 0 && fs.removes == 0,
                  line);
            check(sidecarLoad(fs, ws, buf, sizeof(buf), b) == SidecarState::kLoaded && is(b, A),
                  "  and loading the project again reads the settings");
        }
        {   // the project save after such a load: the Workspace file is copied when it reads now, never removed from the project
            MemFs fs;
            fs.files[ws] = tA;
            fs.files[proj] = tA;
            fs.openFail.insert(ws);
            SidecarState st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
            const bool failCopy = !sidecarProjectStep(fs, ws, proj, b, st, buf, sizeof(buf));   // still unreadable: false
            const bool kept = fs.files[proj] == tA;
            fs.reboot();
            const bool copied = sidecarProjectStep(fs, ws, proj, b, st, buf, sizeof(buf)) && fs.files[proj] == tA;
            fs.files.erase(ws);   // and a Workspace that says "no file" does not remove the project's
            const bool keep2 = sidecarProjectStep(fs, ws, proj, b, st, buf, sizeof(buf)) && fs.files[proj] == tA;
            check(st == SidecarState::kUnreadable && failCopy && kept && copied && keep2,
                  "project save after an unreadable load: false while the card fails, the project's file is never removed (B2, B4)");
        }
        {   // sidecarCopy: a source that cannot be read leaves the destination (it used to remove it)
            MemFs fs;
            fs.files[ws] = tB;
            fs.files[proj] = tA;
            fs.openFail.insert(ws);
            check(!sidecarCopy(fs, ws, proj, buf, sizeof(buf)) && fs.files[proj] == tA, "a copy from an unreadable source: false, the destination kept");
            fs.reboot();
            fs.unknown.insert(proj);
            check(!sidecarCopy(fs, wsBak.c_str(), proj, buf, sizeof(buf)) && fs.files[proj] == tA,
                  "a copy of no file onto a destination the card cannot stat: false, nothing removed");
        }
        {   // project open with the project's file unreadable: the previous project's Workspace file is not used, the project's kept
            MemFs fs;
            fs.files[ws] = tP;     // the previous project
            fs.files[proj] = tA;   // this project's, unreadable now
            fs.openFail.insert(proj);
            bool stale = false;
            const bool opened = sidecarOpenStep(fs, proj, ws, buf, sizeof(buf), stale);
            const bool prevGone = !fs.exists(ws);
            fs.reboot();
            SidecarState st = sidecarLoadStep(fs, ws, buf, sizeof(buf), b, stale);
            const bool loadOk = st == SidecarState::kStale && empty(b) && !stale;
            const bool wsSave = sidecarSave(fs, ws, b, st, buf, sizeof(buf)) && !fs.exists(ws);
            const bool projSave = sidecarProjectStep(fs, ws, proj, b, st, buf, sizeof(buf)) && fs.files[proj] == tA;
            Sidecar picked = b;
            setRef(picked.slot[0], 0x22222222u, 1, "New", {{"Mix", 3}});
            const bool pickFails = !sidecarSave(fs, ws, picked, st, buf, sizeof(buf)) &&
                                   !sidecarProjectStep(fs, ws, proj, picked, st, buf, sizeof(buf)) && fs.files[proj] == tA;
            check(!opened && stale == false && prevGone && loadOk && wsSave && projSave && pickFails,
                  "open with an unreadable project sidecar: empty, the previous project's file gone, saves keep the project's (B2)");
            fs.files[ws] = tP;   // and when the card does not even let the previous file go: still not used
            fs.unknown.insert(ws);
            stale = false;
            sidecarOpenStep(fs, proj, ws, buf, sizeof(buf), stale);
            fs.reboot();
            st = sidecarLoadStep(fs, ws, buf, sizeof(buf), b, stale);
            check(stale == false && st == SidecarState::kStale && empty(b) && fs.files[proj] == tA,
                  "  and with the previous file stuck in the Workspace its slots are not loaded either");
            stale = false;   // the card is back: open again reads it
            check(sidecarOpenStep(fs, proj, ws, buf, sizeof(buf), stale) && !stale &&
                    sidecarLoadStep(fs, ws, buf, sizeof(buf), b, stale) == SidecarState::kLoaded && is(b, A),
                  "  opening the project again loads its settings");
        }
        {   // power cut at each step of the project save copy (B over A) and of the open copy (A over the previous project's P)
            bool save = true, open = true;
            int n1 = 0, n2 = 0;
            for (int k = 1; k <= 12; k++)
            {
                MemFs fs;
                fs.files[ws] = tB;
                fs.files[proj] = tA;
                fs.cut = k;
                sidecarProjectStep(fs, ws, proj, B, SidecarState::kLoaded, buf, sizeof(buf));
                if (!fs.dead) break;
                n1++;
                fs.reboot();
                Sidecar c;
                const SidecarState l = sidecarLoad(fs, proj, buf, sizeof(buf), c);
                save = save && l == SidecarState::kLoaded && (is(c, A) || is(c, B));
            }
            for (int k = 1; k <= 12; k++)
            {
                MemFs fs;
                fs.files[ws] = tP;
                fs.files[proj] = tA;
                fs.cut = k;
                bool stale = false;
                sidecarOpenStep(fs, proj, ws, buf, sizeof(buf), stale);
                if (!fs.dead) break;
                n2++;
                fs.reboot();   // the card back within the session (a real power cut also loses `stale` and the whole open)
                SidecarState st = sidecarLoadStep(fs, ws, buf, sizeof(buf), b, stale);
                const bool safe = st == SidecarState::kStale || (st == SidecarState::kLoaded && is(b, A));
                sidecarSave(fs, ws, b, st, buf, sizeof(buf));
                sidecarProjectStep(fs, ws, proj, b, st, buf, sizeof(buf));
                open = open && safe && fs.files[proj] == tA;
            }
            check(n1 >= 4 && save, "power cut at each step of the project save copy: the project has the old or the new settings");
            check(n2 >= 1 && open, "power cut during the open copy: the previous project's slots are never loaded, the project's file stays");
        }
        {   // removing (both slots emptied) with a cut at each step: old or none, never an older file brought back
            bool all = true;
            for (int k = 1; k <= 4; k++)
            {
                MemFs fs;
                fs.files[ws] = tA;
                fs.files[wsBak] = tP;   // an orphan of an earlier failed replace
                SidecarState st = SidecarState::kLoaded;
                Sidecar none;
                memset(&none, 0, sizeof(none));
                fs.cut = k;
                sidecarSave(fs, ws, none, st, buf, sizeof(buf));
                fs.reboot();
                const SidecarState l = sidecarLoad(fs, ws, buf, sizeof(buf), b);
                all = all && !(l == SidecarState::kLoaded && is(b, P)) && (l == SidecarState::kNone || is(b, A));
            }
            check(all, "emptying both slots with a cut at each step: the old settings or none, an orphan .bak never comes back");
        }
        {   // a foreign file is kept by an empty save, replaced by a slot in use
            MemFs fs;
            fs.files[ws] = "a file of something else\n";
            SidecarState st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
            Sidecar none;
            memset(&none, 0, sizeof(none));
            const bool kept = sidecarSave(fs, ws, none, st, buf, sizeof(buf)) && fs.exists(ws);
            check(st == SidecarState::kForeign && kept && sidecarSave(fs, ws, A, st, buf, sizeof(buf)) && fs.files[ws] == tA &&
                    st == SidecarState::kLoaded,
                  "a foreign plugins.txt: read as empty, kept by a save with no slot, replaced when a slot is used");
        }
        {   // a file larger than the buffer is not read as a truncated sidecar
            MemFs fs;
            fs.files[ws] = tA + std::string(size_t(kSidecarMax), '#');
            check(sidecarLoad(fs, ws, buf, sizeof(buf), b) == SidecarState::kUnreadable && empty(b),
                  "a plugins.txt larger than kSidecarMax: unreadable (kept), not cut");
        }
    }

    // ---- the Inserts entry: saturationType kSatInserts plays the inserts, a built-in entry bypasses and keeps them
    // A sidecar as b8e37c8 wrote it (no format line): Tape picked in the Saturation list, Lo-Fi on the Inserts page
    const char* oldFile = "tffplug 1\nslot 1 3a51c2e0 00010000 Tape\np Drive=30\nslot 2 92b586d4 00000100 Lo-Fi\np Bits=8\n";
    check(sidecarRead(oldFile, int(strlen(oldFile)), b) && b.format == 1 && b.slot[0].uid == 0x3a51c2e0u &&
            b.slot[1].uid == 0x92b586d4u && b.slot[0].count == 1,
          "old sidecar (b8e37c8, no format line) still loads: format 1, both slots and values");
    {
        uint8_t sat = kSatHardClip;
        check(insertsOnAfterLoad(sat, b) && sat == kSatInserts,
              "old sidecar + Hard Clip (what a plugin pick left in the project): turns into Inserts, plays as before");
        sat = 1;   // Soft Clip
        check(!insertsOnAfterLoad(sat, b) && sat == 1, "old sidecar + a built-in other than Hard Clip: kept, inserts bypassed");
        Sidecar none;
        memset(&none, 0, sizeof(none));
        none.format = 1;
        sat = kSatHardClip;
        check(!insertsOnAfterLoad(sat, none) && sat == kSatHardClip, "old sidecar with no slot: Hard Clip stays Hard Clip");
    }
    {
        n = sidecarWrite(a, buf, sizeof(buf));
        sidecarRead(buf, n, b);
        uint8_t sat = kSatHardClip;
        check(!insertsOnAfterLoad(sat, b) && sat == kSatHardClip,
              "new sidecar + Hard Clip picked by the user: stays bypassed (no migration for format 2)");
        sat = kSatInserts;
        check(insertsOnAfterLoad(sat, b) && sat == kSatInserts, "new sidecar + Inserts: on");
        Sidecar none;
        memset(&none, 0, sizeof(none));
        sat = kSatInserts;
        check(insertsOnAfterLoad(sat, none), "Inserts with no sidecar (both slots empty): still the Inserts entry");
        sat = 4;
        check(!insertsOnAfterLoad(sat, none) && sat == 4, "a stock project (no sidecar): untouched");
    }
    {   // bypass keeps them: a project saved under a built-in entry keeps the sidecar, Inserts again finds both slots
        MemFs fs;
        n = sidecarWrite(a, buf, sizeof(buf));
        fs.files[ws] = std::string(buf, size_t(n));
        g_st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
        uint8_t sat = 2;   // Saturate picked: bypassed, the host saves the same references
        const bool off = !insertsOnAfterLoad(sat, b);
        sidecarSave(fs, ws, b, g_st, buf, sizeof(buf));
        sidecarCopy(fs, ws, proj, buf, sizeof(buf));
        Sidecar c;
        sidecarLoad(fs, proj, buf, sizeof(buf), c);
        sat = kSatInserts;   // the user picks Inserts again
        check(off && sameRef(a.slot[0], c.slot[0]) && sameRef(a.slot[1], c.slot[1]) && insertsOnAfterLoad(sat, c),
              "a built-in entry keeps both slots and values through save, Inserts again brings both back");
    }
    {   // an old file becomes format 2 on the first save and keeps its meaning
        MemFs fs;
        fs.files[ws] = oldFile;
        g_st = sidecarLoad(fs, ws, buf, sizeof(buf), b);
        uint8_t sat = kSatHardClip;
        const bool on1 = insertsOnAfterLoad(sat, b);   // migrated, the binary project now holds kSatInserts
        sidecarSave(fs, ws, b, g_st, buf, sizeof(buf));
        Sidecar c;
        sidecarLoad(fs, ws, buf, sizeof(buf), c);
        const bool on2 = insertsOnAfterLoad(sat, c);
        uint8_t stockSaved = kSatHardClip;   // stock firmware saved the project in between: it clamps kSatInserts
        check(on1 && on2 && c.format == 2 && fs.files[ws].find("format 2\n") != std::string::npos &&
                !insertsOnAfterLoad(stockSaved, c),
              "old file: migrated once, saved as format 2, Inserts stays on; a stock save (Hard Clip) leaves it bypassed");
    }

    // ---- the Saturation value text (9 characters fit the master screen field)
    {
        struct
        {
            const char *a, *b, *want;
        } t[] = {
            {"", "", "Inserts"},         {"Tape", "", "Ins: Tape"},     {"", "Dirt", "Ins: Dirt"},
            {"Lo-Fi", "", "Ins:Lo-Fi"},  {"Tape", "Dirt", "Tape+Dirt"}, {"Lo-Fi", "Tape", "Lo-F+Tape"},
            {"Dirt", "Lo-Fi", "Dirt+Lo-F"}, {"Saturator Pro X", "", "Ins:Satur"}, {"A", "Bcdefghijklmnop", "A+Bcdefgh"},
            {"Abcdefghijklmno", "Pqrstuvwxyzabcd", "Abcd+Pqrs"},
        };
        bool all = true;
        for (auto& x : t)
        {
            char out[16];
            insertsLabel(x.a, x.b, out, 9);
            if (strcmp(out, x.want))
            {
                printf("      insertsLabel(\"%s\", \"%s\") = \"%s\", want \"%s\"\n", x.a, x.b, out, x.want);
                all = false;
            }
        }
        check(all, "Inserts value: Inserts / Ins: Tape / Ins:Lo-Fi / Tape+Dirt / Lo-F+Tape and long names cut to 9");
        char wide[32];
        insertsLabel("Tape", "Dirt", wide, 20);
        check(strcmp(wide, "Ins: Tape+Dirt") == 0, "a wider field keeps the full \"Ins: Tape+Dirt\"");
        bool fits = true;
        for (int la = 0; la < kNameLen; la++)
            for (int lb = 0; lb < kNameLen; lb++)
            {
                char na[kNameLen] = {}, nb[kNameLen] = {}, out[16];
                memset(na, 'a', size_t(la));
                memset(nb, 'b', size_t(lb));
                insertsLabel(na, nb, out, 9);
                const int l = int(strlen(out));
                const bool two = la && lb;
                fits = fits && l >= 1 && l <= 9 && (!two || (strchr(out, 'a') && strchr(out, '+') && strchr(out, 'b')));
            }
        check(fits, "every name length pair (0..15): at most 9 characters, both names and the + when two are loaded");
    }

    // ---- the mixer stage under Inserts (the "mixer" line, Hard Clip without one)
    {
        Sidecar m;
        sidecarRead(oldFile, int(strlen(oldFile)), m);
        check(m.mixer == kSatHardClip, "old sidecar (no mixer line): mixer stage Hard Clip");
        n = sidecarWrite(a, buf, sizeof(buf));
        check(std::string(buf, size_t(n)).find("mixer") == std::string::npos, "Hard Clip writes no mixer line (9461f16 bytes)");
        Sidecar k = a;
        k.mixer = kSatSoftKnee;
        n = sidecarWrite(k, buf, sizeof(buf));
        Sidecar r;
        check(std::string(buf, size_t(n)).find("format 2\nmixer 10\n") != std::string::npos && sidecarRead(buf, n, r) &&
                r.mixer == kSatSoftKnee && sameRef(r.slot[0], a.slot[0]) && sameRef(r.slot[1], a.slot[1]),
              "Soft Knee round-trips as \"mixer 10\" after the format line, the slots unchanged");
        k.mixer = kSatSoftClip;
        n = sidecarWrite(k, buf, sizeof(buf));
        check(sidecarRead(buf, n, r) && r.mixer == kSatSoftClip, "Soft Clip round-trips (mixer 1)");
        const char* bad = "tffplug 1\nformat 2\nmixer 3\n";
        check(sidecarRead(bad, int(strlen(bad)), r) && r.mixer == kSatHardClip, "mixer 3 (Overdrive, not a stage) reads as Hard Clip");
        Sidecar only;
        memset(&only, 0, sizeof(only));
        only.mixer = kSatSoftClip;
        n = sidecarWrite(only, buf, sizeof(buf));
        check(n > 0 && sidecarRead(buf, n, r) && r.mixer == kSatSoftClip && !r.slot[0].uid,
              "no plugin but Soft Clip under Inserts: a file is written (the stage is kept)");
        only.mixer = kSatHardClip;
        check(sidecarWrite(only, buf, sizeof(buf)) == 0, "no plugin and Hard Clip: no file, as before");
        uint8_t stockSat = kSatInserts;   // stock firmware: kSatInserts >= its kSaturationTypeEnd (5) is clamped to Hard Clip
        if (stockSat >= 5) stockSat = kSatHardClip;
        check(stockSat == kSatHardClip, "stock firmware opens an Inserts project with any mixer stage as Hard Clip");
        uint8_t knee = kSatSoftKnee;   // a built-in Soft Knee project on stock (end 5) and on FF 1.1.0 (end 10)
        check(knee >= 5 && knee >= 10, "Soft Knee (10) is outside stock and FF 1.1.0 lists: both load it as Hard Clip");
    }

    // ---- built-in Dirt entries (5..9) in a build without them: Inserts with Insert 1 = Dirt, or Hard Clip
    {
        const uint8_t want[5][5] = {{0, 35, 16, 1, 100}, {1, 35, 16, 1, 100}, {2, 35, 16, 1, 100}, {3, 35, 16, 1, 100},
                                    {0, 15, 8, 4, 70}};
        bool all = true;
        for (int k = 0; k < 5; k++)
        {
            Sidecar d;
            memset(&d, 0, sizeof(d));
            uint8_t sat = uint8_t(kSatDirtFirst + k);
            const int r = dirtMigrate(sat, d, true, 0x00010000u);
            float v[6] = {};
            const char* nm[6] = {"Type", "Drive", "Bits", "Rate", "Tone", "Mix"};
            bool vals = true;
            for (int i = 0; i < 6; i++) vals = vals && sidecarValue(d.slot[0], nm[i], v[i]) && v[i] == (i < 5 ? want[k][i] : 100);
            all = all && r == 1 && sat == kSatInserts && d.slot[0].uid == kDirtUid && !strcmp(d.slot[0].name, "Dirt") && vals &&
                  d.mixer == kSatHardClip && insertsOnAfterLoad(sat, d);
        }
        check(all, "Dirt Soft/Hard/Fold/Asym/Crush -> Inserts, insert 1 Dirt at the preset (Crush: Drive 15, Bits 8, Rate 4, Tone 70)");
        Sidecar d;
        memset(&d, 0, sizeof(d));
        uint8_t sat = 9;
        check(dirtMigrate(sat, d, false, 0) == 2 && sat == kSatHardClip && !d.slot[0].uid && !insertsOnAfterLoad(sat, d),
              "Dirt Crush without dirt.plg: Hard Clip, no slot");
        bool none = true;
        for (int v : {0, 1, 2, 3, 4, 10, int(kSatInserts)})
        {
            uint8_t t = uint8_t(v);
            memset(&d, 0, sizeof(d));
            none = none && dirtMigrate(t, d, true, 1) == 0 && t == v && !d.slot[0].uid;
        }
        check(none, "every other value (built-ins, Soft Knee, Inserts) is left alone");
        // a 9461f16 project: Dirt Asym picked, Tape bypassed in insert 1, Lo-Fi in insert 2
        Sidecar p;
        n = sidecarWrite(a, buf, sizeof(buf));
        sidecarRead(buf, n, p);
        p.mixer = kSatSoftClip;
        sat = 8;
        check(dirtMigrate(sat, p, true, 0x00010000u) == 1 && p.slot[0].uid == kDirtUid && sameRef(p.slot[1], a.slot[1]) &&
                p.mixer == kSatHardClip,
              "a PLUGH project with a Dirt entry and bypassed inserts: insert 1 becomes Dirt, insert 2 stays, mixer Hard Clip");
        n = sidecarWrite(p, buf, sizeof(buf));
        Sidecar q;
        check(sidecarRead(buf, n, q) && q.slot[0].uid == kDirtUid && q.slot[0].count == 6, "the migrated sidecar saves and reads back");
    }
    printf("%s: %d failed\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
