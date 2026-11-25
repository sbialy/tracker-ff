// Host test of the chord matcher and the chord names (core/source/mtChord.cpp, tracker-ff). NOT firmware.
//   c++ -std=c++20 -O1 -Wall -I core/source synth-open/tools/chord_match_test.cpp core/source/mtChord.cpp -o chord_match_test
// Checks: every one of the 77 entries matches itself exactly from every bass note and in any note order, the inversions of the
// common chords land on their entries, the approximate matches (missing fifth, one dropped tension) carry their cost, the
// clusters are rejected, the coverage over all 2048 pitch class sets is the one of the chord capture design, the SDK tables
// (synth-open/plug/plug_chord.h) are consistent with each other, and the names come out as the UI shows them.
#include <cstdint>

#include "mtChord.h"
#include "mtChordDefs.h"

#include "../plug/plug_chord.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
int g_fail = 0, g_checks = 0;

void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("FAIL %s\n", what);
    }
}

ChordMatch matchOf(std::vector<int> iv, int bass, bool reverse = false)
{
    uint8_t notes[16];
    int n = 0;
    for (int v : iv)
    {
        notes[n++] = uint8_t(bass + v);
    }
    if (reverse)
    {
        std::reverse(notes, notes + n);
    }
    return chordMatch(notes, uint8_t(n));
}

std::string nameOf(uint8_t idx, int bass, bool approx = false)
{
    char b[32];
    chordName(b, sizeof(b), idx, bass, approx);
    return b;
}
} // namespace

int main()
{
    char msg[160];

    // 1. every entry matches itself, exact, from all 12 bass pitch classes in 3 octaves, notes in table order and reversed
    int selfOk = 0;
    for (int i = 0; i <= MAX_CHORD; i++)
    {
        std::vector<int> iv;
        for (int b = 0; b < CHORD_FOR_MAX; b++)
        {
            if (chordDef[i] >> b & 1)
            {
                iv.push_back(b);
            }
        }
        bool all = true;
        for (int bass = 24; bass < 24 + 36; bass++)
        {
            for (int rev = 0; rev < 2; rev++)
            {
                const ChordMatch m = matchOf(iv, bass, rev);
                all = all && m.found && m.idx == i && m.cost == 0 && m.bass == bass;
            }
        }
        selfOk += all;
        snprintf(msg, sizeof(msg), "entry %d matches itself", i);
        check(all, msg);
    }

    // 2. inversions of the common chords (the step note is the lowest note, the entry carries the inversion)
    struct Inv
    {
        const char* name;
        std::vector<int> iv;
        int idx;
    };
    const std::vector<Inv> inv = {
        {"maj", {0, 4, 7}, 5},           {"maj 1st", {0, 3, 8}, 30},     {"maj 2nd", {0, 5, 9}, 31},
        {"min", {0, 3, 7}, 4},           {"min 1st", {0, 4, 9}, 32},     {"min 2nd", {0, 5, 8}, 33},
        {"dim 1st", {0, 3, 9}, 41},      {"dim 2nd", {0, 6, 9}, 42},     {"maj7", {0, 4, 7, 11}, 24},
        {"maj7 1st", {0, 3, 7, 8}, 18},  {"maj7 2nd", {0, 4, 5, 9}, 34}, {"maj7 3rd", {0, 1, 5, 8}, 35},
        {"7", {0, 4, 7, 10}, 23},        {"7 1st", {0, 3, 6, 8}, 38},    {"7 2nd", {0, 3, 5, 9}, 39},
        {"7 3rd", {0, 2, 6, 9}, 40},     {"m7", {0, 3, 7, 10}, 20},      {"m7 2nd", {0, 3, 5, 8}, 36},
        {"m7 3rd", {0, 2, 5, 9}, 37},    {"m7b5 2nd", {0, 4, 6, 9}, 47}, {"m7b5 3rd", {0, 2, 5, 8}, 48},
        {"open maj C G E'", {0, 7, 16}, 5}, {"maj + octave", {0, 4, 7, 12}, 5}, {"power C G", {0, 7, 12}, 11},
    };
    for (const Inv& t : inv)
    {
        const ChordMatch m = matchOf(t.iv, 48, true);
        snprintf(msg, sizeof(msg), "%s -> %d (got %d cost %d found %d)", t.name, t.idx, m.idx, m.cost, m.found);
        check(m.found && m.idx == t.idx && m.cost == 0, msg);
    }

    // 3. approximate: the intended chord with its cost
    struct Apx
    {
        const char* name;
        std::vector<int> iv;
        int idx, cost;
    };
    const std::vector<Apx> apx = {
        {"maj no5", {0, 4}, 5, 1},           {"min no5", {0, 3}, 4, 1},          {"7 no5", {0, 4, 10}, 23, 1},
        {"maj7 no5", {0, 4, 11}, 24, 1},     {"m7 no5", {0, 3, 10}, 20, 1},      {"add9", {0, 2, 4, 7}, 5, 3},
        {"madd9", {0, 2, 3, 7}, 4, 3},       {"maj9", {0, 4, 7, 11, 14}, 24, 3}, {"dom9", {0, 4, 7, 10, 14}, 23, 3},
        {"min9", {0, 3, 7, 10, 14}, 20, 3},  {"whole tone 5 notes", {0, 2, 4, 6, 8}, 51, 3},   // 7#5 inv1 with the 9th dropped
    };
    for (const Apx& t : apx)
    {
        const ChordMatch m = matchOf(t.iv, 50, true);
        snprintf(msg, sizeof(msg), "%s -> %d cost %d (got %d cost %d found %d)", t.name, t.idx, t.cost, m.idx, m.cost, m.found);
        check(m.found && m.idx == t.idx && m.cost == t.cost, msg);
    }

    // 4. rejected: clusters, 6 note chords
    const std::vector<std::vector<int>> rej = {
        {0, 1, 2}, {0, 1, 2, 3}, {0, 1, 2, 3, 4}, {0, 1, 3, 4}, {0, 4, 7, 10, 14, 17}, {0, 4, 7, 10, 14, 21}};
    for (const auto& iv : rej)
    {
        const ChordMatch m = matchOf(iv, 45);
        snprintf(msg, sizeof(msg), "reject %d notes from %d (got found %d idx %d cost %d)", int(iv.size()), iv[1], m.found, m.idx, m.cost);
        check(!m.found, msg);
    }
    check(!chordMatch(nullptr, 0).found, "no notes, no chord");

    // 5. coverage over all 2048 pitch class sets with a bass (design figures: exact 67, approximate 320 at cost 4. 7b5, entry 76, is the one appended entry inside one octave: its set moved from approximate to exact, so 68 / 319)
    int ex = 0, ap = 0, rj = 0;
    for (int set = 0; set < 2048; set++)
    {
        const uint16_t m = uint16_t(1 | (set << 1));
        uint8_t notes[12];
        uint8_t n = 0;
        for (int b = 0; b < 12; b++)
        {
            if (m >> b & 1)
            {
                notes[n++] = uint8_t(36 + b);
            }
        }
        const ChordMatch r = chordMatch(notes, n);
        if (r.found && r.cost == 0)
            ex++;
        else if (r.found)
            ap++;
        else
            rj++;
    }
    snprintf(msg, sizeof(msg), "coverage exact %d approx %d reject %d (want 68 / 319 / 1661)", ex, ap, rj);
    check(ex == 68 && ap == 319 && rj == 1661, msg);

    // 6. the SDK tables: the root and the name of every entry describe its mask
    const std::map<std::string, std::vector<int>> q = {
        {"note", {0}},          {"maj", {0, 4, 7}},       {"m", {0, 3, 7}},         {"dim", {0, 3, 6}},       {"aug", {0, 4, 8}},
        {"sus2", {0, 2, 7}},    {"sus4", {0, 5, 7}},      {"sus#4", {0, 6, 7}},     {"sus2#5", {0, 2, 8}},    {"5", {0, 7}},
        {"5add9", {0, 7, 2}},   {"7", {0, 4, 7, 10}},     {"maj7", {0, 4, 7, 11}},  {"m7", {0, 3, 7, 10}},    {"dim7", {0, 3, 6, 9}},
        {"m7b5", {0, 3, 6, 10}}, {"mM7", {0, 3, 7, 11}},  {"7#5", {0, 4, 8, 10}},   {"M7#5", {0, 4, 8, 11}},  {"7sus4", {0, 5, 7, 10}},
        {"7sus2", {0, 2, 7, 10}}, {"M7sus2", {0, 2, 7, 11}}, {"M7sus4", {0, 5, 7, 11}}, {"6", {0, 4, 7, 9}}, {"m6", {0, 3, 7, 9}},
        {"6sus2", {0, 2, 7, 9}}, {"madd9", {0, 3, 7, 2}}, {"add9", {0, 4, 7, 2}},
        {"9", {0, 4, 10, 2}}, {"m9", {0, 3, 10, 2}}, {"maj9", {0, 4, 11, 2}}, {"6/9", {0, 4, 9, 2}}, {"7#9", {0, 4, 10, 3}}, {"7b9", {0, 4, 10, 1}},
        {"7b5", {0, 4, 6, 10}},
    };
    int tablesOk = 0;
    for (int i = 0; i < PLUG_CHORD_COUNT; i++)
    {
        const std::string nm = plugchord::kNames[i];
        const size_t sp = nm.find(' ');
        const std::string qual = nm.substr(0, sp);
        const int invN = sp == std::string::npos ? 0 : atoi(nm.c_str() + nm.find("inv") + 3);
        const auto it = q.find(qual);
        bool ok = it != q.end();
        if (ok)
        {
            uint16_t pcs = 0, want = 0;
            for (int b = 0; b < CHORD_FOR_MAX; b++)
                if (chordDef[i] >> b & 1) pcs |= uint16_t(1u << (b % 12));
            std::vector<int> sorted;
            for (int v : it->second)
            {
                want |= uint16_t(1u << ((v + plugchord::kRoot[i]) % 12));
                sorted.push_back(v % 12);
            }
            std::sort(sorted.begin(), sorted.end());
            const int bassInterval = (12 - plugchord::kRoot[i]) % 12;
            ok = pcs == want && invN < int(sorted.size()) && sorted[invN] == bassInterval && (invN > 0 || plugchord::kRoot[i] == 0);
        }
        tablesOk += ok;
        snprintf(msg, sizeof(msg), "plug_chord.h entry %d \"%s\" root %d describes its mask", i, nm.c_str(), plugchord::kRoot[i]);
        check(ok, msg);
    }

    // 7. names as the title bar and the popup show them
    const struct
    {
        int idx, bass;
        bool approx;
        const char* want;
    } nm[] = {
        {0, 60, false, "C"},          {5, 60, false, "C"},          {4, 60, false, "Cm"},        {24, 60, false, "Cmaj7"},
        {34, 60, false, "Fmaj7/C"},   {30, 64, false, "C/E"},       {32, 60, false, "Am/C"},     {40, 62, false, "E7/D"},
        {20, 61, true, "~C#m7"},      {11, 55, false, "G5"},        {9, 48, false, "F5/C"},      {8, 48, false, "Fsus4/C"},
        {24, -1, false, "maj7"},      {35, -1, false, "maj7 inv3"}, {0, -1, false, "note"},      {5, -1, true, "~maj"},
        {99, 60, false, "C"},
    };
    for (const auto& t : nm)
    {
        const std::string got = nameOf(uint8_t(t.idx), t.bass, t.approx);
        snprintf(msg, sizeof(msg), "name %d on %d = \"%s\" (got \"%s\")", t.idx, t.bass, t.want, got.c_str());
        check(got == t.want, msg);
    }
    char small[4];
    chordName(small, sizeof(small), 35, 60);
    check(strlen(small) == 3, "a short buffer is cut and terminated");

    // 8. the appended entries (68 minor add9, 69 major add9, 70..76 the 9th chords and 7b5) and the frozen order of the first 68
    {
        check(PLUG_CHORD_COUNT == 77 && MAX_CHORD == 76 && PLUG_CHORD_COUNT_V1 == 68 && PLUG_CHORD_COUNT_V2 == 70, "77 entries, the 68 and 70 entry table sizes kept for old engines");
        check(CHORD_FOR_MAX == 16 && PLUG_CHORD_SPAN == 16, "16 significant bits: 7#9 (entry 74) reaches the 15th semitone");
        const std::vector<int> madd9 = {0, 3, 7, 14}, add9 = {0, 4, 7, 14};
        for (int e = 68; e <= 69; e++)
        {
            const std::vector<int>& want = e == 68 ? madd9 : add9;
            uint16_t mask = 0;
            for (int v : want) mask = uint16_t(mask | (1u << v));
            snprintf(msg, sizeof(msg), "entry %d mask %04x is the intervals 0 %d 7 14", e, chordDef[e], want[1]);
            check(chordDef[e] == mask && plugchord::kMasks[e] == mask && plugchord::kRoot[e] == 0, msg);
            check(chordDef[e] >> 14 & 1, "the 9th is the widest note, inside CHORD_FOR_MAX (strum and play loops reach it)");
            // exact voicing (the 9th an octave up) from a bass
            const ChordMatch m = matchOf(want, 52, true);
            snprintf(msg, sizeof(msg), "voicing of entry %d matches exactly (got %d cost %d)", e, m.idx, m.cost);
            check(m.found && m.idx == e && m.cost == 0 && m.bass == 52, msg);
        }
        check(nameOf(68, 60) == "Cmadd9" && nameOf(69, 60) == "Cadd9" && nameOf(69, 64) == "Eadd9", "add9 names with a root");
        check(nameOf(68, -1) == "madd9" && nameOf(69, -1) == "add9" && nameOf(69, -1, true) == "~add9", "add9 generic names");
        check(strcmp(chordShortName[68], "mad") == 0 && strcmp(chordShortName[69], "ad9") == 0, "add9 grid names");
        check(strcmp(chordDefName[68], "37E") == 0 && strcmp(chordDefName[69], "47E") == 0, "add9 interval digits");
        // 70..76: intervals, names, grid names, digits, exact voicing (the 9th an octave up, 7b5 inside the octave)
        const struct
        {
            int e;
            std::vector<int> iv;
            const char* name;
            const char* grid;
            const char* digits;
        } nw[] = {
            {70, {0, 4, 10, 14}, "9", "do9", "4AE"},  {71, {0, 3, 10, 14}, "m9", "m9 ", "3AE"}, {72, {0, 4, 11, 14}, "maj9", "^9 ", "4BE"},
            {73, {0, 4, 9, 14}, "6/9", "6/9", "49E"}, {74, {0, 4, 10, 15}, "7#9", "7#9", "4AF"}, {75, {0, 4, 10, 13}, "7b9", "7b9", "4AD"},
            {76, {0, 4, 6, 10}, "7b5", "7b5", "46A"},
        };
        for (const auto& w : nw)
        {
            uint16_t mask = 0;
            for (int v : w.iv) mask = uint16_t(mask | (1u << v));
            snprintf(msg, sizeof(msg), "entry %d (%s) mask %04x is its intervals", w.e, w.name, chordDef[w.e]);
            check(chordDef[w.e] == mask && plugchord::kMasks[w.e] == mask && plugchord::kRoot[w.e] == 0, msg);
            check(strcmp(plugchord::kNames[w.e], w.name) == 0 && strchr(plugchord::kNames[w.e], ' ') == nullptr, "name, no space (a space would read as an inversion)");
            check(strcmp(chordShortName[w.e], w.grid) == 0 && strcmp(chordDefName[w.e], w.digits) == 0, "grid name and interval digits");
            check(nameOf(uint8_t(w.e), 60) == std::string("C") + w.name && nameOf(uint8_t(w.e), -1) == w.name, "name with and without a root");
            const ChordMatch m = matchOf(w.iv, 52, true);
            snprintf(msg, sizeof(msg), "voicing of entry %d matches exactly (got %d cost %d)", w.e, m.idx, m.cost);
            check(m.found && m.idx == w.e && m.cost == 0 && m.bass == 52, msg);
            // no earlier entry has the same interval set (nothing appended twice)
            bool dup = false;
            for (int i = 0; i < w.e; i++) dup = dup || chordDef[i] == mask;
            check(!dup, "the interval set is new");
            // four notes at most (the fifth is dropped so a chord fits the 4 voices)
            int nb = 0;
            for (int b = 0; b < 16; b++) nb += mask >> b & 1;
            check(nb == 4, "four notes");
        }
        // the exact voicing wins over a wider bass: a C below a close voiced 7b5 reads 7b5, the 7#9 spans 15 semitones
        check(matchOf({0, 4, 6, 10}, 60, true).idx == 76 && matchOf({0, 4, 10, 15}, 40, true).idx == 74, "7b5 and 7#9 from other basses");
        // the close voiced add9 (9th inside the octave) and every other pitch class set keep their old matcher result: the approximate
        // pass skips the new entries, so the coverage figures of section 5 stay and these two stay on maj / m
        const ChordMatch a = matchOf({0, 2, 4, 7}, 50, true), b = matchOf({0, 2, 3, 7}, 50, true);
        check(a.found && a.idx == 5 && a.cost == 3 && b.found && b.idx == 4 && b.cost == 3, "close voiced add9 still ~maj / ~m");
        // no approximate result ever names a new entry
        bool never = true;
        for (int set = 0; set < 2048; set++)
        {
            const uint16_t mm = uint16_t(1 | (set << 1));
            uint8_t notes[12];
            uint8_t n = 0;
            for (int bi = 0; bi < 12; bi++)
                if (mm >> bi & 1) notes[n++] = uint8_t(36 + bi);
            const ChordMatch r = chordMatch(notes, n);
            never = never && !(r.found && r.idx >= 68 && r.idx != 76);
            // the one octave entry 76 (7b5, 0 4 6 10) is reached exactly and from nothing else
            never = never && ((mm == 0x451) == (r.found && r.idx == 76 && r.cost == 0));
        }
        check(never, "a note set within one octave never matches an entry 68..75 (7b5, 76, is the one octave entry)");
        // the first 68 entries (masks, roots, names, short names, interval digits) are frozen: FNV-1a over them, taken from the 1.4.0 tables
        uint32_t h = 2166136261u;
        auto mix = [&h](uint32_t v) { h = (h ^ (v & 0xFF)) * 16777619u; };
        for (int i = 0; i < 68; i++)
        {
            mix(chordDef[i]);
            mix(chordDef[i] >> 8);
            mix(plugchord::kRoot[i]);
            for (const char* c = plugchord::kNames[i]; *c; c++) mix(uint8_t(*c));
            mix(0xFF);
            for (const char* c = chordDefName[i]; *c; c++) mix(uint8_t(*c));
            mix(0xFE);
            for (const char* c = chordShortName[i]; *c; c++) mix(uint8_t(*c));
            mix(0xFD);
        }
        snprintf(msg, sizeof(msg), "entries 0..67 unchanged (hash %08x, want 9c6f94b5)", h);
        check(h == 0x9c6f94b5u, msg);
    }

    printf("chord_match_test: %d checks, %d failed (self match %d of 77, tables %d of 77, coverage %d / %d / %d)\n", g_checks, g_fail,
           selfOk, tablesOk, ex, ap, rj);
    return g_fail ? 1 : 0;
}
