// Host test of the chord matcher and the chord names (core/source/mtChord.cpp, tracker-ff). NOT firmware.
//   c++ -std=c++20 -O1 -Wall -I core/source synth-open/tools/chord_match_test.cpp core/source/mtChord.cpp -o chord_match_test
// Checks: every one of the 68 entries matches itself exactly from every bass note and in any note order, the inversions of the
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

    // 5. coverage over all 2048 pitch class sets with a bass (design figures: exact 67, approximate 320 at cost 4)
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
    snprintf(msg, sizeof(msg), "coverage exact %d approx %d reject %d (want 67 / 320 / 1661)", ex, ap, rj);
    check(ex == 67 && ap == 320 && rj == 1661, msg);

    // 6. the SDK tables: the root and the name of every entry describe its mask
    const std::map<std::string, std::vector<int>> q = {
        {"note", {0}},          {"maj", {0, 4, 7}},       {"m", {0, 3, 7}},         {"dim", {0, 3, 6}},       {"aug", {0, 4, 8}},
        {"sus2", {0, 2, 7}},    {"sus4", {0, 5, 7}},      {"sus#4", {0, 6, 7}},     {"sus2#5", {0, 2, 8}},    {"5", {0, 7}},
        {"5add9", {0, 7, 2}},   {"7", {0, 4, 7, 10}},     {"maj7", {0, 4, 7, 11}},  {"m7", {0, 3, 7, 10}},    {"dim7", {0, 3, 6, 9}},
        {"m7b5", {0, 3, 6, 10}}, {"mM7", {0, 3, 7, 11}},  {"7#5", {0, 4, 8, 10}},   {"M7#5", {0, 4, 8, 11}},  {"7sus4", {0, 5, 7, 10}},
        {"7sus2", {0, 2, 7, 10}}, {"M7sus2", {0, 2, 7, 11}}, {"M7sus4", {0, 5, 7, 11}}, {"6", {0, 4, 7, 9}}, {"m6", {0, 3, 7, 9}},
        {"6sus2", {0, 2, 7, 9}},
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

    printf("chord_match_test: %d checks, %d failed (self match %d of 68, tables %d of 68, coverage %d / %d / %d)\n", g_checks, g_fail,
           selfOk, tablesOk, ex, ap, rj);
    return g_fail ? 1 : 0;
}
