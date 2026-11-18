// project.mtff: the Tracker FF settings of a project that the binary project.mt has no room for. Plain text next to project.mt (in
// Workspace/ and in the project folder). No file system here: the codec is plain C++, the file rules are templates over the same small
// file operations type as plug_sidecar_io.h (which this header reuses: replace through .tmp and .bak, fetch after an interrupted
// replace, copy, the SidecarState rules, the path limits), so tools/plug_mtff_test.cpp runs the very code of the firmware on an
// in-memory card that fails or loses power at any step. Design: ref/mtff-sidecar-design.md (phase 1: this file, the strum.ramp key).
//
//   tffproj 1 ff=1.4.0 build=5a1b2c3 cap=32768
//                              required first line, written first on every save. 1 = the FORMAT version (below), ff= the Tracker FF
//                              version that wrote the file (A.B.C, mtStructs.h CORE_VER), build= its git sha (BUILD_ID, at most 8
//                              letters or digits), cap= the largest file that build reads (kMtffMax). The three tokens are optional
//                              and a reader ignores tokens it does not know: a bare "tffproj 1" (a dev build before the header) is
//                              format 1 written by an unknown build. A format above kMtffMajor is a file this build must not touch
//                              (kUnreadable). See "Versions" below
//   mt 3a51c2e0 2324           stamp: the CRC32 and size the project.mt held when this file was written (see below)
//   strum.ramp 100             Strum ramp depth 0..100 percent (strum_ramp.h). No line = 0 = off: the project sounds as it did
//                              before the setting existed. Not written at 0 (a value equal to the old behaviour is never written)
//   fx.slots 4                 the project uses FX3 and FX4 of every pattern step (core/source/fx34.h, one pattern_NN.mtx sidecar per pattern).
//                              Only the value 4 means on. No line = off = 2 FX slots, as every firmware before. Written only when on, so a
//                              project that never switched it on has the file it had without the key. A project-wide key: it applies on a
//                              stale file too. Not CE compatible: stock and 1.3.x ignore the line and the sidecars and play slots 1/2 only
//   macro.mode 0               the macro depth mode (open_patchdata.h) that a patch ADDED to the project takes: 0 Add, 1 Scale. No line = the
//                              patches take what they always did (a new patch Scale, a loaded file its own field 100). Written only
//                              when set. The patches already in the project never change with it. Stock and 1.3.x keep the line
//   seed.take 42               the sequencer's random seed 1..255 (seeded random takes). No line = 0 = Off
//   duck.send 3 60 50 50       send ducking (FEATURE_SEND_DUCK, duck_math.h): source, depth %, release, threshold. No line = off
//   track.3.groove -25         per track groove, -50..50 (Sequencer::kGrooveMax). Track numbers are 1..16. No line = 0
//   track.3.snap 2             scale snap of the track: 1 Nearest, 2 Down, 3 Up. No line = 0 = Off
//   track.3.follow 1 2         follow chord: the chord track (1..16) and the mode (1 Snap, 2 Transp, 3 Tr+Snap). No line = Off
//   pat.parts.mode 1           the Song screen plays the Parts list (1) instead of the playlist. No line = 0
//   pat.part.12 <32 hex> 16 2 1 0   Part 12 (1..255): the source pattern of each of the 16 tracks (hex bytes, 00 = track off), the
//                              length in steps, the passes (0 = End), flags and the baked pattern number (core/source/parts_format.h)
//   pat.start.12 <32 hex>      Part 12: the step of its source pattern where each of the 16 tracks starts to play (hex bytes 00..7f,
//                              00 = step 1). Written only for a Part that has a non-zero start, so a project that never used the
//                              field has the file it had without the key. No line = every start 0 = the Part plays as it always did.
//                              A build that does not know the key keeps the line (unknown line) and plays every track from step 1
//   <anything else>            a line this build does not know: kept, byte exact, in file order, after the known lines
//
// Since features-140 this is the ONE per project file of Tracker FF next to plugins.txt (the master inserts). The seed, scale snap and
// ducking used to be lines of plugins.txt, the groove, follow chord and Parts files of their own (groove.txt, follow.txt, parts).
// None of those ever shipped (1.4.0 is not released), so nothing reads them any more.
//
// Stale check. The project.mt already holds its own CRC32 in its last four bytes. This file stores the CRC and the size of the
// project.mt it was written with. At load they are compared with the ones of the project.mt just read: equal = the project.mt is
// still the one this file belongs to. Different = something else wrote it (stock firmware, an older FF, a Mac tool). The rule is benign
// on purpose (a power cut between the two writes of one Workspace save also looks stale):
//   project-wide keys (strum.ramp)         apply as they are, nothing a stock firmware does to the .mt changes their meaning
//   track.N keys (groove, snap, follow)    always apply: track numbers never move (fixed anchor), nothing a stock firmware does to the
//                                          .mt renumbers a track (user decision 2026-11-13)
//   pat. keys (Parts, pat.start.N)        always apply (user decision 2026-11-18, replaces the parking of 2026-11-13): pattern numbers
//                                          are fixed anchors like track numbers. A Part keeps naming pattern N whatever another firmware
//                                          did to it: an edited pattern plays as edited. A Part cell that names a pattern the project no
//                                          longer has (1.3.x can delete one) is blanked (track off, which plays the empty steps a missing
//                                          pattern played anyway) by the caller, mtffBlankMissing, and host.txt says so
//   slot-bound keys (part. instr.)         would apply only when the slot they belong to is unchanged. This build writes none of them and
//                                          has no anchor test (design phase 2), so on a stale file it parks every line of those
//                                          namespaces: the line is renamed parked.<key>, kept in the file, applied by nobody, and the load
//                                          says so in one status line
//   no mt line (a file written by hand)    cannot be checked: not stale
// After a load the next save writes a fresh stamp.
//
// Versions (ref/mtff-version.md). Two numbers, with different jobs:
//   format   the integer after "tffproj". Bumped ONLY when a key this build writes changes its meaning or the file layout changes in a
//            way an older reader would misapply. A file of a higher format is read by nobody older: kUnreadable, nothing applied, the
//            file never overwritten or removed (so an older build cannot damage it), one status line says so. A new key is NOT a bump
//   writer   the ff= token, the Tracker FF version that last wrote the file. Every save rewrites it with the version of the build
//            that saves (last writer wins), unknown lines kept. A file of a newer writer in the same format is read: known keys apply,
//            unknown ones are kept byte exact, one status line says "written by FF x.y.z". A file with no ff= (a dev build) is
//            writer unknown (0)
// mtffMigrate (the one place for upgrade steps) is called after every successful read with both versions.
//
// A project with no setting that differs from the old behaviour and no kept line has no file at all (write gives 0, the caller removes
// the file), exactly as a project of a firmware without this file.
#pragma once

#include "plug_sidecar_io.h"

#include "../core/libs/Audio/duck_math.h"
#include "../core/source/parts_format.h"

namespace plug
{

constexpr int kMtffTracks = 16;           // kAllTracks
constexpr int kSnapTracks = 16;          // tracks of the scale snap (core/source/scales.h kScaleSnapTracks)
constexpr int kSnapModes = 4;            // Off, Nearest, Down, Up (scales.h ScaleSnapMode)
constexpr int kMtffMax = 32768;          // bytes of the whole file: one read buffer. The longest file this build writes is about 28.3 KB
                                         // (255 Parts of 59 bytes, 16 tracks of three lines, and a pat.start line of 46 bytes for every
                                         // Part), a parked copy of it 3.6 KB more. A larger file is not read (kUnreadable). 1.4.0 before
                                         // pat.start read 20,480 B: a file larger than that (255 Parts, most with starts) is no file to it
constexpr int kMtffMajor = 1;            // "tffproj 1": the format version of this build (see Versions above)
constexpr int kMtffBuildLen = 8;         // longest build= token
constexpr const char* kMtffName = "project.mtff";

// A Tracker FF build: the version A.B.C and its git sha. valid false = unknown (a file with no ff= token, a build that did not say)
struct MtffId
{
    uint8_t major, minor, patch;
    bool valid;
    char build[kMtffBuildLen + 1];   // "" = none
};

// What mtffMigrate is told: a format version and the packed FF version of the build that wrote it (0 = unknown)
struct MtffVersion
{
    uint16_t format;
    uint32_t ff;   // (major << 16) | (minor << 8) | patch
};

struct Mtff
{
    uint8_t depth;     // strum.ramp, 0..100, absent 0
    uint8_t seed;      // seed.take, 0 Off
    uint8_t fxSlots;   // fx.slots, 4 = FX3 and FX4 on, 0 = off (absent)
    uint8_t macroMode; // macro.mode, 1 = Add, 2 = Scale for a patch added to the project, 0 = each patch as before (absent)
    DuckParams duck;   // duck.send, source 0 = off
    int8_t groove[kMtffTracks];        // track.N.groove, -50..50
    uint8_t snap[kMtffTracks];         // track.N.snap, 0 Off .. kSnapModes - 1
    uint8_t followMode[kMtffTracks];   // track.N.follow: 0 Off, 1 Snap, 2 Transp, 3 Tr+Snap
    uint8_t followSource[kMtffTracks]; // ... and the chord track 0..15
    parts::List* parts;                // where the pat.part. lines go (read) and come from (write). Not part of the state this struct
                                       // owns: set by the caller, kept by every clear. nullptr = this build has no Parts, the lines are
                                       // then not owned and are kept as unknown lines
    bool hasStamp;     // the file has an mt line
    uint32_t crc;      // the stamp: CRC32 of the project.mt as it was when the file was written
    uint32_t size;     // ... and its size
    uint16_t parked;   // lines parked by the last load (stale file, slot-bound keys), 0 when none
    bool stale;        // the last load found the stamp different from the project.mt's
    bool newer;        // the file is of a higher major than this build: nothing applied, nothing written
    // The header of the file the last read found (all zero without a file). The caller sets self (like parts) and every clear keeps it
    uint16_t format;   // the format version of the file: 1 for a bare "tffproj 1", 0 = nothing was read
    MtffId writer;     // ff= and build=, valid false when the file has no ff= token
    uint32_t writerCap;   // cap=, 0 when absent
    MtffId self;       // the build that runs this code, written as the header of every save. Not valid: the header is the bare "tffproj <major>"
};

// What the last read of a project.mt (or the write of one) said: the CRC32 in its last four bytes and its size
struct MtStamp
{
    uint32_t crc, size;
    bool valid;
};

namespace mtff
{
inline bool isKey(const char* p, const char* e, const char* key)
{
    const size_t n = strlen(key);
    return size_t(e - p) >= n && memcmp(p, key, n) == 0 && (size_t(e - p) == n || p[n] == ' ' || p[n] == '\r');
}
// The namespaces of keys that belong to a movable slot of the .mt (synth part, instrument). track. and pat. are not: track and pattern
// numbers never move, so a stale file still applies them
inline bool slotBound(const char* p, const char* e)
{
    static const char* const kNs[] = {"part.", "instr."};
    for (const char* ns : kNs)
    {
        const size_t n = strlen(ns);
        if (size_t(e - p) > n && memcmp(p, ns, n) == 0) return true;
    }
    return false;
}
inline const char* lineEnd(const char* p, const char* end)
{
    while (p < end && *p != '\n') p++;
    return p;
}
inline bool getU(const char* p, const char* e, uint32_t& v)
{
    v = 0;
    int k = 0;
    for (; p < e && *p >= '0' && *p <= '9' && k < 10; p++, k++) v = v * 10u + uint32_t(*p - '0');
    while (p < e && (*p == ' ' || *p == '\r')) p++;
    return k > 0 && p == e && k < 10;
}
// The next decimal number of a line: leading spaces skipped, at most 9 digits. False: no digits there
inline bool nextU(const char*& q, const char* e, uint32_t& v)
{
    while (q < e && *q == ' ') q++;
    v = 0;
    int k = 0;
    for (; q < e && *q >= '0' && *q <= '9' && k < 10; q++, k++) v = v * 10u + uint32_t(*q - '0');
    return k > 0 && k < 10;
}
inline bool atEnd(const char* q, const char* e)
{
    while (q < e && (*q == ' ' || *q == '\r')) q++;
    return q == e;
}
// "track.<1..16>.": t is 0 based, rest points behind the second dot
inline bool trackKey(const char* p, const char* e, int& t, const char*& rest)
{
    if (e - p < 9 || memcmp(p, "track.", 6) != 0) return false;
    const char* q = p + 6;
    int v = 0, d = 0;
    for (; q < e && *q >= '0' && *q <= '9' && d < 2; q++, d++) v = v * 10 + (*q - '0');
    if (!d || q >= e || *q != '.' || v < 1 || v > kMtffTracks) return false;
    t = v - 1;
    rest = q + 1;
    return true;
}
// "<ns><1..255> " (ns = "pat.part." or "pat.start."): idx is 1 based, rest points at the value
inline bool partKey(const char* p, const char* e, int& idx, const char*& rest, const char* ns = "pat.part.")
{
    const size_t nl = strlen(ns);
    if (size_t(e - p) < nl + 2 || memcmp(p, ns, nl) != 0) return false;
    const char* q = p + nl;
    int v = 0, d = 0;
    for (; q < e && *q >= '0' && *q <= '9' && d < 4; q++, d++) v = v * 10 + (*q - '0');
    if (!d || q >= e || *q != ' ' || v < 1 || v > parts::kMax) return false;
    idx = v;
    rest = q + 1;
    return true;
}
// A line this build writes itself (it regenerates it from its state): everything else is kept as it is. withParts: the build has the
// Parts list. A well formed key only, a line like track.99.groove or track.3.groove2 is somebody else's and is kept.
inline bool owned(const char* p, const char* e, bool withParts)
{
    if (isKey(p, e, "tffproj") || isKey(p, e, "mt") || isKey(p, e, "strum.ramp") || isKey(p, e, "seed.take") || isKey(p, e, "duck.send") ||
        isKey(p, e, "fx.slots") ||
        isKey(p, e, "macro.mode"))
        return true;
    int t, idx;
    const char* r;
    if (trackKey(p, e, t, r)) return isKey(r, e, "groove") || isKey(r, e, "snap") || isKey(r, e, "follow");
    if (!withParts) return false;
    return isKey(p, e, "pat.parts.mode") || partKey(p, e, idx, r) || partKey(p, e, idx, r, "pat.start.");
}
inline int hexVal(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
}
// ---- the header line (first line, see Versions above)
inline uint32_t pack(const MtffId& id)
{
    return id.valid ? (uint32_t(id.major) << 16) | (uint32_t(id.minor) << 8) | id.patch : 0u;
}
// "A.B.C" with each part 0..255 and nothing after it
inline bool parseVer(const char* p, const char* e, MtffId& id)
{
    uint32_t v[3];
    for (int i = 0; i < 3; i++)
    {
        v[i] = 0;
        int k = 0;
        for (; p < e && *p >= '0' && *p <= '9' && k < 4; p++, k++) v[i] = v[i] * 10u + uint32_t(*p - '0');
        if (k == 0 || v[i] > 255u || (i < 2 && (p >= e || *p++ != '.'))) return false;
    }
    if (p != e) return false;
    id.major = uint8_t(v[0]);
    id.minor = uint8_t(v[1]);
    id.patch = uint8_t(v[2]);
    id.valid = true;
    return true;
}
// The first line "tffproj <format> [name=value ...]": format and the tokens this build knows into m (ff=, build=, cap=). A token it does
// not know, or a known one that does not parse, is skipped: the file then reads as written by an unknown build. False: not a
// project.mtff line
inline bool parseHead(const char* p, const char* e, Mtff& m)
{
    if (e - p < 8 || memcmp(p, "tffproj ", 8) != 0) return false;
    p += 8;
    uint32_t major = 0;
    int k = 0;
    for (; p < e && *p >= '0' && *p <= '9' && k < 10; p++, k++) major = major * 10u + uint32_t(*p - '0');
    if (k == 0 || k >= 10 || (p < e && *p != ' ' && *p != '\r')) return false;
    m.format = uint16_t(major > 65535u ? 65535u : major);
    while (p < e)
    {
        if (*p == ' ' || *p == '\r')
        {
            p++;
            continue;
        }
        const char* t = p;
        while (p < e && *p != ' ' && *p != '\r') p++;
        const size_t n = size_t(p - t);
        uint32_t v;
        if (n > 3 && memcmp(t, "ff=", 3) == 0)
        {
            MtffId id;
            memset(&id, 0, sizeof(id));
            if (parseVer(t + 3, p, id)) m.writer = id;
        }
        else if (n > 6 && memcmp(t, "build=", 6) == 0)
        {
            int b = 0;
            for (const char* q = t + 6; q < p && b < kMtffBuildLen; q++)
            {
                const char c = *q;
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) break;
                m.writer.build[b++] = c;
            }
            m.writer.build[b] = 0;
        }
        else if (n > 4 && memcmp(t, "cap=", 4) == 0 && getU(t + 4, p, v))
            m.writerCap = v;
    }
    if (!m.writer.valid) m.writer.build[0] = 0;   // a sha with no version says nothing
    return true;
}
}   // namespace mtff

// Clears every setting of m. The Parts list the caller pointed it at is emptied too. m.parts and m.self (set by the caller) are kept.
inline void mtffClear(Mtff& m)
{
    parts::List* const pl = m.parts;
    const MtffId self = m.self;
    memset(&m, 0, sizeof(m));
    m.parts = pl;
    m.self = self;
    if (pl) memset(pl, 0, sizeof(*pl));
}

// Fills m (cleared first). False when the text is not a project.mtff (no first line "tffproj <digits>"). A higher major reads as
// true with m.newer set and nothing else applied. slotBound false: the pat. lines (Parts) are not applied (no load does that since
// 2026-11-18: a stale file applies them too, the tests still use it). The track.
// lines always apply.
inline bool mtffRead(const char* b, int len, Mtff& m, bool slotBound = true)
{
    mtffClear(m);
    const char* const end = b + len;
    const char* p = b;
    const char* e = mtff::lineEnd(p, end);
    if (!mtff::parseHead(p, e, m)) return false;
    if (m.format > uint16_t(kMtffMajor))
    {
        m.newer = true;
        return true;
    }
    for (p = e + 1; p < end; p = e + 1)
    {
        e = mtff::lineEnd(p, end);
        int t, idx;
        const char* r;
        uint32_t v;
        if (mtff::isKey(p, e, "strum.ramp"))
        {
            if (e - p > 11 && mtff::getU(p + 11, e, v)) m.depth = uint8_t(v > 100u ? 100u : v);
        }
        else if (mtff::isKey(p, e, "seed.take"))
        {   // anything but 1..255 is Off
            const char* q = p + 9;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v >= 1 && v <= 255) m.seed = uint8_t(v);
        }
        else if (mtff::isKey(p, e, "fx.slots"))
        {   // only 4 means on, anything else leaves the project at 2 FX slots
            const char* q = p + 8;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e)) m.fxSlots = v == 4 ? 4 : 0;
        }
        else if (mtff::isKey(p, e, "macro.mode"))
        {   // 0 Add, 1 Scale, anything else leaves each patch as it was
            const char* q = p + 10;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v <= 1) m.macroMode = uint8_t(v + 1);
        }
        else if (mtff::isKey(p, e, "duck.send"))
        {   // four numbers, a missing or too large one leaves ducking off
            uint32_t d[4];
            const char* q = p + 9;
            int k = 0;
            for (; k < 4 && mtff::nextU(q, e, d[k]) && d[k] <= 255; k++) {}
            const DuckParams dp = k == 4 ? DuckParams{uint8_t(d[0]), uint8_t(d[1]), uint8_t(d[2]), uint8_t(d[3])} : DuckParams{0, 0, 0, 0};
            if (k == 4 && mtff::atEnd(q, e) && d[0] >= 1 && duckValid(dp)) m.duck = dp;
        }
        else if (mtff::isKey(p, e, "mt"))
        {
            const char* q = p + 3;
            uint32_t crc, size;
            if (e - p > 3 && sc::getHex(q, e, crc) && q < e && *q++ == ' ' && mtff::getU(q, e, size))
            {
                m.hasStamp = true;
                m.crc = crc;
                m.size = size;
            }
        }
        else if (mtff::trackKey(p, e, t, r))
        {
            const char* q = r;
            if (mtff::isKey(r, e, "groove"))
            {   // -50..50, a value past it is clamped, a line that is no number is skipped
                q += 6;
                while (q < e && *q == ' ') q++;
                const bool neg = q < e && *q == '-';
                if (q < e && (*q == '-' || *q == '+')) q++;
                if (mtff::nextU(q, e, v) && mtff::atEnd(q, e))
                {
                    const int g = v > 50u ? 50 : int(v);
                    m.groove[t] = int8_t(neg ? -g : g);
                }
            }
            else if (mtff::isKey(r, e, "snap"))
            {
                q += 4;
                if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v < uint32_t(kSnapModes)) m.snap[t] = uint8_t(v);
            }
            else if (mtff::isKey(r, e, "follow"))
            {
                uint32_t src, mode;
                q += 6;
                if (mtff::nextU(q, e, src) && mtff::nextU(q, e, mode) && mtff::atEnd(q, e) && src >= 1 && src <= uint32_t(kMtffTracks) &&
                    src - 1 != uint32_t(t) && mode >= 1 && mode <= 3)
                {
                    m.followSource[t] = uint8_t(src - 1);
                    m.followMode[t] = uint8_t(mode);
                }
            }
        }
        else if (slotBound && m.parts && mtff::isKey(p, e, "pat.parts.mode"))
        {
            const char* q = p + 14;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e)) m.parts->mode = v == 1 ? 1 : 0;
        }
        else if (slotBound && m.parts && mtff::partKey(p, e, idx, r))
        {   // 32 hex digits, then length, passes, flags and the baked pattern
            parts::Part pt;
            memset(&pt, 0, sizeof(pt));
            bool ok = e - r > 33;
            for (int i = 0; ok && i < 16; i++)
            {
                const int hi = mtff::hexVal(r[2 * i]), lo = mtff::hexVal(r[2 * i + 1]);
                ok = hi >= 0 && lo >= 0;
                pt.src[i] = uint8_t(hi * 16 + lo);
            }
            const char* q = r + 32;
            uint32_t len, rep, fl, bk;
            ok = ok && mtff::nextU(q, e, len) && mtff::nextU(q, e, rep) && mtff::nextU(q, e, fl) && mtff::nextU(q, e, bk) &&
                 mtff::atEnd(q, e) && fl <= 255 && bk <= 255;
            if (ok)
            {
                pt.length = uint8_t(len > parts::kMaxLength ? parts::kMaxLength : len);
                pt.repeats = uint8_t(rep > parts::kMaxRepeats ? parts::kMaxRepeats : rep);
                pt.flags = uint8_t(fl);
                pt.baked = uint8_t(bk);
                m.parts->part[idx - 1] = pt;
                if (m.parts->count < idx) m.parts->count = uint16_t(idx);
            }
        }
        else if (slotBound && m.parts && mtff::partKey(p, e, idx, r, "pat.start."))
        {   // 32 hex digits: the start step of each track (0..127). A line that is not well formed leaves the Part's starts at 0
            uint8_t st[parts::kTracks];
            bool ok = e - r >= 32 && (e - r == 32 || r[32] == '\r');
            for (int i = 0; ok && i < 16; i++)
            {
                const int hi = mtff::hexVal(r[2 * i]), lo = mtff::hexVal(r[2 * i + 1]);
                ok = hi >= 0 && lo >= 0;
                st[i] = uint8_t(hi * 16 + lo > parts::kMaxStart ? parts::kMaxStart : hi * 16 + lo);
            }
            if (ok) memcpy(m.parts->start[idx - 1], st, sizeof(st));
        }
    }
    return true;
}

// Does m hold any setting that differs from the old behaviour
inline bool mtffAny(const Mtff& m)
{
    bool any = m.depth != 0 || m.seed != 0 || m.fxSlots != 0 || m.macroMode != 0 || m.duck.source != 0 || (m.parts && m.parts->count > 0);
    for (int t = 0; t < kMtffTracks; t++) any = any || m.groove[t] != 0 || m.snap[t] != 0 || m.followMode[t] != 0;
    return any;
}

// The file text for m followed by the lines of raw (the file as it was read) that this build does not own, in order. Returns the
// text length, 0 when no file is needed (nothing set and no kept line), -1 when it would not fit cap. The result never has more than
// cap bytes.
inline int mtffWrite(const Mtff& m, const char* raw, int rawN, char* out, int cap)
{
    // pass 1: what raw keeps
    int kept = 0;
    const char* const end = raw + rawN;
    const bool withParts = m.parts != nullptr;
    for (const char* p = raw; p < end;)
    {
        const char* e = mtff::lineEnd(p, end);
        if (e > p && !mtff::owned(p, e, withParts)) kept += int(e - p) + 1;
        p = e + 1;
    }
    if (!mtffAny(m) && kept == 0) return 0;
    if (cap < 64) return -1;
    // the header, first on every save: the format, then who wrote it (the tokens a reader may ignore)
    int n = sc::putStr(out, 0, cap, "tffproj ");
    n = sc::putU(out, n, cap, uint32_t(kMtffMajor));
    if (m.self.valid)
    {
        n = sc::putStr(out, n, cap, " ff=");
        n = sc::putU(out, n, cap, m.self.major);
        n = sc::putStr(out, n, cap, ".");
        n = sc::putU(out, n, cap, m.self.minor);
        n = sc::putStr(out, n, cap, ".");
        n = sc::putU(out, n, cap, m.self.patch);
        if (m.self.build[0])
        {
            n = sc::putStr(out, n, cap, " build=");
            n = sc::putStr(out, n, cap, m.self.build);
        }
        n = sc::putStr(out, n, cap, " cap=");
        n = sc::putU(out, n, cap, uint32_t(kMtffMax));
    }
    n = sc::putStr(out, n, cap, "\n");
    auto num = [&](const char* key, uint32_t v) {
        n = sc::putStr(out, n, cap, key);
        n = sc::putU(out, n, cap, v);
        n = sc::putStr(out, n, cap, "\n");
    };
    auto track = [&](int t, const char* name) {   // "track.<t+1>.<name> "
        n = sc::putStr(out, n, cap, "track.");
        n = sc::putU(out, n, cap, uint32_t(t + 1));
        n = sc::putStr(out, n, cap, ".");
        n = sc::putStr(out, n, cap, name);
        n = sc::putStr(out, n, cap, " ");
    };
    if (m.hasStamp)
    {
        n = sc::putStr(out, n, cap, "mt ");
        n = sc::putHex(out, n, cap, m.crc);
        n = sc::putStr(out, n, cap, " ");
        n = sc::putU(out, n, cap, m.size);
        n = sc::putStr(out, n, cap, "\n");
    }
    if (m.depth) num("strum.ramp ", m.depth);
    if (m.seed) num("seed.take ", m.seed);
    if (m.fxSlots) num("fx.slots ", 4);
    if (m.macroMode) num("macro.mode ", m.macroMode - 1u);
    if (m.duck.source != 0)
    {
        const DuckParams d = duckClamp(m.duck);
        n = sc::putStr(out, n, cap, "duck.send ");
        n = sc::putU(out, n, cap, d.source);
        n = sc::putStr(out, n, cap, " ");
        n = sc::putU(out, n, cap, d.depth);
        n = sc::putStr(out, n, cap, " ");
        n = sc::putU(out, n, cap, d.release);
        n = sc::putStr(out, n, cap, " ");
        n = sc::putU(out, n, cap, d.thresh);
        n = sc::putStr(out, n, cap, "\n");
    }
    for (int t = 0; t < kMtffTracks; t++)
    {
        if (m.groove[t] != 0)
        {
            track(t, "groove");
            n = sc::putStr(out, n, cap, m.groove[t] < 0 ? "-" : "");
            n = sc::putU(out, n, cap, uint32_t(m.groove[t] < 0 ? -m.groove[t] : m.groove[t]));
            n = sc::putStr(out, n, cap, "\n");
        }
        if (m.snap[t] != 0 && m.snap[t] < kSnapModes)
        {
            track(t, "snap");
            n = sc::putU(out, n, cap, m.snap[t]);
            n = sc::putStr(out, n, cap, "\n");
        }
        if (m.followMode[t] >= 1 && m.followMode[t] <= 3 && m.followSource[t] < kMtffTracks && m.followSource[t] != t)
        {
            track(t, "follow");
            n = sc::putU(out, n, cap, uint32_t(m.followSource[t] + 1));
            n = sc::putStr(out, n, cap, " ");
            n = sc::putU(out, n, cap, m.followMode[t]);
            n = sc::putStr(out, n, cap, "\n");
        }
    }
    if (m.parts && m.parts->count > 0)
    {
        const parts::List& pl = *m.parts;
        if (pl.mode == 1) n = sc::putStr(out, n, cap, "pat.parts.mode 1\n");
        const int count = pl.count > parts::kMax ? parts::kMax : pl.count;
        for (int i = 0; i < count; i++)
        {
            const parts::Part& pt = pl.part[i];
            n = sc::putStr(out, n, cap, "pat.part.");
            n = sc::putU(out, n, cap, uint32_t(i + 1));
            n = sc::putStr(out, n, cap, " ");
            for (int k = 0; k < parts::kTracks && n + 2 <= cap; k++)
            {
                out[n++] = "0123456789abcdef"[pt.src[k] >> 4];
                out[n++] = "0123456789abcdef"[pt.src[k] & 15];
            }
            const uint8_t f[4] = {pt.length, pt.repeats, pt.flags, pt.baked};
            for (int k = 0; k < 4; k++)
            {
                n = sc::putStr(out, n, cap, " ");
                n = sc::putU(out, n, cap, f[k]);
            }
            n = sc::putStr(out, n, cap, "\n");
        }
        // the start steps, only of the Parts that have one (a project that never used them has no such line)
        for (int i = 0; i < count; i++)
        {
            const uint8_t* st = pl.start[i];
            bool any = false;
            for (int k = 0; k < parts::kTracks; k++) any = any || st[k] != 0;
            if (!any) continue;
            n = sc::putStr(out, n, cap, "pat.start.");
            n = sc::putU(out, n, cap, uint32_t(i + 1));
            n = sc::putStr(out, n, cap, " ");
            for (int k = 0; k < parts::kTracks && n + 2 <= cap; k++)
            {
                out[n++] = "0123456789abcdef"[st[k] >> 4];
                out[n++] = "0123456789abcdef"[st[k] & 15];
            }
            n = sc::putStr(out, n, cap, "\n");
        }
    }
    if (n + kept > cap) return -1;
    for (const char* p = raw; p < end;)
    {
        const char* e = mtff::lineEnd(p, end);
        if (e > p && !mtff::owned(p, e, withParts))
        {
            memcpy(out + n, p, size_t(e - p));
            n += int(e - p);
            out[n++] = '\n';
        }
        p = e + 1;
    }
    return n;
}

// A stale file: rename every slot-bound line to parked.<line>. The new text goes to tmp (tmpCap bytes) and back to raw (cap bytes).
// Returns the number of lines parked (0 when nothing needed it, or when the result would not fit: the lines stay as they are, which
// is harmless, no build applies them).
inline int mtffPark(char* raw, int& rawN, int cap, char* tmp, int tmpCap)
{
    const char* const end = raw + rawN;
    int parked = 0, n = 0;
    for (const char* p = raw; p < end;)
    {
        const char* e = mtff::lineEnd(p, end);
        if (mtff::slotBound(p, e))
        {
            parked++;
            n += 7;
        }
        n += int(e - p) + (e < end ? 1 : 0);
        p = e + 1;
    }
    if (!parked || n > cap || n > tmpCap) return 0;
    int at = 0;
    for (const char* p = raw; p < end;)
    {
        const char* e = mtff::lineEnd(p, end);
        if (mtff::slotBound(p, e))
        {
            memcpy(tmp + at, "parked.", 7);
            at += 7;
        }
        memcpy(tmp + at, p, size_t(e - p));
        at += int(e - p);
        if (e < end) tmp[at++] = '\n';
        p = e + 1;
    }
    memcpy(raw, tmp, size_t(at));
    rawN = at;
    return parked;
}

// ---- versions: the API and the one place for upgrade steps

// The format version of the file the last read found: 1 for a bare "tffproj 1", 0 when no file was read
inline int mtffFormatVersion(const Mtff& m)
{
    return m.format;
}
// The Tracker FF version that wrote the file the last read found, (major << 16) | (minor << 8) | patch, 0 = unknown (no file, or a
// header with no ff= token: a dev build before the header existed)
inline uint32_t mtffWriterVersion(const Mtff& m)
{
    return mtff::pack(m.writer);
}
// The version of this build in the same packing
inline uint32_t mtffSelfVersion(const Mtff& m)
{
    return mtff::pack(m.self);
}

// UPGRADE AND DOWNGRADE STEPS GO HERE. Called once after every successful read of a project.mtff (never for a missing file, a file
// of a higher format, a foreign file or a failed open copy), with what the file's header said and what this build is. m holds the
// settings as just read (the keys this build knows, applied afterwards by the caller): a step edits m, and the next save writes the
// result under the header of this build. Lines this build does not know are not in m, they are carried by the raw text.
//   file.format  1 for a bare "tffproj 1"            ours.format  kMtffMajor
//   file.ff      0 = written by an unknown build      ours.ff      0 = this build did not say its version
// file.ff < ours.ff is an upgrade (the file is older), file.ff > ours.ff a downgrade (a newer build wrote it, same format). Example
// of a step: if (file.ff && file.ff < packed(1, 5, 0)) { m.something = convertedFromOldMeaning; }
// 1.4.0 is the first build that writes a header: it has no step. MTFF_MIGRATE_PROBE lets tests see the calls.
inline void mtffMigrate(Mtff& m, const MtffVersion& file, const MtffVersion& ours)
{
    (void)m;
    (void)file;
    (void)ours;
#ifdef MTFF_MIGRATE_PROBE
    MTFF_MIGRATE_PROBE(m, file, ours);
#endif
}

// ---- the file rules (the plugins.txt rules of plug_sidecar_io.h, for this codec)

// Project load: the file at path into raw (rawN bytes), m filled. The state is plug_sidecar_io.h's: kNone no file, kLoaded read, kForeign
// a file that is no project.mtff (read as empty), kUnreadable a card error, a file over cap or a file of a higher major (never
// overwritten or removed, applies nothing).
template <class Fs> SidecarState mtffLoad(Fs& fs, const char* path, char* raw, int cap, Mtff& m, int& rawN)
{
    mtffClear(m);
    rawN = 0;
    int n = 0;
    switch (sidecarFetch(fs, path, raw, cap, n))
    {
        case Presence::kAbsent: return SidecarState::kNone;
        case Presence::kPresent: break;
        default: return SidecarState::kUnreadable;
    }
    if (n > 0 && mtffRead(raw, n, m))
    {
        if (m.newer) return SidecarState::kUnreadable;
        rawN = n;
        return SidecarState::kLoaded;
    }
    mtffClear(m);
    return SidecarState::kForeign;
}

// Workspace load after an open: mtffLoad, the stale flag of the project open's failed copy (cleared here) and the check against the
// stamp of the project.mt just read (have: it is known). A stale file parks its slot-bound lines (part. instr., none of them a key this
// build reads) and applies the rest, the Parts too (m.stale tells the caller to run mtffBlankMissing). tmp: scratch for that (tmpCap).
template <class Fs>
SidecarState mtffLoadStep(Fs& fs, const char* wsPath, char* raw, int cap, Mtff& m, int& rawN, bool& openStale, const MtStamp& stamp,
                          char* tmp, int tmpCap)
{
    SidecarState st = mtffLoad(fs, wsPath, raw, cap, m, rawN);
    if (openStale)
    {
        mtffClear(m);
        rawN = 0;
        openStale = false;
        return SidecarState::kStale;
    }
    if (st == SidecarState::kLoaded && m.hasStamp && stamp.valid && (m.crc != stamp.crc || m.size != stamp.size))
    {
        // slot-bound lines never apply on a stale file: parked (a rename). This build reads none of them, so m stays as read: the
        // project-wide, track. and pat. keys apply
        const int parked = mtffPark(raw, rawN, cap, tmp, tmpCap);
        m.stale = true;
        m.parked = uint16_t(parked);
    }
    if (st == SidecarState::kLoaded)
        mtffMigrate(m, MtffVersion{m.format, mtffWriterVersion(m)}, MtffVersion{uint16_t(kMtffMajor), mtffSelfVersion(m)});
    return st;
}

// A stale file's Parts (mtffLoadStep set m.stale): every cell that names a pattern the project no longer has is blanked (0 = the
// track is off), nothing else of the Part changes. has(pattern) true when the pattern file is there (a card error must answer true:
// nothing is blanked on a doubt), each pattern asked once. dropped(part, track, pattern) for the log, part and track from 0. Returns
// the cells blanked
template <class Has, class Dropped> int mtffBlankMissing(parts::List& l, Has has, Dropped dropped)
{
    uint32_t asked[8] = {}, gone[8] = {};
    int n = 0;
    for (int i = 0; i < int(l.count) && i < parts::kMax; i++)
        for (int t = 0; t < parts::kTracks; t++)
        {
            const uint8_t pat = l.part[i].src[t];
            if (!pat) continue;
            const uint32_t bit = 1u << (pat & 31);
            if (!(asked[pat >> 5] & bit))
            {
                asked[pat >> 5] |= bit;
                if (!has(pat)) gone[pat >> 5] |= bit;
            }
            if (!(gone[pat >> 5] & bit)) continue;
            l.part[i].src[t] = 0;
            n++;
            dropped(i, t, pat);
        }
    return n;
}

// Workspace save: the file when there is something to say, none otherwise. st is what the load found (updated). A blocked state
// touches nothing (true when there is nothing to save, false when there is), except a file of a higher format (m.newer): the save
// succeeds and the file stays as it is, the settings of this build are dropped (the caller says so). Unchanged text is not written
// again. tmp (tmpCap, at most cap) takes the new text. False: the card did not take it or the text does not fit, the settings are not
// on the card.
template <class Fs>
bool mtffSave(Fs& fs, const char* path, const Mtff& m, SidecarState& st, char* raw, int& rawN, int cap, char* tmp, int tmpCap)
{
    const int n = mtffWrite(m, raw, rawN, tmp, tmpCap < cap ? tmpCap : cap);
    if (n < 0) return false;
    if (sidecarBlocked(st)) return n == 0 || (st == SidecarState::kUnreadable && m.newer);   // newer format: file kept, edits dropped
    if (n == 0)
    {
        if (st == SidecarState::kForeign) return true;   // not ours, nothing of ours to store: kept
        if (st == SidecarState::kNone)                   // the card said there is none and this build wrote none since
        {
            rawN = 0;
            return true;
        }
        if (!sidecarRemove(fs, path)) return false;
        st = SidecarState::kNone;
        rawN = 0;
        return true;
    }
    if (st == SidecarState::kLoaded && n == rawN && memcmp(tmp, raw, size_t(n)) == 0) return true;   // as the card has it
    if (!sidecarReplace(fs, path, tmp, n)) return false;
    memcpy(raw, tmp, size_t(n));
    rawN = n;
    st = SidecarState::kLoaded;
    return true;
}

// Project save (after the Workspace save): the Workspace file to the project folder, as sidecarProjectStep. `has`: the Workspace
// state has something to store (mtffWrite != 0).
template <class Fs> bool mtffProjectStep(Fs& fs, const char* wsPath, const char* projectPath, bool has, SidecarState st, char* buf, int cap)
{
    if (st == SidecarState::kStale) return !has;
    return sidecarCopy(fs, wsPath, projectPath, buf, cap, sidecarBlocked(st));
}

}   // namespace plug
