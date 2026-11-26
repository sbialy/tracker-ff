#ifndef _SCALES_H_
#define _SCALES_H_

#include <stdint.h>

const uint8_t MIN_SCALE = 0;
const uint8_t MAX_SCALE = 38;

uint8_t isInScale(uint8_t note, uint8_t root, uint8_t scale);
bool inProjectScale(uint8_t note, uint8_t root, uint8_t scale); // a note in the project scale (the root and scale of the project) as the random note FX reads it (degrees of N notes in a tuning)

#ifdef FEATURE_FF_CORE
// Scale snap at playback (per track, the project scale and root note of the Project Settings page, File screen F7). The pattern data is not touched:
// the sequencer moves the note it sends (mtSequencer.cpp snapStepNote). Mode 0 plays the note as written.
enum ScaleSnapMode : uint8_t
{
    kSnapOff,
    kSnapNearest, // the closest scale note, a tie goes down
    kSnapDown,    // the scale note at or below (above only when none is left below)
    kSnapUp,      // the scale note at or above (below only when none is left above)
    kSnapModeCount
};
constexpr uint8_t kScaleSnapTracks = 16; // kAllTracks of the original, plug_sidecar.h kSnapTracks
extern uint8_t scaleSnapMode[kScaleSnapTracks]; // per track, saved in the project sidecar when any is not Off

uint8_t snapToScale(uint8_t note, uint8_t root, uint8_t scale, uint8_t mode);
#endif

const uint16_t scaleDef[] = {
    0b111111111111, // 0
    0b010110101101, // 1 minor
    0b101010110101, // 2 major
    0b011010101101, // 3
    0b101011010101, // 4
    0b010111010101, // 5
    0b010101101011, // 6
    0b010110101011, // 7
    0b010110110011, // 8
    0b011010110101, // 9
    0b101010101101, // 10
    0b100110101101, // 11
    0b101110110101, // 12
    0b011010111101, // 13
    0b111010110101, // 14
    0b010011101001, // 15
    0b001010011101, // 16
    0b010010101001, // 17
    0b001010010101, // 18
    0b100111001101, // 19
    0b011011001101, // 20
    0b101011010011, // 21
    0b010110101011, // 22
    0b010101010101, // 23
    0b101101101101, // 24
    0b010101011011, // 25
    0b000110001101, // 26
    0b010010100011, // 27
    0b001010100101, // 28
    0b010001100011, // 29
    0b101101101101, // 30
    0b001010001101, // 31
    0b011011010101, // 32
    0b100110110011, // 33
    0b010110110001, // 34
    0b100111001101, // 35
    0b101010101011, // 36
    0b100110101011, // 37
    0b110101010011, // 38
};

inline const char* const scaleNames[MAX_SCALE + 1] = {
    "Chromatic", "Minor",     "Major",     "Dorian",    "Lyd Maj",   "Lyd Min",   "Locrian",
    "Phrygian ", "PhrygDom",  "Mixlydian", "Melo Min",  "Harm Min",  "BeBopMaj",  "BeBopDor",
    "BeBopMix",  "Blues Min", "Blues Maj", "Penta Min", "Penta Maj", "Hung Min",  "Ukrainian",
    "Marva",     "Todi",      "Wholetone", "Dim",       "SLocrian",  "Hirajoshi", "In Sen",
    "Yo",        "Iwato",     "WholeHalf", "Kumoi",     "Overtone",  "DoubHarm",  "Indian",
    "GypsyMin",  "NeapoMaj",  "NeapoMin",  "Enigmatic",
};

const char* const allNotes[128] = {
    "C0",  "C#0", "D0",  "D#0", "E0",   "F0",  "F#0",  "G0",  "G#0", "A0",   "A#0", "B0",  "C1",
    "C#1", "D1",  "D#1", "E1",  "F1",   "F#1", "G1",   "G#1", "A1",  "A#1",  "B1",  "C2",  "C#2",
    "D2",  "D#2", "E2",  "F2",  "F#2",  "G2",  "G#2",  "A2",  "A#2", "B2",   "C3",  "C#3", "D3",
    "D#3", "E3",  "F3",  "F#3", "G3",   "G#3", "A3",   "A#3", "B3",  "C4",   "C#4", "D4",  "D#4",
    "E4",  "F4",  "F#4", "G4",  "G#4",  "A4",  "A#4",  "B4",  "C5",  "C#5",  "D5",  "D#5", "E5",
    "F5",  "F#5", "G5",  "G#5", "A5",   "A#5", "B5",   "C6",  "C#6", "D6",   "D#6", "E6",  "F6",
    "F#6", "G6",  "G#6", "A6",  "A#6",  "B6",  "C7",   "C#7", "D7",  "D#7",  "E7",  "F7",  "F#7",
    "G7",  "G#7", "A7",  "A#7", "B7",   "C8",  "C#8",  "D8",  "D#8", "E8",   "F8",  "F#8", "G8",
    "G#8", "A8",  "A#8", "B8",  "C9",   "C#9", "D9",   "D#9", "E9",  "F9",   "F#9", "G9",  "G#9",
    "A9",  "A#9", "B9",  "C10", "C#10", "D10", "D#10", "E10", "F10", "F#10", "G10"
};

#endif
