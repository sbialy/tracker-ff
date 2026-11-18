#pragma once

// MIDI remote (Config > Features > MIDI remote, FEATURE_EXT_HOOKS builds): a controller on one MIDI channel plays the Tracker's own keys.
// Notes base .. base + 32 are the 33 keys in the order of the key ids (F1 to F8, Params, Performance, File, Config, Master, Sample Load,
// Sample Play, Sample Edit, Sample Rec, Song, Note, Instr, Vol, Fx, Pattern, Play, Copy, Insert, Up, Delete, Rec, Shift, Left, Down, Right),
// notes base + 33 .. base + 80 the 48 pads. CC 96 turns the encoder one step up, CC 97 one step down (value 64 or more). A note on is a key
// press, a note off its release, and the key counts as held for the Shift combos. Notes of that channel below the base note, other channels
// and every other message work as before, the rest of the channel is consumed. Off by default.

#include <stdint.h>

namespace midi_remote
{
constexpr uint8_t kKeys = 33;
constexpr uint8_t kPads = 48;
static_assert(kKeys <= 64 && kPads <= 64, "the held masks of a switch group are 64 bit");
constexpr uint8_t kCcUp = 96;
constexpr uint8_t kCcDown = 97;

enum Source : uint8_t
{
    kSourceUsb = 0,
    kSourceDin = 1,
    kSources = 2,
};

void releaseAll();                   // every key and pad the remote holds goes up
void releaseSource(Source source);   // what one source holds goes up (the USB cable was pulled, no note off comes)
void set(bool on, uint8_t channel, uint8_t baseNote);   // channel 1..16
bool note(uint8_t channel, uint8_t pitch, uint8_t velocity, bool down, Source source = kSourceUsb);   // true: consumed
bool cc(uint8_t channel, uint8_t number, uint8_t value);                  // true: consumed
}   // namespace midi_remote
