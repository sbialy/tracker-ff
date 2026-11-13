// Tracker FF chord table: the SDK contract of the 68 chord entries. MIT licence, see synth-open/plug/README.md.
//
// The stock "0 - MIDI Chord" step FX stores a value 0..67. Value i means the notes of PLUG_CHORD_MASKS[i] above the step note:
// bit k set = a note k semitones above it. Bit 0 is set in every entry, so the step note is the LOWEST note (the bass) and the
// inversion is part of the entry (30..67 are inversions). Entry 0 is the single note. The order is frozen: values 0..29 are
// upstream (Polyend), 30..67 were added by the community fork (fork picks 233da6a, 86fcc38). New entries may only be appended.
//
// A synth engine that plays chords itself (paraphony, PLUG_EXT_CHORD in plug_abi.h) has one enum parameter with exactly
// PLUG_CHORD_COUNT values in this order: the host writes a MIDI Chord step's value into it for that one note-on. The names
// below are the ones the firmware shows (the MIDI Chord title bar, the capture popup, the built-in ACD's Chord parameter).
// A name says the chord quality and, for an inversion, which chord tone is in the bass ("maj7 inv2"). With the bass note
// known the firmware writes the root in front and the bass after a slash: entry 34 on C is "Fmaj7/C" (root = bass +
// PLUG_CHORD_ROOT[i] semitones).
//
// Plain C, header only: the tables are macros, a translation unit that wants the data instantiates it
//   static const uint16_t masks[PLUG_CHORD_COUNT] = PLUG_CHORD_MASKS;
// so a plugin pays for what it uses only.
#pragma once

#define PLUG_CHORD_COUNT 68
#define PLUG_CHORD_SPAN 15   // significant bits of a mask: the widest entry spans 14 semitones

// clang-format off
#define PLUG_CHORD_MASKS { \
    0x0001, 0x0085, 0x0105, 0x0049, 0x0089, 0x0091, 0x0111, 0x00A1, 0x0421, 0x1021, \
    0x00C1, 0x1081, 0x4081, 0x0285, 0x0485, 0x0885, 0x0249, 0x0449, 0x0189, 0x0289, \
    0x0489, 0x0889, 0x0291, 0x0491, 0x0891, 0x0311, 0x0511, 0x0911, 0x04A1, 0x08A1, \
    0x0109, 0x0221, 0x0211, 0x0121, 0x0231, 0x0123, 0x0129, 0x0225, 0x0149, 0x0229, \
    0x0245, 0x0209, 0x0241, 0x0441, 0x0051, 0x0043, 0x0821, 0x0251, 0x0125, 0x0131, \
    0x0113, 0x0151, 0x0115, 0x0445, 0x0191, 0x0119, 0x0223, 0x00A5, 0x0429, 0x0521, \
    0x00A9, 0x0215, 0x0621, 0x00B1, 0x010B, 0x00C5, 0x0431, 0x0143 }

// Semitones from the bass (the step note) up to the chord root, per entry
#define PLUG_CHORD_ROOT { \
    0, 0, 0, 0, 0, 0, 0, 0, 5, 5, \
    0, 0, 0, 0, 0, 0, 0, 0, 8, 0, \
    0, 0, 0, 0, 0, 9, 0, 0, 0, 0, \
    8, 5, 9, 5, 5, 1, 5, 2, 8, 5, \
    2, 9, 6, 10, 4, 6, 5, 6, 2, 5, \
    1, 8, 4, 2, 8, 4, 1, 7, 5, 10, \
    5, 2, 10, 5, 1, 7, 5, 1 }

// Name per entry: the quality, then " invN" when the bass is not the root
#define PLUG_CHORD_NAMES { \
    "note", "sus2", "sus2#5", "dim", "m", "maj", "aug", "sus4", "sus4 inv2", "5 inv1", \
    "sus#4", "5", "5add9", "6sus2", "7sus2", "M7sus2", "dim7", "m7b5", "maj7 inv1", "m6", \
    "m7", "mM7", "6", "7", "maj7", "mM7 inv1", "7#5", "M7#5", "7sus4", "M7sus4", \
    "maj inv1", "maj inv2", "m inv1", "m inv2", "maj7 inv2", "maj7 inv3", "m7 inv2", "m7 inv3", "7 inv1", "7 inv2", \
    "7 inv3", "dim inv1", "dim inv2", "sus2#5 inv1", "sus2#5 inv2", "sus#4 inv1", "sus#4 inv2", "m7b5 inv2", "m7b5 inv3", "mM7 inv2", \
    "mM7 inv3", "7#5 inv1", "7#5 inv2", "7#5 inv3", "M7#5 inv1", "M7#5 inv2", "M7#5 inv3", "7sus4 inv1", "7sus4 inv2", "7sus2 inv1", \
    "7sus2 inv2", "7sus2 inv3", "M7sus2 inv1", "M7sus2 inv2", "M7sus2 inv3", "M7sus4 inv1", "M7sus4 inv2", "M7sus4 inv3" }
// clang-format on

#ifdef __cplusplus
// C++: the tables as inline variables, one copy per image however many files use them (none when nothing does)
namespace plugchord
{
inline constexpr unsigned short kMasks[PLUG_CHORD_COUNT] = PLUG_CHORD_MASKS;
inline constexpr unsigned char kRoot[PLUG_CHORD_COUNT] = PLUG_CHORD_ROOT;
inline constexpr const char* kNames[PLUG_CHORD_COUNT] = PLUG_CHORD_NAMES;
}   // namespace plugchord
#endif
