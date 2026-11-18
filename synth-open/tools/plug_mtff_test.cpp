// Host test of project.mtff (synth-open/plug_mtff.h): the text codec and the file rules the firmware runs on project load, Workspace
// save, project open and Save / Save As (plug_host.cpp calls the same templates on the SD card), on an in-memory card with FatFs
// rename rules that fails or loses power at any step. NOT firmware.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open -I synth-open/tools/plug_stub synth-open/tools/plug_mtff_test.cpp \
//       -o /tmp/plug_mtff_test && /tmp/plug_mtff_test
#include "plug_memfs.h"

// mtffMigrate records its calls here (the hook is empty in the firmware)
#include <vector>
struct MigrateCall
{
    unsigned fileFormat, ourFormat;
    unsigned long fileFf, ourFf;
    int depth;   // m.depth as the hook saw it
};
static std::vector<MigrateCall> g_migrate;
#define MTFF_MIGRATE_PROBE(m, file, ours) g_migrate.push_back({file.format, ours.format, file.ff, ours.ff, int((m).depth)})
#include "plug_mtff.h"

#include <cstdio>
#include <string>

using namespace plug;
using plug_test::MemFs;

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

constexpr const char* kWs = "Workspace/project.mtff";
constexpr const char* kProj = "Projects/Song/project.mtff";

// one project.mtff the way plug_host.cpp holds it: raw text, parsed state, what the load found
struct Host
{
    char raw[kMtffMax];
    int rawN = 0;
    Mtff m{};
    SidecarState st = SidecarState::kNone;
    bool openStale = false;
    MtStamp stamp{};
    char tmp[kMtffMax];
    parts::List pl{};
    Host() { m.parts = &pl; }
};

SidecarState load(MemFs& fs, Host& h, const char* path = kWs)
{
    h.st = mtffLoadStep(fs, path, h.raw, kMtffMax, h.m, h.rawN, h.openStale, h.stamp, h.tmp, sizeof(h.tmp));
    return h.st;
}
bool save(MemFs& fs, Host& h, int depth, const char* path = kWs)
{
    h.m.depth = uint8_t(depth);
    h.m.hasStamp = h.stamp.valid;
    h.m.crc = h.stamp.crc;
    h.m.size = h.stamp.size;
    return mtffSave(fs, path, h.m, h.st, h.raw, h.rawN, kMtffMax, h.tmp, sizeof(h.tmp));
}
bool hasContent(const Host& h)
{
    char t[kMtffMax + 64];
    return mtffWrite(h.m, h.raw, h.rawN, t, sizeof(t)) != 0;
}
std::string text(const char* b, int n) { return std::string(b, size_t(n)); }
}   // namespace

int main()
{
    char out[4096];

    // ---- codec
    {
        Mtff m{};
        check(mtffWrite(m, "", 0, out, sizeof(out)) == 0, "codec: depth 0 and nothing kept is no file (write gives 0)");
        m.hasStamp = true;
        m.crc = 0x3a51c2e0u;
        m.size = 2324;
        check(mtffWrite(m, "", 0, out, sizeof(out)) == 0, "codec: a stamp alone is no file either");
        m.depth = 100;
        int n = mtffWrite(m, "", 0, out, sizeof(out));
        check(text(out, n) == "tffproj 1\nmt 3a51c2e0 2324\nstrum.ramp 100\n", "codec: header, stamp, strum.ramp 100");
        Mtff r;
        check(mtffRead(out, n, r) && r.depth == 100 && r.hasStamp && r.crc == 0x3a51c2e0u && r.size == 2324 && !r.newer, "codec: round trip");
        m.depth = 37;
        n = mtffWrite(m, "", 0, out, sizeof(out));
        check(mtffRead(out, n, r) && r.depth == 37, "codec: depth 37 round trips");
        const char* noKey = "tffproj 1\nmt 00000001 10\n";
        check(mtffRead(noKey, int(strlen(noKey)), r) && r.depth == 0, "codec: no strum.ramp line reads as 0 (off, the 1.3.0 sound)");
        const char* big = "tffproj 1\nstrum.ramp 250\n";
        check(mtffRead(big, int(strlen(big)), r) && r.depth == 100, "codec: a value over 100 reads as 100");
        const char* junk = "tffproj 1\nstrum.ramp abc\nstrum.ramp\nstrum.rampX 5\nmt zz 1\n";
        check(mtffRead(junk, int(strlen(junk)), r) && r.depth == 0 && !r.hasStamp, "codec: a value that does not parse, a key prefix and a bad stamp are ignored");
        const char* crlf = "tffproj 1\r\nmt 0000abcd 2324\r\nstrum.ramp 60\r\n";
        check(mtffRead(crlf, int(strlen(crlf)), r) && r.depth == 60 && r.hasStamp && r.crc == 0xabcdu && r.size == 2324, "codec: CRLF lines");
        const char* none = "not a project file\nstrum.ramp 100\n";
        check(!mtffRead(none, int(strlen(none)), r) && r.depth == 0, "codec: a file without the first line is not a project.mtff");
        const char* newer = "tffproj 2\nstrum.ramp 100\nfuture 1\n";
        check(mtffRead(newer, int(strlen(newer)), r) && r.newer && r.depth == 0, "codec: a higher major reads as newer, nothing applied");
        check(!mtffRead("", 0, r), "codec: an empty file is not a project.mtff");
    }
    {   // unknown lines are kept, byte exact, in file order, after the known ones
        const char* raw = "tffproj 1\nmt 11111111 99\nx.future-key some value  with spaces\nstrum.ramp 80\nshadow.sat 128\nx.crlf 1\r\nparked.part.1.engine 83 Pluck\n";
        Mtff m{};
        check(mtffRead(raw, int(strlen(raw)), m) && m.depth == 80, "keep: parse");
        m.hasStamp = true;
        m.crc = 0xdeadbeefu;
        m.size = 2324;
        m.depth = 55;
        const int n = mtffWrite(m, raw, int(strlen(raw)), out, sizeof(out));
        check(text(out, n) == "tffproj 1\nmt deadbeef 2324\nstrum.ramp 55\nx.future-key some value  with spaces\nshadow.sat 128\nx.crlf 1\r\nparked.part.1.engine 83 Pluck\n",
              "keep: known lines regenerated (fresh stamp, new depth), unknown lines byte exact in order, parked lines kept");
        m.depth = 0;
        const int n0 = mtffWrite(m, raw, int(strlen(raw)), out, sizeof(out));
        check(n0 > 0 && text(out, n0).find("strum.ramp") == std::string::npos && text(out, n0).find("shadow.sat 128\n") != std::string::npos,
              "keep: depth 0 with kept lines is a file without the strum.ramp line");
        const char* dup = "tffproj 1\nstrum.ramp 10\nstrum.ramp 20\nmt 1 2\nmt 3 4\n";
        m.depth = 0;
        check(mtffWrite(m, dup, int(strlen(dup)), out, sizeof(out)) == 0, "keep: duplicate known keys are dropped (nothing else kept = no file)");
        check(mtffWrite(m, raw, int(strlen(raw)), out, 70) == -1, "cap: a text that does not fit the cap is refused, not cut");
        check(mtffWrite(m, raw, int(strlen(raw)), out, 20) == -1, "cap: a cap below the head is refused");
        std::string large = "tffproj 1\n";
        for (int i = 0; large.size() < size_t(kMtffMax) + 100; i++) large += "x.line" + std::to_string(i) + " 1234567890\n";
        m.depth = 100;
        check(mtffWrite(m, large.c_str(), int(large.size()), out, kMtffMax) == -1, "cap: kept lines past the cap make the write fail, nothing is dropped silently");
    }
    {   // stale: parked
        Host h;
        MemFs fs;
        h.stamp = {0x1111u, 2324u, true};
        fs.files[kWs] = "tffproj 1\nmt 00002222 2324\nstrum.ramp 70\npart.1.engine 83\npart.1.patch Lead\ntrack.3.mute 1\nx.other 5\ninstr.4.name Kick\npat.9.x 1\n";
        const SidecarState st = load(fs, h);
        check(st == SidecarState::kLoaded && h.m.stale && h.m.depth == 70, "stale: the file loads, project-wide strum.ramp applies");
        check(h.m.parked == 3, "stale: the three slot-bound lines (part. instr.) are parked, track. and pat. are not (2026-11-18)");
        check(text(h.raw, h.rawN) == "tffproj 1\nmt 00002222 2324\nstrum.ramp 70\nparked.part.1.engine 83\nparked.part.1.patch Lead\ntrack.3.mute 1\nx.other 5\nparked.instr.4.name Kick\npat.9.x 1\n",
              "stale: the other lines stay as they are (pat.9.x too), the parked ones are renamed in place");
        check(save(fs, h, 70) && fs.files[kWs].find("mt 00001111 2324") != std::string::npos && fs.files[kWs].find("parked.part.1.engine 83\n") != std::string::npos &&
                fs.files[kWs].find("\npart.1.engine") == std::string::npos,
              "stale: the next save writes the fresh stamp and keeps the parked lines (not applied, not lost)");
        // load again: stamp equal now, parked lines are plain unknown lines, not parked twice
        const SidecarState st2 = load(fs, h);
        check(st2 == SidecarState::kLoaded && !h.m.stale && h.m.parked == 0 && fs.files[kWs].find("parked.parked.") == std::string::npos,
              "stale: in sync after that save, nothing parked again");
        // a size mismatch alone is stale too
        fs.files[kWs] = "tffproj 1\nmt 00001111 2000\nstrum.ramp 40\n";
        load(fs, h);
        check(h.m.stale && h.m.depth == 40, "stale: the same CRC with another size is stale, depth still applies");
        // no stamp in the file, or none known: not stale
        fs.files[kWs] = "tffproj 1\nstrum.ramp 40\npart.1.engine 83\n";
        load(fs, h);
        check(!h.m.stale && h.m.parked == 0 && h.m.depth == 40, "stale: a file without an mt line cannot be checked: not stale, nothing parked");
        fs.files[kWs] = "tffproj 1\nmt 00002222 2324\nstrum.ramp 40\npart.1.engine 83\n";
        h.stamp.valid = false;
        load(fs, h);
        check(!h.m.stale && h.m.parked == 0, "stale: the project.mt stamp unknown (no project.mt read): not stale");
    }

    // ---- file rules
    {   // missing file
        Host h;
        MemFs fs;
        h.stamp = {0xaaaau, 2324u, true};
        check(load(fs, h) == SidecarState::kNone && h.m.depth == 0 && h.rawN == 0, "missing file: kNone, every key at its absent value (depth 0)");
        const int mut = fs.mutations;
        check(save(fs, h, 0) && fs.files.empty() && fs.mutations == mut, "missing file: a save at depth 0 writes nothing and touches nothing on the card");
        check(save(fs, h, 100) && h.st == SidecarState::kLoaded && fs.files.size() == 1 &&
                fs.files[kWs] == "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 100\n" && fs.clean({kWs}),
              "missing file: depth 100 writes it, stamp included, no .tmp or .bak left");
        const int mut2 = fs.mutations;
        check(save(fs, h, 100) && fs.mutations == mut2, "unchanged: the same text is not written again");
        h.stamp.crc = 0xbbbbu;
        check(save(fs, h, 100) && fs.mutations > mut2 && fs.files[kWs].find("mt 0000bbbb") != std::string::npos,
              "stamp: a new project.mt (new stamp) rewrites the file even though no setting changed");
        check(save(fs, h, 0) && !fs.exists(kWs) && h.st == SidecarState::kNone && fs.clean({}), "depth back to 0: the file is removed (a stock like project has none)");
        const int mut3 = fs.mutations;
        check(save(fs, h, 0) && fs.mutations == mut3, "depth 0 again: no card access");
    }
    {   // unknown lines survive a rewrite on the card
        Host h;
        MemFs fs;
        h.stamp = {0xaaaau, 2324u, true};
        fs.files[kWs] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 100\nx.keep me exactly\n";
        load(fs, h);
        check(!h.m.stale && h.m.depth == 100, "unknown: loads in sync");
        check(save(fs, h, 20) && fs.files[kWs] == "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 20\nx.keep me exactly\n", "unknown: depth changed, the unknown line is still there byte exact");
        check(save(fs, h, 0) && fs.files[kWs] == "tffproj 1\nmt 0000aaaa 2324\nx.keep me exactly\n", "unknown: depth 0, the file stays (a line is kept), no strum.ramp line");
        Host h2;
        load(fs, h2);
        check(h2.m.depth == 0 && h2.rawN > 0, "unknown: it reads back as depth 0");
    }
    {   // foreign, newer, card error
        Host h;
        MemFs fs;
        h.stamp = {1u, 1u, true};
        fs.files[kWs] = "this is not ours\n";
        check(load(fs, h) == SidecarState::kForeign && h.m.depth == 0, "foreign: a file without the first line reads as empty");
        check(save(fs, h, 0) && fs.files[kWs] == "this is not ours\n", "foreign: nothing to store, the file is kept");
        check(save(fs, h, 50) && fs.files[kWs].rfind("tffproj 1\n", 0) == 0, "foreign: a setting to store replaces it");
        fs.files[kWs] = "tffproj 2\nstrum.ramp 100\nfuture 1\n";
        check(load(fs, h) == SidecarState::kUnreadable && h.m.newer && h.m.depth == 0, "newer: a higher major is blocked and applies nothing");
        check(save(fs, h, 0) && save(fs, h, 30) && fs.files[kWs] == "tffproj 2\nstrum.ramp 100\nfuture 1\n", "newer: never overwritten, never removed (a setting to store is dropped, the save succeeds)");
        MemFs fe;
        fe.files[kWs] = "tffproj 1\nstrum.ramp 100\n";
        fe.unknown.insert(kWs);
        check(load(fe, h) == SidecarState::kUnreadable, "card error: stat answers unknown: kUnreadable");
        fe.reboot();
        check(save(fe, h, 0) && !save(fe, h, 50) && fe.files[kWs] == "tffproj 1\nstrum.ramp 100\n", "card error: blocked, the file the card could not read is not overwritten or removed");
        check(load(fe, h) == SidecarState::kLoaded && h.m.depth == 100, "card error: the next load retries and reads it");
        MemFs fb;
        fb.files[kWs] = std::string("tffproj 1\n") + std::string(size_t(kMtffMax) + 100, 'x') + "\n";
        check(load(fb, h) == SidecarState::kUnreadable, "size: a file over the cap is unreadable (blocked), not truncated");
        MemFs fr;
        fr.files["Workspace/project.mtff.bak"] = "tffproj 1\nstrum.ramp 35\n";
        check(load(fr, h) == SidecarState::kLoaded && h.m.depth == 35 && fr.exists(kWs), "interrupted replace: the .bak is moved back and read");
    }
    {   // Save and Save As carry the file, Open brings it
        Host h;
        MemFs fs;
        char buf[kMtffMax];
        fs.files[kWs] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 100\nx.k 1\n";
        h.stamp = {0xaaaau, 2324u, true};
        load(fs, h);
        check(mtffProjectStep(fs, kWs, kProj, hasContent(h), h.st, buf, sizeof(buf)) && fs.files[kProj] == fs.files[kWs] && fs.clean({kWs, kProj}),
              "Save As: the Workspace file is copied to the new project folder, byte for byte");
        fs.files[kWs] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 10\n";
        check(mtffProjectStep(fs, kWs, kProj, true, SidecarState::kLoaded, buf, sizeof(buf)) && fs.files[kProj] == fs.files[kWs], "Save: a changed Workspace file replaces the project's");
        const std::string kept = fs.files[kProj];
        fs.files.erase(kWs);
        check(mtffProjectStep(fs, kWs, kProj, false, SidecarState::kNone, buf, sizeof(buf)) && !fs.exists(kProj), "Save: no Workspace file (depth 0) removes the project's old one");
        fs.files[kProj] = kept;
        check(mtffProjectStep(fs, kWs, kProj, false, SidecarState::kUnreadable, buf, sizeof(buf)) && fs.files[kProj] == kept,
              "Save: a Workspace file the card could not read never removes the project's");
        check(mtffProjectStep(fs, kWs, kProj, false, SidecarState::kStale, buf, sizeof(buf)) && fs.files[kProj] == kept, "Save: a failed open copy (stale) copies nothing");
        check(!mtffProjectStep(fs, kWs, kProj, true, SidecarState::kStale, buf, sizeof(buf)) && fs.files[kProj] == kept,
              "Save: stale with a setting to store fails, the project's file is untouched");
        // Open
        MemFs fo;
        fo.files[kProj] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 45\n";
        fo.files[kWs] = "tffproj 1\nstrum.ramp 99\n";   // the previous project's
        bool stale = false;
        Host ho;
        ho.stamp = {0xaaaau, 2324u, true};
        check(sidecarOpenStep(fo, kProj, kWs, buf, sizeof(buf), stale) && !stale && fo.files[kWs] == fo.files[kProj], "Open: the project's file replaces the Workspace's");
        load(fo, ho);
        check(ho.m.depth == 45 && !ho.m.stale, "Open: loads with the project's depth, in sync");
        fo.files.erase(kProj);
        check(sidecarOpenStep(fo, kProj, kWs, buf, sizeof(buf), stale) && !fo.exists(kWs), "Open: a project without the file leaves none in the Workspace (the previous project's depth does not leak)");
        load(fo, ho);
        check(ho.m.depth == 0, "Open: so the project plays with the ramp off");
        // a failed open copy
        fo.files[kProj] = "tffproj 1\nstrum.ramp 45\n";
        fo.unknown.insert(kProj);
        check(!sidecarOpenStep(fo, kProj, kWs, buf, sizeof(buf), stale) && stale, "Open: a card error on the project's file is a failed copy (stale)");
        fo.reboot();
        ho.openStale = stale;   // plug_host.cpp s_mtffOpenStale, the flag of the open that just failed
        check(load(fo, ho) == SidecarState::kStale && ho.m.depth == 0 && !ho.openStale, "Open: the load after a failed copy applies nothing (kStale)");
        check(save(fo, ho, 0) && fo.files[kProj] == "tffproj 1\nstrum.ramp 45\n", "Open: and a save does not touch the project's file");
    }
    {   // nested folders and refused paths
        char p1[kPathMax], p2[kPathMax];
        const std::string deep = "Projects/Albums_2026/Summer_Tour_Warmup_Sessions/Rehearsal_Take_One";
        MemFs fs;
        char buf[kMtffMax];
        fs.files[std::string("Workspace/") + kMtffName] = "tffproj 1\nstrum.ramp 100\n";
        check(sidecarPath(p1, kPathMax, "Workspace", kMtffName) && sidecarPath(p2, kPathMax, deep.c_str(), kMtffName), "nested: both paths fit");
        check(mtffProjectStep(fs, p1, p2, true, SidecarState::kLoaded, buf, sizeof(buf)) && fs.files[deep + "/" + kMtffName] == fs.files[p1] && fs.files.size() == 2,
              "nested: a 67 character folder gets <folder>/project.mtff and no other file");
        const std::string f243(243 - 9 + 1, 'd');   // "Projects/" + this = 243 characters... checked below
        std::string folder = "Projects/" + f243.substr(0, 243 - 9);
        check(folder.size() == 243, "nested: the long test folder has 243 characters");
        fs.files.clear();
        fs.files[p1] = "tffproj 1\nstrum.ramp 100\n";
        const bool fits = sidecarPath(p2, kPathMax, folder.c_str(), kMtffName);
        check(!fits || strlen(p2) == 243 + 1 + strlen(kMtffName), "nested: a path is whole or refused");
        // project.mtff is 12 characters: 243 + 1 + 12 = 256 does not fit, 242 does
        check(!sidecarPath(p2, kPathMax, folder.c_str(), kMtffName) && p2[0] == 0, "overflow: a 256 character file path is refused, out is empty");
        const std::string f242 = folder.substr(0, 242);
        check(sidecarPath(p2, kPathMax, f242.c_str(), kMtffName) && strlen(p2) == 255, "overflow: a 255 character file path is the longest that fits");
        const int mut = fs.mutations;
        const std::string tooLong = folder + "/" + kMtffName;
        check(!mtffProjectStep(fs, p1, tooLong.c_str(), true, SidecarState::kLoaded, buf, sizeof(buf)) && fs.mutations == mut && fs.files.size() == 1,
              "overflow: a copy to a 256 character path fails and touches nothing");
        check(mtffProjectStep(fs, p1, p2, true, SidecarState::kLoaded, buf, sizeof(buf)) && fs.files.count(p2) == 1 && fs.clean({p1, p2}),
              "overflow: the 255 character path works, .tmp and .bak fit");
    }
    {   // power loss at every step of a save over an existing file: the old or the new file, never a torn one
        MemFs probe;
        Host hp;
        hp.stamp = {0xaaaau, 2324u, true};
        probe.files[kWs] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 100\n";
        load(probe, hp);
        probe.mutations = 0;
        save(probe, hp, 20);
        const int steps = probe.mutations;
        bool ok = steps > 0;
        int oldNew = 0, newNew = 0;
        for (int cut = 1; cut <= steps; cut++)
        {
            MemFs fs;
            Host h;
            h.stamp = {0xaaaau, 2324u, true};
            fs.files[kWs] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 100\n";
            load(fs, h);
            fs.cut = cut;
            save(fs, h, 20);
            fs.reboot();
            Host h2;
            h2.stamp = {0xaaaau, 2324u, true};
            const SidecarState st = load(fs, h2);
            const bool isOld = h2.m.depth == 100, isNew = h2.m.depth == 20;
            oldNew += isOld;
            newNew += isNew;
            ok = ok && st == SidecarState::kLoaded && (isOld || isNew);
        }
        printf("      %d cut points, old %d, new %d\n", steps, oldNew, newNew);
        check(ok, "power loss: at every step of a save the next load finds the old file or the new one, never a torn or missing one");
    }

    // ---- features-140: the keys of the 15 feature branches (seed, duck, groove, snap, follow chord, Parts) in this one file
    {
        Host h;
        Mtff& m = h.m;
        m.hasStamp = true;
        m.crc = 0x3a51c2e0u;
        m.size = 2324;
        m.depth = 100;
        m.seed = 42;
        m.duck = {3, 60, 50, 50};
        m.groove[1] = 17;
        m.groove[8] = -25;
        m.snap[2] = 1;
        m.snap[15] = 3;
        m.followMode[1] = 1;
        m.followSource[1] = 0;
        m.followMode[15] = 3;
        m.followSource[15] = 14;
        h.pl.count = 2;
        h.pl.mode = 1;
        parts::partInit(h.pl.part[0], 3);
        h.pl.part[0].src[3] = 0;
        h.pl.part[0].length = 16;
        parts::partInit(h.pl.part[1], 9);
        h.pl.part[1].repeats = 0;
        h.pl.part[1].baked = 77;
        const int n = mtffWrite(m, "", 0, out, sizeof(out));
        const std::string want =
          "tffproj 1\nmt 3a51c2e0 2324\nstrum.ramp 100\nseed.take 42\nduck.send 3 60 50 50\n"
          "track.2.groove 17\ntrack.2.follow 1 1\ntrack.3.snap 1\ntrack.9.groove -25\ntrack.16.snap 3\ntrack.16.follow 15 3\n"
          "pat.parts.mode 1\npat.part.1 03030300030303030303030303030303 16 1 1 0\npat.part.2 09090909090909090909090909090909 0 0 1 77\n";
        check(text(out, n) == want, "keys: every setting written, in key order, track keys by track, nothing else");
        Host r;
        check(mtffRead(out, n, r.m) && r.m.depth == 100 && r.m.seed == 42 && r.m.duck.source == 3 && r.m.duck.depth == 60 && r.m.duck.release == 50 &&
                r.m.duck.thresh == 50 && r.m.groove[1] == 17 && r.m.groove[8] == -25 && r.m.snap[2] == 1 && r.m.snap[15] == 3 &&
                r.m.followMode[1] == 1 && r.m.followSource[1] == 0 && r.m.followMode[15] == 3 && r.m.followSource[15] == 14 && r.pl.count == 2 &&
                r.pl.mode == 1 && memcmp(r.pl.part, h.pl.part, 2 * sizeof(parts::Part)) == 0 && r.m.groove[0] == 0 && r.m.followMode[0] == 0,
              "keys: every setting reads back, the Parts list in place");
        // Loop Song is bit 1 of the flags of Part 0: written in the same field (no new key), back after a read, and a project written
        // without it is the same text as before the flag existed
        h.pl.part[0].flags |= parts::kFlagLoop;
        char outl[2048];
        const int nl = mtffWrite(m, "", 0, outl, sizeof(outl));
        check(text(outl, nl).find("pat.part.1 03030300030303030303030303030303 16 1 3 0\n") != std::string::npos &&
                text(outl, nl).find("pat.part.2 09090909090909090909090909090909 0 0 1 77\n") != std::string::npos,
              "keys: Loop Song is flags 3 on Part 1 only, no other line changes");
        Host rl;
        check(mtffRead(outl, nl, rl.m) && parts::songLoops(rl.pl) && !(rl.pl.part[1].flags & parts::kFlagLoop),
              "keys: Loop Song reads back on Part 0");
        h.pl.part[0].flags &= uint8_t(~parts::kFlagLoop);
        check(parts::songLoops(r.pl) == false, "keys: a list written without the flag does not loop (a missing flag stops the song at its end)");
        // a PARTS=0 build has no list: the pat. lines are somebody else's and stay
        Mtff np = m;
        np.parts = nullptr;
        const int n2 = mtffWrite(np, out, n, out + 8192, 8192);
        check(n2 > 0 && text(out + 8192, n2).find("pat.part.1 03030300") != std::string::npos && text(out + 8192, n2).find("pat.parts.mode 1\n") != std::string::npos,
              "keys: a build without the Parts list keeps the pat.part. lines byte exact");
        Mtff np2{};
        check(mtffRead(out, n, np2) && np2.depth == 100 && np2.snap[15] == 3, "keys: a file with pat. lines reads in a build without a list (lines ignored)");
        // nothing set: no file, each key alone makes one
        Mtff z{};
        z.parts = &h.pl;
        h.pl.count = 0;
        check(mtffWrite(z, "", 0, out, sizeof(out)) == 0, "keys: nothing set is no file (an empty Parts list too)");
        const char* only[] = {"seed.take 9\n", "duck.send 1 60 50 50\n", "track.4.groove 5\n", "track.1.snap 2\n", "track.7.follow 1 2\n"};
        bool ok = true;
        for (int i = 0; i < 5; i++)
        {
            Mtff a{};
            a.parts = &h.pl;
            if (i == 0) a.seed = 9;
            if (i == 1) a.duck = {1, 60, 50, 50};
            if (i == 2) a.groove[3] = 5;
            if (i == 3) a.snap[0] = 2;
            if (i == 4) { a.followMode[6] = 2; a.followSource[6] = 0; }
            const int k = mtffWrite(a, "", 0, out, sizeof(out));
            ok = ok && k > 0 && text(out, k) == std::string("tffproj 1\n") + only[i];
        }
        check(ok, "keys: seed, duck, groove, snap and follow each alone make the file, as the single key");
        // values that are not valid: skipped or clamped, the rest of the file still reads
        const char* odd =
          "tffproj 1\nseed.take 0\nseed.take 256\nseed.take 12x\nseed.take\nduck.send 3 60 50\nduck.send 0 60 50 50\nduck.send 14 60 50 50\n"
          "duck.send 3 101 50 50\ntrack.0.groove 5\ntrack.17.groove 5\ntrack.3.groove 99\ntrack.4.groove -99\ntrack.5.groove x\ntrack.6.snap 4\n"
          "track.7.snap -1\ntrack.8.follow 8 1\ntrack.8.follow 17 1\ntrack.8.follow 1 0\ntrack.8.follow 1 4\ntrack.9.follow 1\ntrack.10.groove 3 4\n";
        Host o;
        check(mtffRead(odd, int(strlen(odd)), o.m) && o.m.seed == 0 && o.m.duck.source == 0 && o.m.groove[2] == 50 && o.m.groove[3] == -50 && o.m.groove[4] == 0 &&
                o.m.snap[5] == 0 && o.m.snap[6] == 0 && o.m.followMode[7] == 0 && o.m.followMode[8] == 0 && o.m.groove[9] == 0 && o.m.groove[0] == 0,
              "keys: seed 0 256 12x, a duck of three numbers, source 0 or 14, a depth over 100, tracks 0 and 17, snap past Up, a follow of itself, track 17, mode 0 or 4, "
              "a follow without a mode and a groove with two numbers: skipped, a groove past 50 clamped");
        const char* later = "tffproj 1\nseed.take 7\nseed.take 9\ntrack.2.snap 1\ntrack.2.snap 3\n";
        check(mtffRead(later, int(strlen(later)), o.m) && o.m.seed == 9 && o.m.snap[1] == 3, "keys: with two lines of one key the last counts");
        const char* crlf = "tffproj 1\r\nseed.take 17\r\ntrack.1.groove -4\r\npat.part.1 01010101010101010101010101010101 8 2 0 0\r\n";
        check(mtffRead(crlf, int(strlen(crlf)), o.m) && o.m.seed == 17 && o.m.groove[0] == -4 && o.pl.count == 1 && o.pl.part[0].length == 8 && o.pl.part[0].repeats == 2,
              "keys: a CRLF file reads");
        const char* badp = "tffproj 1\npat.part.1 0101\npat.part.2 0g010101010101010101010101010101 8 2 0 0\npat.part.3 01010101010101010101010101010101 200 99 0 0\n"
                           "pat.part.0 01010101010101010101010101010101 8 2 0 0\npat.part.256 01010101010101010101010101010101 8 2 0 0\npat.parts.mode 2\n";
        check(mtffRead(badp, int(strlen(badp)), o.m) && o.pl.count == 3 && o.pl.part[2].length == parts::kMaxLength && o.pl.part[2].repeats == parts::kMaxRepeats &&
                o.pl.part[0].src[0] == 0 && o.pl.part[1].src[0] == 0 && o.pl.mode == 0,
              "keys: Part lines with a short or non hex source, number 0 or 256 are skipped, a length over 128 and passes over 64 clamped, mode 2 is 0");
        // fx.slots (FX3 and FX4, core/source/fx34.h): only 4 is on, written only when on, a file of it alone is a file, the unknown lines stay
        {
            Host f;
            bool onOk = true;
            const char* ons[] = {"tffproj 1\nfx.slots 4\n", "tffproj 1\r\nfx.slots 4\r\n", "tffproj 1\nfx.slots   4\n", "tffproj 1\nfx.slots 04\n"};
            for (const char* t : ons) onOk = onOk && mtffRead(t, int(strlen(t)), f.m) && f.m.fxSlots == 4;
            check(onOk, "keys: fx.slots 4 is on (also CRLF, spaces and a leading zero)");
            bool offOk = true;
            const char* offs[] = {"tffproj 1\nfx.slots 0\n", "tffproj 1\nfx.slots 3\n", "tffproj 1\nfx.slots 5\n", "tffproj 1\nfx.slots on\n", "tffproj 1\nfx.slots 4 7\n",
                                  "tffproj 1\nfx.slots\n", "tffproj 1\nfx.slots 4x\n", "tffproj 1\nfx.slots 4\nfx.slots 2\n", "tffproj 1\nfx.slotsx 4\n", "tffproj 1\nseed.take 3\n"};
            for (const char* t : offs) offOk = offOk && mtffRead(t, int(strlen(t)), f.m) && f.m.fxSlots == 0;
            check(offOk, "keys: fx.slots 0 3 5 on, two values, none, 4x, a later 2 and the key of another name leave it off");
            const char* last = "tffproj 1\nfx.slots 2\nfx.slots 4\n";
            check(mtffRead(last, int(strlen(last)), f.m) && f.m.fxSlots == 4, "keys: with two fx.slots lines the last counts");
            Mtff a{};
            a.parts = &h.pl;
            check(!mtffAny(a), "keys: no fx.slots, no file");
            a.fxSlots = 4;
            const int k = mtffWrite(a, "", 0, out, sizeof(out));
            check(mtffAny(a) && text(out, k) == "tffproj 1\nfx.slots 4\n", "keys: fx.slots 4 alone is a file of its own and is written as one line");
            a.depth = 100;
            a.seed = 3;
            const char* raw = "tffproj 1\nfx.slots 4\nx.future 1\n";
            const int k2 = mtffWrite(a, raw, int(strlen(raw)), out, sizeof(out));
            check(text(out, k2) == "tffproj 1\nstrum.ramp 100\nseed.take 3\nfx.slots 4\nx.future 1\n", "keys: fx.slots is regenerated in file order, the unknown line is kept");
            a.fxSlots = 0;
            const int k3 = mtffWrite(a, raw, int(strlen(raw)), out, sizeof(out));
            check(text(out, k3) == "tffproj 1\nstrum.ramp 100\nseed.take 3\nx.future 1\n", "keys: switched off the line is dropped, a file without the key is the file of 1.4.0 before it");
            // a stale file still applies it (a project-wide key)
            Host st;
            st.raw[0] = 0;
            const char* stale = "tffproj 1\nmt 12345678 2324\nfx.slots 4\npat.parts.mode 1\n";
            memcpy(st.raw, stale, strlen(stale));
            st.rawN = int(strlen(stale));
            check(mtffRead(st.raw, st.rawN, st.m, false) && st.m.fxSlots == 4, "keys: the key applies on a stale file (slot bound lines are parked, this one is not)");
        }
        // macro.mode (the Macro depth row of the Project Settings page): 0 Add, 1 Scale, read into 1 and 2 (0 is no key), written only when set
        {
            Host f;
            Mtff r{};
            bool ok = true;
            const char* add0 = "tffproj 1\nmacro.mode 0\n";
            const char* sc1 = "tffproj 1\r\nmacro.mode   1\r\n";
            ok = ok && mtffRead(add0, int(strlen(add0)), r) && r.macroMode == 1;
            ok = ok && mtffRead(sc1, int(strlen(sc1)), r) && r.macroMode == 2;
            check(ok, "keys: macro.mode 0 reads as Add (1) and macro.mode 1 as Scale (2), also CRLF and spaces");
            bool offOk = true;
            const char* offs[] = {"tffproj 1\nmacro.mode 2\n", "tffproj 1\nmacro.mode 7\n", "tffproj 1\nmacro.mode x\n", "tffproj 1\nmacro.mode\n", "tffproj 1\nmacro.mode 1 0\n",
                                  "tffproj 1\nmacro.mode -1\n", "tffproj 1\nmacro.modex 1\n", "tffproj 1\nseed.take 3\n"};
            for (const char* t : offs) offOk = offOk && mtffRead(t, int(strlen(t)), r) && r.macroMode == 0;
            check(offOk, "keys: macro.mode 2 7 x, none, two values, -1 and a key of another name leave it absent");
            const char* last = "tffproj 1\nmacro.mode 0\nmacro.mode 1\n";
            check(mtffRead(last, int(strlen(last)), r) && r.macroMode == 2, "keys: with two macro.mode lines the last counts");
            Mtff a{};
            a.parts = &h.pl;
            check(!mtffAny(a), "keys: no macro.mode, no file");
            a.macroMode = 1;
            int k = mtffWrite(a, "", 0, out, sizeof(out));
            check(mtffAny(a) && text(out, k) == "tffproj 1\nmacro.mode 0\n", "keys: Add alone is a file of its own, written as macro.mode 0");
            a.macroMode = 2;
            k = mtffWrite(a, "", 0, out, sizeof(out));
            check(text(out, k) == "tffproj 1\nmacro.mode 1\n", "keys: Scale is written as macro.mode 1");
            a.depth = 100;
            const char* raw = "tffproj 1\nmacro.mode 0\nx.future 1\n";
            k = mtffWrite(a, raw, int(strlen(raw)), out, sizeof(out));
            check(text(out, k) == "tffproj 1\nstrum.ramp 100\nmacro.mode 1\nx.future 1\n", "keys: macro.mode is regenerated in file order, the unknown line is kept");
            a.macroMode = 0;
            k = mtffWrite(a, raw, int(strlen(raw)), out, sizeof(out));
            check(text(out, k) == "tffproj 1\nstrum.ramp 100\nx.future 1\n", "keys: Per patch drops the line, a project that never set it has the file it had before");
            Host st;
            const char* stale = "tffproj 1\nmt 12345678 2324\nmacro.mode 1\npat.parts.mode 1\n";
            memcpy(st.raw, stale, strlen(stale));
            st.rawN = int(strlen(stale));
            check(mtffRead(st.raw, st.rawN, st.m, false) && st.m.macroMode == 2, "keys: the key applies on a stale file (a project-wide key)");
        }
        // unknown suffixes and parked lines are not ours: kept
        const char* mine = "tffproj 1\ntrack.3.groove2 5\ntrack.3.mute 1\ntrack.99.groove 4\nparked.track.3.groove 9\nseed.take 5\n";
        Mtff k{};
        k.parts = &h.pl;
        k.seed = 6;
        const int nk = mtffWrite(k, mine, int(strlen(mine)), out, sizeof(out));
        check(text(out, nk) == "tffproj 1\nseed.take 6\ntrack.3.groove2 5\ntrack.3.mute 1\ntrack.99.groove 4\nparked.track.3.groove 9\n",
              "keys: track.3.groove2, track.3.mute, track.99.groove and parked. lines are kept as they are, seed.take is regenerated");
    }
    {   // pat.start.N: the start step of each track of a Part, written only for a Part that has one
        Host h;
        h.pl.count = 3;
        h.pl.mode = 1;
        for (int i = 0; i < 3; i++) parts::partInit(h.pl.part[i], uint8_t(3 + i));
        h.pl.start[1][2] = 16;
        h.pl.start[1][5] = 127;
        h.pl.start[1][15] = 1;
        char o[2048], o2[2048];
        const int n = mtffWrite(h.m, "", 0, o, sizeof(o));
        const std::string t = text(o, n);
        check(t.find("pat.start.2 00001000000000000000000000000000 16 1 1 0\n") == std::string::npos &&
                t.find("pat.start.2 00001000007f00000000000000000001\n") != std::string::npos,
              "start: Part 2 has a pat.start line, 16 bytes of hex, track 3 at 0x10, track 6 at 0x7f, track 16 at 1");
        check(t.find("pat.start.1") == std::string::npos && t.find("pat.start.3") == std::string::npos && t.find("pat.start.2") > t.find("pat.part.3 "),
              "start: no line for a Part without a start, the lines follow the pat.part. lines");
        Host r;
        check(mtffRead(o, n, r.m) && r.pl.count == 3 && memcmp(r.pl.start, h.pl.start, sizeof(h.pl.start)) == 0 && memcmp(r.pl.part, h.pl.part, 3 * sizeof(parts::Part)) == 0,
              "start: every start reads back, the Parts too");
        // 1.4.0 file (no start key at all): every start is 0, and writing it back adds no line
        h.pl.start[1][2] = h.pl.start[1][5] = h.pl.start[1][15] = 0;
        const int n0 = mtffWrite(h.m, "", 0, o2, sizeof(o2));
        check(text(o2, n0).find("pat.start") == std::string::npos, "start: a list with every start 0 writes the file it wrote before the key existed");
        Host r0;
        r0.pl.start[0][0] = 9;
        check(mtffRead(o2, n0, r0.m) && r0.pl.count == 3 && r0.pl.start[0][0] == 0 && r0.pl.start[1][2] == 0, "start: a file without the key reads with every start 0 (and clears the rows of the list before)");
        // bad lines: too short, not hex, too long, a value past 127, index 0 or 256, a Part past the count
        const char* bad = "tffproj 1\npat.part.1 01010101010101010101010101010101 8 2 0 0\npat.start.1 0101\npat.start.1 0g010101010101010101010101010101\n"
                          "pat.start.1 000000000000000000000000000000000\npat.start.0 01010101010101010101010101010101\npat.start.256 01010101010101010101010101010101\n"
                          "pat.start.1 ff800000000000000000000000000001\n";
        Host rb;
        check(mtffRead(bad, int(strlen(bad)), rb.m) && rb.pl.count == 1 && rb.pl.start[0][0] == 127 && rb.pl.start[0][1] == 127 && rb.pl.start[0][15] == 1 &&
                rb.pl.start[0][2] == 0,
              "start: a short, a non hex and a too long line are skipped, 0 and 256 are no Part, a value past 127 is 127");
        // a build without the list keeps the lines, a stale file applies them (2026-11-18)
        Mtff np = h.m;
        np.parts = nullptr;
        h.pl.start[1][2] = 16;
        const int n1 = mtffWrite(h.m, "", 0, o, sizeof(o));
        const int n2 = mtffWrite(np, o, n1, o2, sizeof(o2));
        check(n2 > 0 && text(o2, n2).find("pat.start.2 00001000007f00") == std::string::npos && text(o2, n2).find("pat.start.2 00001000000000000000000000000000\n") != std::string::npos,
              "start: a build without the Parts list keeps the pat.start line byte exact");
        MemFs fs;
        fs.files[kWs] = text(o, n1);
        Host st;
        st.stamp = {0x1111u, 2324u, true};
        fs.files[kWs] = "tffproj 1\nmt 12345678 2324\n" + text(o, n1).substr(text(o, n1).find("pat."));
        check(load(fs, st) == SidecarState::kLoaded && st.m.stale && st.pl.count == 3 && st.m.parked == 0 && st.pl.start[1][2] == 16 &&
                  text(st.raw, st.rawN).find("parked.") == std::string::npos,
              "start: a stale file applies the pat.start line with the Parts, nothing parked");
    }
    {   // the largest file this build writes fits the cap, and a stale one applies every Part
        Host h;
        h.m.hasStamp = true;
        h.m.crc = 0xdeadbeefu;
        h.m.size = 2324;
        h.m.depth = 100;
        h.m.seed = 255;
        h.m.duck = {13, 100, 255, 100};
        h.pl.count = parts::kMax;
        h.pl.mode = 1;
        for (int i = 0; i < parts::kMax; i++)
        {
            parts::partInit(h.pl.part[i], uint8_t(255 - i % 200));
            h.pl.part[i].src[15] = 255;
            h.pl.part[i].length = 128;
            h.pl.part[i].repeats = 64;
            h.pl.part[i].baked = 255;
            h.pl.part[i].flags = 255;
        }
        memset(h.pl.start, parts::kMaxStart, sizeof(h.pl.start));   // a start on every track of every Part: a pat.start line of 46 bytes each
        for (int t = 0; t < kMtffTracks; t++)
        {
            h.m.groove[t] = int8_t(t % 2 ? -50 : 50);
            h.m.snap[t] = 3;
            h.m.followMode[t] = 3;
            h.m.followSource[t] = uint8_t((t + 1) % 16);
        }
        char* big = new char[kMtffMax];
        const int n = mtffWrite(h.m, "", 0, big, kMtffMax);
        printf("      largest file %d bytes of %d\n", n, kMtffMax);
        check(n > 0 && n + 7 * (2 * parts::kMax + 1) <= kMtffMax, "cap: 255 Parts at their longest with a start on every track, 16 followers, grooves, snaps and the rest fit with room for the parking (7 bytes a line)");
        Host r;
        check(mtffRead(big, n, r.m) && r.pl.count == parts::kMax && memcmp(r.pl.part, h.pl.part, sizeof(h.pl.part)) == 0 && memcmp(r.pl.start, h.pl.start, sizeof(h.pl.start)) == 0 && r.m.groove[15] == -50 && r.m.followSource[15] == 0,
              "cap: that file reads back whole");
        // as a stale file (a 1.3.x save): the Parts apply whole, the track. keys too, nothing parked (user decision 2026-11-18)
        MemFs fs;
        fs.files[kWs] = std::string(big, size_t(n));
        Host s;
        s.stamp = {0x1111u, 2324u, true};
        const SidecarState st = load(fs, s);
        check(st == SidecarState::kLoaded && s.m.stale && s.m.depth == 100 && s.m.seed == 255 && s.m.duck.source == 13, "stale: seed, duck and the strum depth are project-wide and apply");
        check(s.m.parked == 0 && s.pl.count == parts::kMax && s.pl.mode == 1 && memcmp(s.pl.part, h.pl.part, sizeof(h.pl.part)) == 0 &&
                  memcmp(s.pl.start, h.pl.start, sizeof(h.pl.start)) == 0,
              "stale: the Parts mode, every Part and every start apply, nothing parked");
        check(s.m.groove[0] == 50 && s.m.groove[1] == -50 && s.m.snap[3] == 3 && s.m.followMode[2] == 3 && s.m.followSource[15] == 0,
              "stale: the groove, snap and follow keys of the tracks still apply (track numbers never move)");
        check(save(fs, s, 100) && fs.files[kWs].find("\npat.part.255 ") != std::string::npos && fs.files[kWs].find("\npat.start.255 ") != std::string::npos &&
                  fs.files[kWs].find("\npat.parts.mode 1\n") != std::string::npos && fs.files[kWs].find("\ntrack.16.follow 1 3") != std::string::npos &&
                  fs.files[kWs].find("parked.") == std::string::npos && fs.files[kWs].find("mt 00001111 2324") != std::string::npos,
              "stale: the next save writes the fresh stamp and every Part, start and track key live, no parked line");
        {   // a Part names a pattern that another firmware deleted: only those cells are blanked, each pattern asked once
            parts::List& l = s.pl;
            l.part[0].src[0] = 7;
            l.part[0].src[1] = 9;
            l.part[1].src[3] = 7;
            l.part[2].src[5] = 0;
            int asked7 = 0;
            std::string log;
            const parts::Part p2 = l.part[2];
            const int b = mtffBlankMissing(
                l, [&](uint8_t pat) { asked7 += pat == 7; return pat != 7; },
                [&](int part, int track, uint8_t pat) { log += std::to_string(part) + ":" + std::to_string(track) + ":" + std::to_string(pat) + " "; });
            check(b == 2 && asked7 == 1 && l.part[0].src[0] == 0 && l.part[1].src[3] == 0 && l.part[0].src[1] == 9 && log.find("0:0:7 ") == 0 &&
                      log.find("1:3:7 ") != std::string::npos,
                  "blank: the cells naming the deleted pattern 7 are 0, pattern 9 stays, pattern 7 asked once, each blank logged");
            check(memcmp(&l.part[2], &p2, sizeof(p2)) == 0 && l.part[0].length == 128 && l.part[0].repeats == 64 && l.part[0].baked == 255, "blank: nothing else of a Part changes");
            parts::List e = {};
            e.count = 1;
            e.part[0].src[0] = 7;
            check(mtffBlankMissing(e, [](uint8_t) { return true; }, [](int, int, uint8_t) {}) == 0 && e.part[0].src[0] == 7, "blank: a pattern that is there (or a card error) blanks nothing");
            e.count = 0;
            check(mtffBlankMissing(e, [](uint8_t) { return false; }, [](int, int, uint8_t) {}) == 0 && e.part[0].src[0] == 7, "blank: cells past the count are not looked at");
        }
        delete[] big;
    }
    {   // the file rules with the new keys: a groove only project, Save As, a card error, removal
        Host h;
        MemFs fs;
        h.stamp = {0xaaaau, 2324u, true};
        check(load(fs, h) == SidecarState::kNone, "rules: no file, none loaded");
        h.m.groove[4] = -12;
        check(save(fs, h, 0) && fs.files.count(kWs) && fs.files[kWs] == "tffproj 1\nmt 0000aaaa 2324\ntrack.5.groove -12\n", "rules: a groove alone makes the file, with the stamp");
        check(mtffProjectStep(fs, kWs, kProj, true, h.st, h.tmp, kMtffMax) && fs.files[kProj] == fs.files[kWs], "rules: Save carries it to the project folder");
        Host h2;
        h2.stamp = {0xaaaau, 2324u, true};
        MemFs fs2;
        fs2.files[kWs] = fs.files[kProj];
        check(load(fs2, h2) == SidecarState::kLoaded && !h2.m.stale && h2.m.groove[4] == -12, "rules: Open brings it back, in sync, the groove applies");
        h2.m.groove[4] = 0;
        check(save(fs2, h2, 0) && !fs2.exists(kWs), "rules: the groove back to 0 removes the file (nothing else set, nothing kept)");
        h2.pl.count = 1;
        parts::partInit(h2.pl.part[0], 1);
        check(save(fs2, h2, 0) && fs2.exists(kWs), "rules: a Part makes the file");
        MemFs fe;
        fe.files[kWs] = "tffproj 1\ntrack.2.groove 9\n";
        fe.unknown.insert(kWs);
        Host h3;
        check(load(fe, h3) == SidecarState::kUnreadable && h3.m.groove[1] == 0, "rules: a card error applies nothing");
        fe.reboot();
        h3.m.seed = 3;
        check(!save(fe, h3, 0) && fe.files[kWs] == "tffproj 1\ntrack.2.groove 9\n", "rules: and the file the card could not read is not written over");
    }

    // ---- header: the format version and the Tracker FF version of the writer (ref/mtff-version.md)
    auto ver = [](int a, int b, int c, const char* build) {
        MtffId id{};
        id.major = uint8_t(a);
        id.minor = uint8_t(b);
        id.patch = uint8_t(c);
        id.valid = true;
        snprintf(id.build, sizeof(id.build), "%s", build);
        return id;
    };
    constexpr unsigned long kV140 = (1ul << 16) | (4ul << 8) | 0ul, kV150 = (1ul << 16) | (5ul << 8) | 0ul;
    {   // write
        Mtff m{};
        m.self = ver(1, 4, 0, "5a1b2c3");
        m.depth = 100;
        int n = mtffWrite(m, "", 0, out, sizeof(out));
        check(text(out, n) == "tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768\nstrum.ramp 100\n", "header: format, ff version, build sha and cap, first line");
        m.hasStamp = true;
        m.crc = 0x3a51c2e0u;
        m.size = 2324;
        n = mtffWrite(m, "", 0, out, sizeof(out));
        check(text(out, n).rfind("tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768\nmt 3a51c2e0 2324\n", 0) == 0, "header: the stamp is the second line");
        Mtff nb{};
        nb.self = ver(1, 4, 12, "");
        nb.depth = 5;
        n = mtffWrite(nb, "", 0, out, sizeof(out));
        check(text(out, n) == "tffproj 1 ff=1.4.12 cap=32768\nstrum.ramp 5\n", "header: no sha, no build token");
        Mtff none{};
        none.depth = 5;
        n = mtffWrite(none, "", 0, out, sizeof(out));
        check(text(out, n) == "tffproj 1\nstrum.ramp 5\n", "header: a build that did not say its version writes the bare format line");
        Mtff empty{};
        empty.self = ver(1, 4, 0, "5a1b2c3");
        check(mtffWrite(empty, "", 0, out, sizeof(out)) == 0, "header: never written on its own, nothing set and nothing kept is no file");
        empty.hasStamp = true;
        check(mtffWrite(empty, "", 0, out, sizeof(out)) == 0, "header: a stamp alone is no file either");
        // the header is at most 64 bytes with the longest sha
        Mtff longest{};
        longest.self = ver(255, 255, 255, "abcdefgh12345");
        longest.depth = 1;
        n = mtffWrite(longest, "", 0, out, sizeof(out));
        check(text(out, n) == "tffproj 1 ff=255.255.255 build=abcdefgh cap=32768\nstrum.ramp 1\n", "header: the sha is cut at 8 characters");
    }
    {   // read
        Mtff r{};
        const char* full = "tffproj 1 ff=1.4.0 build=5a1b2c3 cap=20480\nstrum.ramp 40\n";
        check(mtffRead(full, int(strlen(full)), r) && r.format == 1 && r.writer.valid && mtffWriterVersion(r) == kV140 &&
                  strcmp(r.writer.build, "5a1b2c3") == 0 && r.writerCap == 20480 && mtffFormatVersion(r) == 1 && r.depth == 40 && !r.newer,
              "header: read gives format 1, writer 1.4.0, build 5a1b2c3, cap 20480");
        const char* bare = "tffproj 1\nstrum.ramp 40\n";
        check(mtffRead(bare, int(strlen(bare)), r) && r.format == 1 && !r.writer.valid && mtffWriterVersion(r) == 0 && r.writer.build[0] == 0 &&
                  r.writerCap == 0 && r.depth == 40 && !r.newer,
              "missing header fields (a dev build): format 1, writer unknown (0), keys apply");
        const char* crlf = "tffproj 1 ff=1.4.0 build=abc cap=20480\r\nstrum.ramp 41\r\n";
        check(mtffRead(crlf, int(strlen(crlf)), r) && mtffWriterVersion(r) == kV140 && strcmp(r.writer.build, "abc") == 0 && r.writerCap == 20480 && r.depth == 41,
              "header: CRLF first line");
        const char* odd = "tffproj 1 future=7 ff=1.5.0 cap=40000 more\nstrum.ramp 42\n";
        check(mtffRead(odd, int(strlen(odd)), r) && mtffWriterVersion(r) == kV150 && r.writerCap == 40000 && r.format == 1 && r.depth == 42,
              "header: a token this build does not know is skipped, the ones it knows are read");
        const char* bad = "tffproj 1 ff=1.4 build=zz cap=x\nstrum.ramp 43\n";
        check(mtffRead(bad, int(strlen(bad)), r) && !r.writer.valid && mtffWriterVersion(r) == 0 && r.writer.build[0] == 0 && r.writerCap == 0 && r.depth == 43,
              "header: a version that does not parse (ff=1.4) leaves the writer unknown, the file still reads (its sha is dropped with it)");
        const char* big = "tffproj 1 ff=1.300.0\nstrum.ramp 44\n";
        check(mtffRead(big, int(strlen(big)), r) && !r.writer.valid && r.depth == 44, "header: a version part over 255 is not a version");
        const char* trail = "tffproj 1x ff=1.4.0\nstrum.ramp 45\n";
        check(!mtffRead(trail, int(strlen(trail)), r), "header: 1x is not a format number: not a project.mtff");
        const char* sha = "tffproj 1 ff=1.4.0 build=ab-cdef\n";
        check(mtffRead(sha, int(strlen(sha)), r) && strcmp(r.writer.build, "ab") == 0, "header: a build token stops at the first character that is no letter or digit");
        // a higher format: the header is still read, so the status line can name the writer
        const char* newer = "tffproj 2 ff=1.5.0 build=beef cap=32768\nstrum.ramp 100\nfuture 1\n";
        check(mtffRead(newer, int(strlen(newer)), r) && r.newer && r.format == 2 && mtffWriterVersion(r) == kV150 && r.depth == 0,
              "format 2: read as newer with its writer named, nothing applied");
        const char* newerBare = "tffproj 9\nstrum.ramp 100\n";
        check(mtffRead(newerBare, int(strlen(newerBare)), r) && r.newer && r.format == 9 && mtffWriterVersion(r) == 0 && r.depth == 0,
              "format 9 with no ff token: newer, writer unknown");
        // self survives a read and a clear
        Mtff keep{};
        keep.self = ver(1, 4, 0, "5a1b2c3");
        mtffRead(full, int(strlen(full)), keep);
        check(keep.self.valid && mtffSelfVersion(keep) == kV140 && strcmp(keep.self.build, "5a1b2c3") == 0, "header: m.self is kept by a read");
        mtffClear(keep);
        check(keep.self.valid && keep.format == 0 && !keep.writer.valid, "header: m.self is kept by a clear, the file's header is not");
    }
    {   // round trip and the keys around the header
        Mtff w{};
        w.self = ver(1, 4, 0, "5a1b2c3");
        w.depth = 70;
        w.seed = 9;
        w.groove[2] = -7;
        const int n = mtffWrite(w, "", 0, out, sizeof(out));
        Mtff r{};
        check(mtffRead(out, n, r) && r.depth == 70 && r.seed == 9 && r.groove[2] == -7 && mtffWriterVersion(r) == kV140 && r.format == 1, "header: round trip with keys");
        // a rewrite by a newer build over the file of this one: header replaced, not repeated
        Mtff w2{};
        w2.self = ver(1, 5, 0, "cafe123");
        w2.depth = 70;
        w2.seed = 9;
        w2.groove[2] = -7;
        const std::string first(out, size_t(n));
        const int n2 = mtffWrite(w2, first.c_str(), int(first.size()), out, sizeof(out));
        const std::string second(out, size_t(n2));
        check(second.rfind("tffproj 1 ff=1.5.0 build=cafe123 cap=32768\n", 0) == 0 && second.find("tffproj", 1) == std::string::npos,
              "header: a rewrite carries the version of the build that saves, one header line");
    }
    {   // the file rules: load, hook, save
        MemFs fs;
        Host h;
        h.stamp = {0xaaaau, 2324u, true};
        h.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(fs, h) == SidecarState::kNone && g_migrate.empty() && mtffFormatVersion(h.m) == 0 && mtffWriterVersion(h.m) == 0,
              "load: no file, the hook is not called, versions 0");
        const int mut = fs.mutations;
        check(save(fs, h, 0) && fs.files.empty() && fs.mutations == mut, "save: nothing set writes nothing (no header on its own), the card is not touched");
        check(save(fs, h, 100) && fs.files[kWs] == "tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768\nmt 0000aaaa 2324\nstrum.ramp 100\n" && fs.clean({kWs}),
              "save: the header is the first line of the file");

        // same build again: the hook is called with equal versions
        Host a;
        a.stamp = {0xaaaau, 2324u, true};
        a.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(fs, a) == SidecarState::kLoaded && g_migrate.size() == 1 && g_migrate[0].fileFormat == 1 && g_migrate[0].ourFormat == 1 &&
                  g_migrate[0].fileFf == kV140 && g_migrate[0].ourFf == kV140 && g_migrate[0].depth == 100,
              "hook: called once after the read with (file 1.4.0 fmt 1, ours 1.4.0 fmt 1) and the settings already parsed");
        const int mut2 = fs.mutations;
        check(save(fs, a, 100) && fs.mutations == mut2, "save: an unchanged file is not written again (header included)");

        // upgrade: a file of an older build
        MemFs up;
        up.files[kWs] = "tffproj 1 ff=1.3.0 build=old1234 cap=2048\nmt 0000aaaa 2324\nstrum.ramp 30\n";
        Host u;
        u.stamp = {0xaaaau, 2324u, true};
        u.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(up, u) == SidecarState::kLoaded && g_migrate.size() == 1 && g_migrate[0].fileFf < g_migrate[0].ourFf && u.m.depth == 30 && mtffWriterVersion(u.m) < mtffSelfVersion(u.m),
              "upgrade: an older writer is read, the hook sees file < ours");
        check(save(up, u, 31) && up.files[kWs] == "tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768\nmt 0000aaaa 2324\nstrum.ramp 31\n",
              "upgrade: the next save writes this build's header");

        // a bare header (a dev build): format 1, writer unknown
        MemFs dev;
        dev.files[kWs] = "tffproj 1\nmt 0000aaaa 2324\nstrum.ramp 30\nx.keep 1\n";
        Host d;
        d.stamp = {0xaaaau, 2324u, true};
        d.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(dev, d) == SidecarState::kLoaded && g_migrate.size() == 1 && g_migrate[0].fileFormat == 1 && g_migrate[0].fileFf == 0 && d.m.depth == 30,
              "dev file: no header fields = format 1, writer unknown, the hook sees (1, 0) and nothing else is migrated");
        check(save(dev, d, 30) && dev.files[kWs] == "tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768\nmt 0000aaaa 2324\nstrum.ramp 30\nx.keep 1\n",
              "dev file: saved with the header, the unknown line kept");

        // downgrade: a newer writer in the same format: known keys apply, unknown ones are carried, the header becomes ours
        MemFs dn;
        dn.files[kWs] = "tffproj 1 ff=1.5.0 build=cafe123 cap=40000\nmt 0000aaaa 2324\nstrum.ramp 30\nfx.future 3 4 5\ntrack.2.groove 12\nx.newkey hello world\n";
        Host w;
        w.stamp = {0xaaaau, 2324u, true};
        w.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(dn, w) == SidecarState::kLoaded && w.m.depth == 30 && w.m.groove[1] == 12 && !w.m.newer && mtffWriterVersion(w.m) == kV150 &&
                  g_migrate.size() == 1 && g_migrate[0].fileFf > g_migrate[0].ourFf && w.m.writerCap == 40000,
              "downgrade: a 1.5.0 file of format 1 loads on 1.4.0, known keys apply, the hook sees file > ours");
        check(save(dn, w, 35) && dn.files[kWs] == "tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768\nmt 0000aaaa 2324\nstrum.ramp 35\ntrack.2.groove 12\nfx.future 3 4 5\nx.newkey hello world\n",
              "downgrade: the save writes our header and keeps the lines this build does not know, byte exact");
        Host w2;
        w2.stamp = {0xaaaau, 2324u, true};
        w2.m.self = ver(1, 5, 0, "cafe123");
        check(load(dn, w2) == SidecarState::kLoaded && w2.m.depth == 35 && mtffWriterVersion(w2.m) == kV140 && w2.rawN > 0 &&
                  text(w2.raw, w2.rawN).find("fx.future 3 4 5\n") != std::string::npos,
              "downgrade: back on 1.5.0 the carried line is still there and the writer reads as 1.4.0 (an upgrade step)");

        // a higher format is never touched
        MemFs nf;
        const std::string newerFile = "tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nmt 0000aaaa 2324\nstrum.ramp 100\nfuture 1\n";
        nf.files[kWs] = newerFile;
        Host f;
        f.stamp = {0xaaaau, 2324u, true};
        f.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(nf, f) == SidecarState::kUnreadable && f.m.newer && f.m.format == 2 && mtffWriterVersion(f.m) == kV150 && f.m.depth == 0 && g_migrate.empty(),
              "newer format: blocked, nothing applied, the writer is known for the status line, the hook is not called");
        check(save(nf, f, 0) && save(nf, f, 30) && nf.files[kWs] == newerFile && nf.clean({kWs}) && f.st == SidecarState::kUnreadable && f.m.newer,
              "newer format: never overwritten or removed, a setting to store is dropped (the save succeeds), the file and the card stay as they were");
        MemFs proj;
        char buf[kMtffMax];
        proj.files[kWs] = newerFile;
        const bool saveAs = mtffProjectStep(proj, kWs, kProj, true, f.st, buf, sizeof(buf));
        printf("      Save As of a newer format file: %d, project copy %s\n", int(saveAs), proj.files.count(kProj) ? (proj.files[kProj] == newerFile ? "identical" : "DIFFERENT") : "absent");
        check(saveAs && proj.files[kProj] == newerFile && proj.files[kWs] == newerFile, "newer format: Save As carries the file to the project folder byte exact");

        // the cap
        MemFs cp;
        std::string over = "tffproj 1 ff=1.5.0 cap=65536\n";
        while (over.size() <= size_t(kMtffMax)) over += "x.line 1234567890\n";
        cp.files[kWs] = over;
        Host c;
        c.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(cp, c) == SidecarState::kUnreadable && !c.m.newer && g_migrate.empty() && c.m.depth == 0, "cap: a file over this build's cap is unreadable, nothing applied, hook not called");
        check(save(cp, c, 0) && !save(cp, c, 30) && cp.files[kWs] == over, "cap: and it is never overwritten or removed");
        std::string exact = "tffproj 1 ff=1.5.0 cap=65536\nstrum.ramp 12\n";
        while (exact.size() + 18 <= size_t(kMtffMax)) exact += "x.line 1234567890\n";
        exact += std::string(size_t(kMtffMax) - exact.size() - 1, 'y') + "\n";
        MemFs ce;
        ce.files[kWs] = exact;
        Host e;
        e.m.self = ver(1, 4, 0, "5a1b2c3");
        check(exact.size() == size_t(kMtffMax) && load(ce, e) == SidecarState::kLoaded && e.m.depth == 12, "cap: a file of exactly the cap is read");
        const bool grew = save(ce, e, 12);
        printf("      rewrite of a cap sized file: %d, size %zu\n", int(grew), ce.files[kWs].size());
        check(!grew && ce.files[kWs] == exact, "cap: a rewrite whose header makes it grow past the cap is refused, the old file stays, nothing is cut");

        // a foreign file (no first line) and a header with a higher version but nothing else
        MemFs fo;
        fo.files[kWs] = "ff=1.4.0\nstrum.ramp 50\n";
        Host g;
        g.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(fo, g) == SidecarState::kForeign && g.m.depth == 0 && g_migrate.empty() && mtffFormatVersion(g.m) == 0, "foreign: no tffproj first line, the hook is not called");

        // stale stamp: the hook still runs, after the parking (part. lines still park)
        MemFs sf;
        sf.files[kWs] = "tffproj 1 ff=1.3.0\nmt 00002222 2324\nstrum.ramp 70\npart.1.engine 83\n";
        Host s2;
        s2.stamp = {0x1111u, 2324u, true};
        s2.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(sf, s2) == SidecarState::kLoaded && s2.m.stale && s2.m.parked == 1 && g_migrate.size() == 1 && g_migrate[0].depth == 70 && mtffWriterVersion(s2.m) == (1ul << 16 | 3ul << 8),
              "stale: the hook runs after the parking, the header of the stale read is kept");

        // failed open copy: nothing read, no hook
        MemFs oc;
        oc.files[kWs] = "tffproj 1 ff=1.4.0\nstrum.ramp 70\n";
        Host o;
        o.openStale = true;
        o.m.self = ver(1, 4, 0, "5a1b2c3");
        g_migrate.clear();
        check(load(oc, o) == SidecarState::kStale && g_migrate.empty() && mtffFormatVersion(o.m) == 0, "open copy failed: kStale, no hook, versions 0");
    }
    {   // power loss at every step of a save that rewrites the header (upgrade of the file on a save)
        MemFs probe;
        Host hp;
        hp.stamp = {0xaaaau, 2324u, true};
        hp.m.self = ver(1, 4, 0, "5a1b2c3");
        probe.files[kWs] = "tffproj 1 ff=1.3.0 cap=2048\nmt 0000aaaa 2324\nstrum.ramp 100\nx.keep 1\n";
        load(probe, hp);
        probe.mutations = 0;
        save(probe, hp, 20);
        const int steps = probe.mutations;
        bool ok = steps > 0;
        int oldN = 0, newN = 0;
        for (int cut = 1; cut <= steps; cut++)
        {
            MemFs fs;
            Host h;
            h.stamp = {0xaaaau, 2324u, true};
            h.m.self = ver(1, 4, 0, "5a1b2c3");
            fs.files[kWs] = "tffproj 1 ff=1.3.0 cap=2048\nmt 0000aaaa 2324\nstrum.ramp 100\nx.keep 1\n";
            load(fs, h);
            fs.cut = cut;
            save(fs, h, 20);
            fs.reboot();
            Host h2;
            h2.stamp = {0xaaaau, 2324u, true};
            h2.m.self = ver(1, 4, 0, "5a1b2c3");
            const SidecarState st = load(fs, h2);
            const bool isOld = h2.m.depth == 100 && mtffWriterVersion(h2.m) == (1ul << 16 | 3ul << 8);
            const bool isNew = h2.m.depth == 20 && mtffWriterVersion(h2.m) == kV140;
            oldN += isOld;
            newN += isNew;
            ok = ok && st == SidecarState::kLoaded && (isOld || isNew) && text(h2.raw, h2.rawN).find("x.keep 1\n") != std::string::npos;
        }
        printf("      %d cut points, old header %d, new header %d\n", steps, oldN, newN);
        check(ok, "power loss: at every step the next load finds the old header with the old keys or the new one with the new, the kept line always");
    }
    // ---- a project.mtff of a higher format: Save and Save As always work, the file stays byte for byte, edits made here are dropped
    {
        const std::string newerFile = "tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nmt 0000aaaa 2324\nstrum.ramp 100\nfuture 1\n";
        constexpr const char* kProj2 = "Projects/Other/project.mtff";
        auto fresh = [&](MemFs& fs, Host& h) {   // the open of a refused project: the project's file copied to the Workspace, then the load
            fs.files[kProj] = newerFile;
            char buf[kMtffMax];
            check(mtffProjectStep(fs, kProj, kWs, false, SidecarState::kLoaded, buf, sizeof(buf)), "refused: open copies the project's file to the Workspace");
            h.stamp = {0xaaaau, 2324u, true};
            h.m.self = ver(1, 4, 0, "5a1b2c3");
            return load(fs, h);
        };
        MemFs fs;
        Host h;
        check(fresh(fs, h) == SidecarState::kUnreadable && h.m.newer && h.m.depth == 0 && !hasContent(h), "refused: blocked at open, nothing applied, nothing to store yet");
        // Save with nothing changed: no loss, nothing to drop
        char buf[kMtffMax];
        check(save(fs, h, 0) && mtffProjectStep(fs, kWs, kProj, false, h.st, buf, sizeof(buf)) && fs.files[kWs] == newerFile && fs.files[kProj] == newerFile,
              "refused: Save with no FF setting changed succeeds, both files as they were");
        // a setting changed after the open: Save (Workspace save, then the project folder, in place)
        h.m.depth = 30;
        check(hasContent(h), "refused: a changed setting is something the writer would store (the host logs it as dropped)");
        const bool wsSave = save(fs, h, 30);
        const bool projSave = mtffProjectStep(fs, kWs, kProj, true, h.st, buf, sizeof(buf));
        check(wsSave && projSave, "refused: Save with a changed setting succeeds (Workspace save and project step)");
        check(fs.files[kWs] == newerFile && fs.files[kProj] == newerFile && fs.clean({kWs, kProj}),
              "refused: after Save both the Workspace file and the project's file are the newer file byte for byte, no .tmp or .bak left");
        check(h.st == SidecarState::kUnreadable && h.m.newer && h.rawN == 0, "refused: the state stays refused, no text of the dropped setting kept in memory");
        // autosave: the same Workspace save again and again
        h.m.groove[1] = 12;
        check(save(fs, h, 45) && save(fs, h, 46) && fs.files[kWs] == newerFile, "refused: repeated autosaves succeed and leave the Workspace file alone");
        // Save As to a new folder: the file is carried unchanged, the Workspace file too
        const bool saveAs = mtffProjectStep(fs, kWs, kProj2, true, h.st, buf, sizeof(buf));
        check(saveAs && fs.files[kProj2] == newerFile && fs.files[kWs] == newerFile && fs.files[kProj] == newerFile && fs.clean({kWs, kProj, kProj2}),
              "refused: Save As with a changed setting copies the newer file unchanged, the other two stay as they were");
        // the Workspace reload (power up, a project load) cannot bring the dropped edits back
        Host again;
        again.stamp = {0xaaaau, 2324u, true};
        check(load(fs, again) == SidecarState::kUnreadable && again.m.newer && again.m.depth == 0 && again.m.groove[1] == 0 && again.rawN == 0,
              "refused: a Workspace reload applies none of the dropped edits (depth 0, groove 0), the file still refuses");
        // a project without this much: no file at all in the Workspace is the old case, untouched by the change
        MemFs fn;
        Host hn;
        hn.stamp = {0xaaaau, 2324u, true};
        check(load(fn, hn) == SidecarState::kNone && save(fn, hn, 0) && fn.files.empty(), "refused: no file, nothing to store, nothing written (unchanged behaviour)");
        // other blocked states keep failing a save with something to store: a card error is not a newer format
        MemFs fe;
        Host he;
        fe.files[kWs] = "tffproj 1\nstrum.ramp 100\n";
        fe.unknown.insert(kWs);
        check(load(fe, he) == SidecarState::kUnreadable && !he.m.newer && !save(fe, he, 50), "refused: a card error is still blocked and a setting to store still fails");
        // a refused read that is then followed by a good file on the next load: the newer flag does not stick
        fe.reboot();
        check(load(fe, he) == SidecarState::kLoaded && !he.m.newer && save(fe, he, 50) && fe.files[kWs].find("strum.ramp 50\n") != std::string::npos,
              "refused: the flag is cleared by the next load, a normal file saves normally again");
    }
    {   // power loss at every step of Save (Workspace save + project step) and of Save As of a refused project with a changed setting
        for (int mode = 0; mode < 2; mode++)
        {
            const std::string newerFile = "tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nmt 0000aaaa 2324\nstrum.ramp 100\nfuture 1\n";
            const char* const dst = mode == 0 ? kProj : "Projects/Other/project.mtff";
            auto run = [&](MemFs& fs, Host& h) {
                fs.files[kProj] = newerFile;
                fs.files[kWs] = newerFile;
                h.stamp = {0xaaaau, 2324u, true};
                h.m.self = ver(1, 4, 0, "5a1b2c3");
                load(fs, h);
            };
            MemFs probe;
            Host hp;
            run(probe, hp);
            probe.mutations = 0;
            char buf[kMtffMax];
            bool a = save(probe, hp, 30);
            a = mtffProjectStep(probe, kWs, dst, true, hp.st, buf, sizeof(buf)) && a;
            const int steps = probe.mutations;
            bool ok = a && steps > 0;
            int wsSame = 0, dstSame = 0;
            for (int cut = 1; cut <= steps; cut++)
            {
                MemFs fs;
                Host h;
                run(fs, h);
                fs.cut = cut;
                save(fs, h, 30);
                mtffProjectStep(fs, kWs, dst, true, h.st, buf, sizeof(buf));
                fs.reboot();
                Host w, d;
                w.stamp = d.stamp = {0xaaaau, 2324u, true};
                const bool wsRef = load(fs, w) == SidecarState::kUnreadable && w.m.newer && fs.files[kWs] == newerFile;
                // the destination, after the recovery of an interrupted replace: absent (Save As cut early) or the newer file
                const SidecarState ds = load(fs, d, dst);
                const bool dstRef = ds == SidecarState::kNone ? mode == 1 :   // Save: the project's own file is always back after the recovery
                     (ds == SidecarState::kUnreadable && d.m.newer && fs.files[dst] == newerFile);
                wsSame += wsRef;
                dstSame += dstRef;
                ok = ok && wsRef && dstRef;
            }
            printf("      mode %s: %d cut points, Workspace file identical %d, destination identical or absent %d\n", mode == 0 ? "Save" : "Save As", steps, wsSame, dstSame);
            check(ok, mode == 0 ? "refused: power loss at every step of Save, the newer file is the same byte for byte after every cut"
                                : "refused: power loss at every step of Save As, the Workspace file and the target are the newer file or the target is absent, never torn");
        }
    }
    printf("%s: %d failed\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
