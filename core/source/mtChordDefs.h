#ifndef _MTCHORDSDEFS_H_
#define _MTCHORDSDEFS_H_

const uint8_t MIN_CHORD = 0;
const uint8_t MAX_CHORD = 67;
const uint8_t CHORD_FOR_MAX = 15; // number of significant bits in chordDef

// inline: one copy in the image, whatever number of files include this (mtChord.cpp checks it against plug_chord.h)
inline constexpr uint16_t chordDef[] = {
    0b0000000000000001, // 0
    0b0000000010000101, // 1
    0b0000000100000101, // 2
    0b0000000001001001, // 3
    0b0000000010001001, // 4
    0b0000000010010001, // 5
    0b0000000100010001, // 6
    0b0000000010100001, // 7
    0b0000010000100001, // 8
    0b0001000000100001, // 9
    0b0000000011000001, // 10
    0b0001000010000001, // 11
    0b0100000010000001, // 12
    0b0000001010000101, // 13
    0b0000010010000101, // 14
    0b0000100010000101, // 15
    0b0000001001001001, // 16
    0b0000010001001001, // 17
    0b0000000110001001, // 18
    0b0000001010001001, // 19
    0b0000010010001001, // 20
    0b0000100010001001, // 21
    0b0000001010010001, // 22
    0b0000010010010001, // 23
    0b0000100010010001, // 24
    0b0000001100010001, // 25
    0b0000010100010001, // 26
    0b0000100100010001, // 27
    0b0000010010100001, // 28
    0b0000100010100001, // 29
    0b0000000100001001, // 30 major, 1st inversion
    0b0000001000100001, // 31 major, 2nd inversion
    0b0000001000010001, // 32 minor, 1st inversion
    0b0000000100100001, // 33 minor, 2nd inversion
    0b0000001000110001, // 34 maj7, 2nd inversion
    0b0000000100100011, // 35 maj7, 3rd inversion
    0b0000000100101001, // 36 min7, 2nd inversion
    0b0000001000100101, // 37 min7, 3rd inversion
    0b0000000101001001, // 38 dom7, 1st inversion
    0b0000001000101001, // 39 dom7, 2nd inversion
    0b0000001001000101, // 40 dom7, 3rd inversion
    0b0000001000001001, // 41 diminished, 1st inversion
    0b0000001001000001, // 42 diminished, 2nd inversion
    0b0000010001000001, // 43 sus2#5, 1st inversion
    0b0000000001010001, // 44 sus2#5, 2nd inversion
    0b0000000001000011, // 45 sus#4, 1st inversion
    0b0000100000100001, // 46 sus#4, 2nd inversion
    0b0000001001010001, // 47 m7b5, 2nd inversion
    0b0000000100100101, // 48 m7b5, 3rd inversion
    0b0000000100110001, // 49 mMaj7, 2nd inversion
    0b0000000100010011, // 50 mMaj7, 3rd inversion
    0b0000000101010001, // 51 aug7, 1st inversion
    0b0000000100010101, // 52 aug7, 2nd inversion
    0b0000010001000101, // 53 aug7, 3rd inversion
    0b0000000110010001, // 54 augMaj7, 1st inversion
    0b0000000100011001, // 55 augMaj7, 2nd inversion
    0b0000001000100011, // 56 augMaj7, 3rd inversion
    0b0000000010100101, // 57 7sus4, 1st inversion
    0b0000010000101001, // 58 7sus4, 2nd inversion
    0b0000010100100001, // 59 sus2b7, 1st inversion
    0b0000000010101001, // 60 sus2b7, 2nd inversion
    0b0000001000010101, // 61 sus2b7, 3rd inversion
    0b0000011000100001, // 62 sus2maj7, 1st inversion
    0b0000000010110001, // 63 sus2maj7, 2nd inversion
    0b0000000100001011, // 64 sus2maj7, 3rd inversion
    0b0000000011000101, // 65 sus4maj7, 1st inversion
    0b0000010000110001, // 66 sus4maj7, 2nd inversion
    0b0000000101000011, // 67 sus4maj7, 3rd inversion
};
inline constexpr char chordDefName[MAX_CHORD + 1][4] = {
    "---", " 27", " 28", " 36", " 37", " 47", " 48", " 57", " 5A", " 5C", " 67", " 7C",
    " 7E", "279", "27A", "27B", "369", "36A", "378", "379", "37A", "37B", "479", "47A",
    "47B", "489", "48A", "48B", "57A", "57B", " 38", " 59", " 49", " 58", "459", "158",
    "358", "259", "368", "359", "269", " 39", " 69", " 6A", " 46", " 16", " 5B", "469",
    "258", "458", "148", "468", "248", "26A", "478", "348", "159", "257", "35A", "58A",
    "357", "249", "59A", "457", "138", "267", "45A", "168",
};

// Short chord names for the 3 character grid cell (with the 2026-11-12 decisions: sus2 with a capital S,
// the maj7 family as ^7, the 11 sus 7th inversions 57..67 keep the interval digits). Config > Features > Chord names picks this
// table (on, the default) or chordDefName (off). Every entry is 3 characters, only entry 0 has a '-' (the dim empty cell look).
inline constexpr char chordShortName[MAX_CHORD + 1][4] = {
    "---", "Su2", "Su+", "dim", "min", "maj", "aug", "su4", "s42", "po1", "su#", "pow",
    "po9", "6S2", "7S2", "MS2", "di7", "m7b", "^71", "mi6", "m7 ", "mM7", "ma6", "do7",
    "^7 ", "mM1", "a7 ", "aM7", "7s4", "Ms4", "ma1", "ma2", "mi1", "mi2", "^72", "^73",
    "m72", "m73", "do1", "do2", "do3", "di1", "di2", "S+1", "S+2", "s#1", "s#2", "mb2",
    "mb3", "mM2", "mM3", "a71", "a72", "a73", "aM1", "aM2", "aM3", "257", "35A", "58A",
    "357", "249", "59A", "457", "138", "267", "45A", "168",
};

#endif
