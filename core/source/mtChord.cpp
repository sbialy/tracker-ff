// Chord capture and chord names (tracker-ff), see mtChord.h
#include "mtChord.h"

#include "mtChordDefs.h"

#include "../../synth-open/plug/plug_chord.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr bool sameTables()
{
    for (int i = 0; i < PLUG_CHORD_COUNT; i++)
    {
        if (chordDef[i] != plugchord::kMasks[i])
        {
            return false;
        }
    }
    return sizeof(chordDef) / sizeof(chordDef[0]) == PLUG_CHORD_COUNT;
}
static_assert(sameTables(), "mtChordDefs.h chordDef and plug_chord.h (the SDK contract) must hold the same 68 masks in the same order");
static_assert(MAX_CHORD + 1 == PLUG_CHORD_COUNT, "MAX_CHORD");

uint8_t pop16(uint16_t v)
{
    uint8_t c = 0;
    while (v)
    {
        v &= uint16_t(v - 1);
        c++;
    }
    return c;
}

inline uint16_t fold12(uint16_t m) { return uint16_t((m & 0x0FFF) | (m >> 12)); }

const char kNoteNames[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
} // namespace

bool chordShortNamesOn = true;

// every short name is 3 characters (the grid cell has room for 3)
static_assert([] {
    for (const auto& n : chordShortName)
        if (n[0] == 0 || n[1] == 0 || n[2] == 0 || n[3] != 0) return false;
    return true;
}(), "chordShortName: 3 characters each");

ChordMatch chordMatch(const uint8_t* notes, uint8_t n, uint8_t maxCost)
{
    ChordMatch r = { 0, 0, 0, 0 };
    if (n == 0)
    {
        return r;
    }
    uint8_t lo = 255;
    for (uint8_t i = 0; i < n; i++)
    {
        lo = notes[i] < lo ? notes[i] : lo;
    }
    r.bass = lo;
    uint16_t m15 = 0; // exact bass relative mask, valid while the span is at most 14
    uint16_t m12 = 0; // pitch classes above the bass
    bool wide = false;
    for (uint8_t i = 0; i < n; i++)
    {
        const uint8_t d = uint8_t(notes[i] - lo);
        m12 |= uint16_t(1u << (d % 12));
        if (d >= CHORD_FOR_MAX)
        {
            wide = true;
        }
        else
        {
            m15 |= uint16_t(1u << d);
        }
    }
    // 1. the exact voicing is an entry
    if (!wide)
    {
        for (uint8_t i = 0; i <= MAX_CHORD; i++)
        {
            if (chordDef[i] == m15)
            {
                r.idx = i;
                r.found = 1;
                return r;
            }
        }
    }
    // 2. folded to one octave above the bass, the nearest entry by a weighted distance. Ties go to the lower index (root position
    // before the inversions), except that an exact pitch class match wins its tie.
    uint8_t best = 255, bi = 0;
    for (uint8_t i = 0; i <= MAX_CHORD; i++)
    {
        const uint16_t e12 = fold12(chordDef[i]);
        const uint16_t extra = m12 & uint16_t(~e12); // played, not in the entry (dropped)
        const uint16_t miss = e12 & uint16_t(~m12);  // in the entry, not played (added)
        if (pop16(miss) > 1)
        {
            continue; // never more than one invented note
        }
        uint8_t c = (miss & 0x80) ? 1 : uint8_t(3 * pop16(miss)); // a missing fifth is cheap
        for (uint8_t b = 1; b < 12; b++)
        {
            if (extra >> b & 1)
            {
                c = uint8_t(c + ((0x224 >> b & 1) ? 3 : 4)); // dropping a 9th, 11th or 13th is cheaper
            }
        }
        if (c < best || (c == best && e12 == m12 && fold12(chordDef[bi]) != m12))
        {
            best = c;
            bi = i;
        }
    }
    if (best <= maxCost)
    {
        r.idx = bi;
        r.cost = best;
        r.found = 1;
    }
    return r;
}

void chordName(char* buf, int len, uint8_t idx, int bass, bool approx)
{
    if (len <= 0)
    {
        return;
    }
    if (idx > MAX_CHORD)
    {
        idx = MIN_CHORD;
    }
    const char* const q = plugchord::kNames[idx];
    const char* const inv = strchr(q, ' '); // " invN": the bass is not the root
    const char* const tilde = approx ? "~" : "";
    if (bass < 0)
    {
        snprintf(buf, size_t(len), "%s%s", tilde, q);
        return;
    }
    const char* const bassName = kNoteNames[bass % 12];
    if (idx == 0)
    {
        snprintf(buf, size_t(len), "%s%s", tilde, bassName);
        return;
    }
    const char* const root = kNoteNames[(bass + plugchord::kRoot[idx]) % 12];
    const int ql = inv ? int(inv - q) : int(strlen(q));
    const bool major = ql == 3 && strncmp(q, "maj", 3) == 0; // the major triad is the root alone: "C", "F/C"
    snprintf(buf, size_t(len), "%s%s%.*s%s%s", tilde, root, major ? 0 : ql, q, inv ? "/" : "", inv ? bassName : "");
}
