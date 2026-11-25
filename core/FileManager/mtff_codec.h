// project.mtff: the Tracker FF settings of a project that the binary project.mt has no room for. Plain text next to project.mt (in
// Workspace/ and in the project folder). No file system here: the codec is plain C++, the file rules are templates over the same small
// file operations type as plug_sidecar_io.h (which this header reuses: replace through .tmp and .bak, fetch after an interrupted
// replace, copy, the SidecarState rules, the path limits), so tools/plug_mtff_test.cpp runs the very code of the firmware on an
// in-memory card that fails or loses power at any step. Design: ref/mtff-sidecar-design.md (phase 1: this file, the strum.ramp key).
//
//   tffproj 1 ff=1.4.0 build=5a1b2c3 cap=40960
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
//   tune.n 31                  the project tuning (core/source/mtTuning.h): notes per period 1..128. No line = 12. Written only when it is not 12.
//   tune.period 1901955        the period in 1e-3 cent (10000..4800000). No line = 1200000 (an octave). tune.root 60: the key of the 1/1
//                              (0..127), no line = 60. tune.hz 2616256: the Hz of the 1/1 in 1e-4 Hz, no line = 2616256 (261.6256 Hz). Each
//                              is written only when it is not the default, so a project in 12 EDO has the file it had without the keys. A build
//                              without the tuning keeps the lines (unknown lines) and plays the project in 12 EDO
//   tune.file NAME.scl         a Scala tuning (phase 2): the file the tuning came from, shown on the Scale file row. Only the name, the cents below
//                              are the tuning. tune.cents 701955 ...: the cents of the degrees 1..N-1 in 1e-3 cent (N - 1 values, strictly
//                              ascending, the last one below tune.period), tune.n is N and tune.period the last line of the file. Both lines
//                              are written only for a Scala tuning, so a project opens on another card without the file. A tune.cents that
//                              does not match tune.n and tune.period is ignored (the project then plays N equal steps). Older builds keep
//                              both lines (unknown lines) and play N equal steps of the period
//   tune.names ud              the note names of the tuning (phase 3): ups and downs (^C#4, vDb2) instead of degrees (17.2), for an EDO whose fifth
//                              names every degree. No line = degree names. Written only for ud. Older builds keep the line and show degrees
//   mpe.out 15                 MPE out (core/source/mtMpeOut.h): the MIDI instruments play through one MPE lower zone, master channel 1, this many
//                              member channels 2..(1+N), 1..15. No line = Off = the MIDI out as it always was. Written only when on. A build without
//                              MPE out keeps the line (unknown line) and plays the project as before
//   seed.take 42               the sequencer's random seed 1..255 (seeded random takes). No line = 0 = Off
//   duck.send 3 60 50 50       send ducking (FEATURE_SEND_DUCK, duck_math.h): source, depth %, release, threshold. No line = off
//   track.3.groove -25         per track groove, -50..50 (Sequencer::kGrooveMax). Track numbers are 1..16. No line = 0
//   track.3.snap 2             scale snap of the track: 1 Nearest, 2 Down, 3 Up. No line = 0 = Off
//   track.3.follow 1 2         follow chord: the chord track (1..16) and the mode (1 Snap, 2 Transp, 3 Tr+Snap). No line = Off
//   track.1.pair 1             linked stereo pair (core/source/stereoPairs.h): tracks 1+2, only on the odd track 1 3 5 7 (3+4, 5+6, 7+8). Only
//                              the value 1 links. No line = no pair = every track mono, as every firmware before. A project-wide key
//                              like the track ones: it applies on a stale file too. Read BEFORE the instruments load (plug::pairsPreload),
//                              a project that holds a pair loads the right channel of its stereo .pti files
//   pat.parts.mode 1           the Song screen plays the Parts list (1) instead of the playlist. No line = 0
//   pat.part.12 <32 hex> 16 2 1 0   Part 12 (1..255): the source pattern of each of the 16 tracks (hex bytes, 00 = track off), the
//                              length in steps, the passes (0 = End), flags and the baked pattern number (core/source/parts_format.h)
//   pat.start.12 <32 hex>      Part 12: the step of its source pattern where each of the 16 tracks starts to play (hex bytes 00..7f,
//                              00 = step 1). Written only for a Part that has a non-zero start, so a project that never used the
//                              field has the file it had without the key. No line = every start 0 = the Part plays as it always did.
//                              A build that does not know the key keeps the line (unknown line) and plays every track from step 1
//   tbl.3 12 1 4 0cff0000...   Table 3 (1..16, core/source/tables_format.h): tic (uSteps per row, 4..192), Loop 1 / Once 0, length in rows
//                              (1..16), then the rows up to the last one that is not empty, 8 hex digits each: N (int8, 80 = empty), V
//                              (percent, ff = empty), FX id, FX value. Written only for a table that differs from the empty default
//   instr.5.table 3            instrument 5 (1..67, the instrument index + 1) starts table 3 on every sequenced note. Written only when
//                              set. A slot-bound key: a stale file parks it (the instrument may be another one now)
//   synth.2.auto.1 1 2 6 0 50 50 10 500 500   Tracker+ and Mini only (MTFF_SYNTH_AUTO): the Synth Automation slot 1..4 of synth part 1..3, the
//                              ModSlot of .patch field 64 (open_patchdata.h) in the order type (1 LFO, 2 Env), shape 0..4, speed 0..28, part
//                              destination 0..3 (0 Pan, 1 Gain, 2 Reverb send, 3 Delay send, field 64 version 2 kind kPart), amount -100..100
//                              %, sustain %, attack, decay, release ms. The original Tracker keeps the slots in the patch of the part
//                              (field 64 of its .patch file, no project.mtff key), the stock parts of Tracker+ / Mini have no patch. Written
//                              only for a slot that is not Off, so a project without automation has the file it had. A project-wide key:
//                              applies on a stale file too (the part numbers never move). An original Tracker keeps the line (unknown line)
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

#include "../libs/Audio/duck_math.h"
#include "../source/parts_format.h"
#include "../source/tables_format.h"
#include "../source/mtTuning.h"
#include "../source/mtScala.h"

namespace plug
{

constexpr int kMtffTracks = 16;           // kAllTracks
constexpr int kSnapTracks = 16;          // tracks of the scale snap (core/source/scales.h kScaleSnapTracks)
constexpr int kSnapModes = 4;            // Off, Nearest, Down, Up (scales.h ScaleSnapMode)
constexpr int kMtffMax = 40960;          // bytes of the whole file: one read buffer. The longest file this build writes is about 31.8 KB (16 full tables)
                                         // (255 Parts of 59 bytes, 16 tracks of three lines, and a pat.start line of 46 bytes for every
                                         // Part), a parked copy of it 3.6 KB more. A larger file is not read (kUnreadable). 1.4.0 before
                                         // pat.start read 20,480 B: a file larger than that (255 Parts, most with starts) is no file to it
#ifdef MTFF_SYNTH_AUTO
constexpr int kMtffAutoParts = 3;        // synth.N.auto.K: the stock synth parts (kSynthParts) ...
constexpr int kMtffAutoSlots = 4;        // ... and the slots of a part (open_patchdata.h kModSlots), partAuto.h static_asserts both
#endif
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

#ifdef MTFF_SYNTH_AUTO
// One Synth Automation slot as the file has it: the run fields of a part destination slot (open_modrun.h ModRun), type 0 = Off = nothing written
struct MtffAutoSlot
{
    uint8_t type;      // 0 Off, 1 LFO, 2 Env (ModType)
    uint8_t shape;     // 0..4
    uint8_t speed;     // 0..28
    uint8_t dest;      // 0 Pan, 1 Gain, 2 Reverb send, 3 Delay send
    int8_t amount;     // -100..100 %
    uint8_t sustain;   // %
    uint16_t attack;   // ms
    uint16_t decay;
    uint16_t release;
};
#endif

struct Mtff
{
    uint8_t depth;     // strum.ramp, 0..100, absent 0
    uint8_t seed;      // seed.take, 0 Off
    uint8_t fxSlots;   // fx.slots, 4 = FX3 and FX4 on, 0 = off (absent)
    uint8_t macroMode; // macro.mode, 1 = Add, 2 = Scale for a patch added to the project, 0 = each patch as before (absent)
    DuckParams duck;   // duck.send, source 0 = off
    // The project tuning (core/source/mtTuning.h). Every field is 0 when absent = the default 12 notes of 1200 cents, 1/1 on key 60 at 261.6256 Hz
    uint8_t mpeOut;        // mpe.out 1..15: member channels of the MPE out zone, absent 0 = Off
    uint8_t tuneN;         // tune.n 1..128: notes per period
    uint8_t tuneRoot1;     // tune.root + 1 (key 0..127 of the 1/1)
    uint32_t tunePeriodMc; // tune.period in 1e-3 cent (10000..4800000)
    uint32_t tuneHzE4;     // tune.hz in 1e-4 Hz (80000..40000000)
    // The Scala degrees (tune.cents, tune.file): the live tables of core/source/mtTuning.h, set by the caller (like parts, kept by every clear). nullptr
    // = this build keeps no scale (the two lines are read as nothing and not written back)
    uint32_t* tuneDeg;     // 128 entries, deg[0] = 0, deg[1..tuneN - 1] = tune.cents
    char* tuneName;        // tuning::kNameMax bytes, tune.file
    uint8_t tuneScl;       // 1 = a valid tune.cents was read (or is to be written)
    uint8_t tuneNames;     // tune.names ud: 1 = ups and downs note names (phase 3), 0 = degree names
    int8_t groove[kMtffTracks];        // track.N.groove, -50..50
    uint8_t snap[kMtffTracks];         // track.N.snap, 0 Off .. kSnapModes - 1
    uint8_t followMode[kMtffTracks];   // track.N.follow: 0 Off, 1 Snap, 2 Transp, 3 Tr+Snap
    uint8_t followSource[kMtffTracks]; // ... and the chord track 0..15
    uint8_t pairs;                     // track.N.pair: bit k = the pair of tracks 2k+1 and 2k+2 is linked (stereoPairs.h), absent 0
    parts::List* parts;                // where the pat.part. lines go (read) and come from (write). Not part of the state this struct
                                       // owns: set by the caller, kept by every clear. nullptr = this build has no Parts, the lines are
                                       // then not owned and are kept as unknown lines
    tables::Pool* tables;              // the same for the tbl. and instr.N.table lines (the live table pool). nullptr = no tables in
                                       // this build: the lines are kept as unknown lines
#ifdef MTFF_SYNTH_AUTO
    MtffAutoSlot synthAuto[kMtffAutoParts][kMtffAutoSlots];   // synth.N.auto.K, every slot Off when absent
#endif
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
// "<ns><1..max><sep>": idx the number, rest after the separator
inline bool numKey(const char* p, const char* e, const char* ns, int max, char sep, int& idx, const char*& rest)
{
    const size_t nl = strlen(ns);
    if (size_t(e - p) < nl + 2 || memcmp(p, ns, nl) != 0) return false;
    const char* q = p + nl;
    int v = 0, d = 0;
    for (; q < e && *q >= '0' && *q <= '9' && d < 3; q++, d++) v = v * 10 + (*q - '0');
    if (!d || q >= e || *q != sep || v < 1 || v > max) return false;
    idx = v;
    rest = q + 1;
    return true;
}
#ifdef MTFF_SYNTH_AUTO
// "synth.<1..3>.auto.<1..4> ": p and s 0 based, rest points at the value
inline bool autoKey(const char* p, const char* e, int& part, int& slot, const char*& rest)
{
    if (e - p < 17 || memcmp(p, "synth.", 6) != 0 || p[6] < '1' || p[6] > '0' + kMtffAutoParts || memcmp(p + 7, ".auto.", 6) != 0 || p[13] < '1' ||
        p[13] > '0' + kMtffAutoSlots || p[14] != ' ')
        return false;
    part = p[6] - '1';
    slot = p[13] - '1';
    rest = p + 15;
    return true;
}
// An optionally signed decimal number: leading spaces skipped
inline bool nextS(const char*& q, const char* e, int32_t& v)
{
    while (q < e && *q == ' ') q++;
    const bool neg = q < e && *q == '-';
    if (q < e && *q == '-') q++;
    uint32_t u;
    if (!nextU(q, e, u) || u > 100000u) return false;
    v = neg ? -int32_t(u) : int32_t(u);
    return true;
}
#endif
inline bool tableKey(const char* p, const char* e, int& idx, const char*& rest)
{
    return numKey(p, e, "tbl.", tables::kTables, ' ', idx, rest);
}
inline bool bindKey(const char* p, const char* e, int& idx, const char*& rest)
{
    const char* r;
    if (!numKey(p, e, "instr.", tables::kInstruments, '.', idx, r) || !isKey(r, e, "table")) return false;
    rest = r + 5;
    return true;
}
// Parts list. A well formed key only, a line like track.99.groove or track.3.groove2 is somebody else's and is kept. withTables: the
// build has the table pool (tbl. and instr.N.table lines)
inline bool owned(const char* p, const char* e, bool withParts, bool withTables = false)
{
    if (withTables)
    {
        int i;
        const char* r;
        if (tableKey(p, e, i, r) || bindKey(p, e, i, r)) return true;
    }
    if (isKey(p, e, "tffproj") || isKey(p, e, "mt") || isKey(p, e, "strum.ramp") || isKey(p, e, "seed.take") || isKey(p, e, "duck.send") ||
        isKey(p, e, "fx.slots") || isKey(p, e, "mpe.out") ||
        isKey(p, e, "tune.n") || isKey(p, e, "tune.period") || isKey(p, e, "tune.root") || isKey(p, e, "tune.hz") || isKey(p, e, "tune.file") ||
        isKey(p, e, "tune.cents") || isKey(p, e, "tune.names") ||
        isKey(p, e, "macro.mode"))
        return true;
    int t, idx;
    const char* r;
#ifdef MTFF_SYNTH_AUTO
    if (autoKey(p, e, t, idx, r)) return true;
#endif
    if (trackKey(p, e, t, r)) return isKey(r, e, "groove") || isKey(r, e, "snap") || isKey(r, e, "follow") || isKey(r, e, "pair");
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
    tables::Pool* const tp = m.tables;
    const MtffId self = m.self;
    uint32_t* const td = m.tuneDeg;
    char* const tn = m.tuneName;
    memset(&m, 0, sizeof(m));
    m.parts = pl;
    m.tables = tp;
    m.self = self;
    m.tuneDeg = td;
    m.tuneName = tn;
    if (td) memset(td, 0, tuning::kMaxN * sizeof(uint32_t));
    if (tn) memset(tn, 0, tuning::kNameMax);
    if (pl) memset(pl, 0, sizeof(*pl));
    if (tp) tables::poolClear(*tp);
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
        else if (mtff::isKey(p, e, "mpe.out"))
        {   // 1..15 members, anything else is Off
            const char* q = p + 7;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v >= 1 && v <= 15) m.mpeOut = uint8_t(v);
        }
        else if (mtff::isKey(p, e, "tune.n"))
        {   // 1..128, 12 and anything else is the default (the line is then dropped, a value equal to the old behaviour is never kept)
            const char* q = p + 6;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v >= 1 && v <= 128 && v != 12) m.tuneN = uint8_t(v);
        }
        else if (mtff::isKey(p, e, "tune.names"))
        {   // "ud" is the only value that is kept, anything else (including deg) is the default
            const char* q = p + 10;
            while (q < e && *q == ' ') q++;
            if (e - q >= 2 && q[0] == 'u' && q[1] == 'd' && (e - q == 2 || q[2] == ' ' || q[2] == '\r')) m.tuneNames = 1;
        }
        else if (mtff::isKey(p, e, "tune.root"))
        {
            const char* q = p + 9;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v <= 127 && v != 60) m.tuneRoot1 = uint8_t(v + 1);
        }
        else if (mtff::isKey(p, e, "tune.period"))
        {
            const char* q = p + 11;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v >= 10000 && v <= 4800000 && v != 1200000) m.tunePeriodMc = v;
        }
        else if (mtff::isKey(p, e, "tune.hz"))
        {
            const char* q = p + 7;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v >= 80000 && v <= 40000000 && v != 2616256) m.tuneHzE4 = v;
        }
        else if (m.tuneDeg && mtff::isKey(p, e, "tune.file"))
        {   // the name: the rest of the line, a name that does not fit is no name
            const char* q = p + 9;
            while (q < e && *q == ' ') q++;
            const char* t = e;
            while (t > q && (t[-1] == '\r' || t[-1] == ' ')) t--;
            if (m.tuneName && t - q < tuning::kNameMax)
            {
                memcpy(m.tuneName, q, size_t(t - q));
                m.tuneName[t - q] = 0;
            }
        }
        else if (m.tuneDeg && mtff::isKey(p, e, "tune.cents"))
        {   // N - 1 values, 1e-3 cent. More than 127 or a value that does not parse: no scale (the check after the loop)
            const char* q = p + 10;
            int c = 0;
            bool ok = true;
            for (;;)
            {
                while (q < e && *q == ' ') q++;
                if (q >= e || *q == '\r') break;
                if (!mtff::nextU(q, e, v) || c >= tuning::kMaxN - 1) { ok = false; break; }
                m.tuneDeg[++c] = v;
            }
            m.tuneScl = ok ? uint8_t(c + 1) : 0;   // count + 1: 0 = none (a line with no value is N = 1: tuneScl 1)
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
#ifdef MTFF_SYNTH_AUTO
        else if (mtff::autoKey(p, e, t, idx, r))
        {   // type shape speed dest amount sustain attack decay release: a line that is not well formed or out of range leaves the slot Off
            const char* q = r;
            uint32_t ty, sh, sp, de, su, at, dc, re;
            int32_t am;
            if (mtff::nextU(q, e, ty) && mtff::nextU(q, e, sh) && mtff::nextU(q, e, sp) && mtff::nextU(q, e, de) && mtff::nextS(q, e, am) &&
                mtff::nextU(q, e, su) && mtff::nextU(q, e, at) && mtff::nextU(q, e, dc) && mtff::nextU(q, e, re) && mtff::atEnd(q, e) && ty >= 1 &&
                ty <= 2 && sh < 5u && sp < 29u && de < 4u && am >= -100 && am <= 100 && su <= 100u && at <= 10000u && dc <= 10000u && re <= 10000u)
            {
                MtffAutoSlot& a = m.synthAuto[t][idx];
                a.type = uint8_t(ty);
                a.shape = uint8_t(sh);
                a.speed = uint8_t(sp);
                a.dest = uint8_t(de);
                a.amount = int8_t(am);
                a.sustain = uint8_t(su);
                a.attack = uint16_t(at);
                a.decay = uint16_t(dc);
                a.release = uint16_t(re);
            }
        }
#endif
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
            else if (mtff::isKey(r, e, "pair"))
            {   // tracks 1 3 5 7 (t 0 2 4 6) only, only the value 1
                q += 4;
                if ((t & 1) == 0 && t < 8 && mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v == 1) m.pairs = uint8_t(m.pairs | (1u << (t >> 1)));
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
        else if (m.tables && mtff::tableKey(p, e, idx, r))
        {   // tic loop len, then the rows (8 hex digits each, at most 16). A line that is not well formed leaves the table empty
            const char* q = r;
            uint32_t tic, loop, len;
            bool ok = mtff::nextU(q, e, tic) && mtff::nextU(q, e, loop) && mtff::nextU(q, e, len) && loop <= 1 && len >= 1 &&
                      len <= uint32_t(tables::kRows);
            while (ok && q < e && *q == ' ') q++;
            const char* h = q;
            while (ok && q < e && *q != ' ' && *q != '\r') q++;
            const int digits = int(q - h);
            ok = ok && mtff::atEnd(q, e) && digits % (2 * tables::kRowBytes) == 0 && digits <= 2 * tables::kRowBytes * tables::kRows;
            tables::Table tb;
            tables::tableClear(tb);
            for (int i = 0; ok && i < digits / 2; i++)
            {
                const int hi = mtff::hexVal(h[2 * i]), lo = mtff::hexVal(h[2 * i + 1]);
                ok = hi >= 0 && lo >= 0;
                reinterpret_cast<uint8_t*>(&tb.row[i / tables::kRowBytes])[i % tables::kRowBytes] = uint8_t(hi * 16 + lo);
            }
            if (ok)
            {
                for (tables::Row& row : tb.row) row = tables::rowSanitize(row);
                tb.tic = tables::clampTic(tic);
                tb.loop = uint8_t(loop);
                tb.len = uint8_t(len);
                m.tables->table[idx - 1] = tb;
            }
        }
        else if (m.tables && mtff::bindKey(p, e, idx, r))
        {   // the table of an instrument, 1..16 (0 or anything else: none)
            const char* q = r;
            if (mtff::nextU(q, e, v) && mtff::atEnd(q, e) && v <= uint32_t(tables::kTables)) m.tables->bind[idx - 1] = uint8_t(v);
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
    if (m.tuneDeg)
    {   // tune.cents is a Scala tuning only when it matches tune.n and tune.period (the values of the line, N - 1 of them, strictly ascending,
        // below the period): anything else plays as N equal steps, as a build without the key would
        const int n = m.tuneN ? m.tuneN : 12;
        const uint32_t per = m.tunePeriodMc ? m.tunePeriodMc : tuning::kDefPeriodMc;
        const bool ok = m.tuneScl == n && scala::validTable(m.tuneDeg, n, per);
        m.tuneScl = ok ? 1 : 0;
        if (!ok)
        {
            memset(m.tuneDeg, 0, tuning::kMaxN * sizeof(uint32_t));
            if (m.tuneName) memset(m.tuneName, 0, tuning::kNameMax);
        }
    }
    return true;
}

// Does m hold any setting that differs from the old behaviour
inline bool mtffAny(const Mtff& m)
{
    bool any = m.depth != 0 || m.seed != 0 || m.fxSlots != 0 || m.macroMode != 0 || m.mpeOut != 0 || m.tuneN != 0 || m.tuneRoot1 != 0 || m.tunePeriodMc != 0 || m.tuneHzE4 != 0 || m.tuneScl != 0 || m.tuneNames != 0 || m.pairs != 0 || m.duck.source != 0 || (m.parts && m.parts->count > 0) ||
               (m.tables && tables::poolAny(*m.tables));
    for (int t = 0; t < kMtffTracks; t++) any = any || m.groove[t] != 0 || m.snap[t] != 0 || m.followMode[t] != 0;
#ifdef MTFF_SYNTH_AUTO
    for (int pt = 0; pt < kMtffAutoParts; pt++)
        for (int k = 0; k < kMtffAutoSlots; k++) any = any || m.synthAuto[pt][k].type != 0;
#endif
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
    const bool withTables = m.tables != nullptr;
    for (const char* p = raw; p < end;)
    {
        const char* e = mtff::lineEnd(p, end);
        if (e > p && !mtff::owned(p, e, withParts, withTables)) kept += int(e - p) + 1;
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
    if (m.mpeOut) num("mpe.out ", m.mpeOut);
    if (m.tuneN) num("tune.n ", m.tuneN);
    if (m.tuneRoot1) num("tune.root ", m.tuneRoot1 - 1u);
    if (m.tunePeriodMc) num("tune.period ", m.tunePeriodMc);
    if (m.tuneHzE4) num("tune.hz ", m.tuneHzE4);
    if (m.tuneNames) n = sc::putStr(out, n, cap, "tune.names ud\n");
    if (m.tuneScl && m.tuneDeg)
    {   // a Scala tuning: the name and the cents of degrees 1..N - 1 (N = tune.n, 12 when absent). The copy is what plays, the file is only where it came from
        if (m.tuneName && m.tuneName[0])
        {
            n = sc::putStr(out, n, cap, "tune.file ");
            n = sc::putStr(out, n, cap, m.tuneName);
            n = sc::putStr(out, n, cap, "\n");
        }
        n = sc::putStr(out, n, cap, "tune.cents");
        for (int i = 1; i < (m.tuneN ? m.tuneN : 12); i++)
        {
            n = sc::putStr(out, n, cap, " ");
            n = sc::putU(out, n, cap, m.tuneDeg[i]);
        }
        n = sc::putStr(out, n, cap, "\n");
    }
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
        if ((t & 1) == 0 && t < 8 && ((m.pairs >> (t >> 1)) & 1u) != 0)
        {
            track(t, "pair");
            n = sc::putStr(out, n, cap, "1\n");
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
#ifdef MTFF_SYNTH_AUTO
    for (int pt = 0; pt < kMtffAutoParts; pt++)
    {
        for (int k = 0; k < kMtffAutoSlots; k++)
        {
            const MtffAutoSlot& a = m.synthAuto[pt][k];
            if (a.type < 1 || a.type > 2) continue;
            n = sc::putStr(out, n, cap, "synth.");
            n = sc::putU(out, n, cap, uint32_t(pt + 1));
            n = sc::putStr(out, n, cap, ".auto.");
            n = sc::putU(out, n, cap, uint32_t(k + 1));
            const uint32_t f[4] = {a.type, a.shape, a.speed, a.dest};
            for (uint32_t x : f)
            {
                n = sc::putStr(out, n, cap, " ");
                n = sc::putU(out, n, cap, x);
            }
            n = sc::putStr(out, n, cap, a.amount < 0 ? " -" : " ");
            n = sc::putU(out, n, cap, uint32_t(a.amount < 0 ? -int(a.amount) : int(a.amount)));
            const uint32_t g[4] = {a.sustain, a.attack, a.decay, a.release};
            for (uint32_t x : g)
            {
                n = sc::putStr(out, n, cap, " ");
                n = sc::putU(out, n, cap, x);
            }
            n = sc::putStr(out, n, cap, "\n");
        }
    }
#endif
    if (m.tables)
    {   // the tables that differ from the empty default, then the instruments that name a table
        const tables::Pool& tp = *m.tables;
        for (int i = 0; i < tables::kTables; i++)
        {
            const tables::Table& tb = tp.table[i];
            if (tables::tableEmpty(tb)) continue;
            n = sc::putStr(out, n, cap, "tbl.");
            n = sc::putU(out, n, cap, uint32_t(i + 1));
            n = sc::putStr(out, n, cap, " ");
            n = sc::putU(out, n, cap, tb.tic);
            n = sc::putStr(out, n, cap, tb.loop ? " 1 " : " 0 ");
            n = sc::putU(out, n, cap, tb.len);
            const int rows = tables::usedRows(tb);
            if (rows) n = sc::putStr(out, n, cap, " ");
            for (int k = 0; k < rows * tables::kRowBytes && n + 2 <= cap; k++)
            {
                const uint8_t b = reinterpret_cast<const uint8_t*>(&tb.row[k / tables::kRowBytes])[k % tables::kRowBytes];
                out[n++] = "0123456789abcdef"[b >> 4];
                out[n++] = "0123456789abcdef"[b & 15];
            }
            n = sc::putStr(out, n, cap, "\n");
        }
        for (int i = 0; i < tables::kInstruments; i++)
        {
            if (!tp.bind[i] || tp.bind[i] > tables::kTables) continue;
            n = sc::putStr(out, n, cap, "instr.");
            n = sc::putU(out, n, cap, uint32_t(i + 1));
            n = sc::putStr(out, n, cap, ".table ");
            n = sc::putU(out, n, cap, tp.bind[i]);
            n = sc::putStr(out, n, cap, "\n");
        }
    }
    if (n + kept > cap) return -1;
    for (const char* p = raw; p < end;)
    {
        const char* e = mtff::lineEnd(p, end);
        if (e > p && !mtff::owned(p, e, withParts, withTables))
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
        // slot-bound lines never apply on a stale file: parked (a rename). The only one this build reads is instr.N.table (the table
        // bindings, cleared below), the rest of m stays as read: the project-wide, track., pat. and tbl. keys apply
        const int parked = mtffPark(raw, rawN, cap, tmp, tmpCap);
        if (m.tables) memset(m.tables->bind, 0, sizeof(m.tables->bind));   // instr.N.table is slot-bound: parked, never applied
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
