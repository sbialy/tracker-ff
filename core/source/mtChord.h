#pragma once
// Chord capture and chord names (tracker-ff). The 68 entries are the MIDI Chord FX table (mtChordDefs.h chordDef, the SDK contract
// synth-open/plug/plug_chord.h): a mask of semitones above the step note, which is the lowest note (the bass).
#include <stdint.h>

// A held note set as a MIDI Chord value: bass = the lowest note (the step note), idx = the entry, cost 0 exact, else approximate
// (a missing fifth costs 1, one added note 3, a dropped 9th, 11th or 13th 3, any other dropped note 4). found 0: nothing within maxCost.
struct ChordMatch
{
    uint8_t bass;
    uint8_t idx;
    uint8_t cost;
    uint8_t found;
};
constexpr uint8_t kChordMaxCost = 4;   // user decision 2026-11-12: approximate matches up to cost 4, shown with "~"

ChordMatch chordMatch(const uint8_t* notes, uint8_t n, uint8_t maxCost = kChordMaxCost);

// The readable name of entry idx: with bass < 0 the generic one ("maj7 inv2"), else with the root and the bass ("Fmaj7/C", "C",
// "Am/C"). approx puts a "~" in front. Always NUL terminated.
void chordName(char* buf, int len, uint8_t idx, int bass, bool approx = false);

// Config > Features > Chord names (features_builtin.cpp, on by default): the grid cell of a MIDI Chord FX shows chordShortName
// ("maj", "^7", "m7"), off the stock interval digits chordDefName (" 47", "47B"). Builds without the Features page keep it on.
extern bool chordShortNamesOn;
