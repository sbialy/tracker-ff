// Hand roll of a recognised chord (tracker-ff): record join (mtSequencer_chord.cpp) and Capture (mtSequencer_capture.cpp) write the roll
// of the played notes as a Strum FX beside the MIDI Chord FX. Header only, no firmware types: synth-open/tools/strum_roll_test.cpp tests it.
#pragma once
#include <stdint.h>

namespace handroll
{
// A mean gap under this is no roll: the USB MIDI path spreads notes pressed together by about 13 ms (device test J1)
constexpr uint32_t kDeadUs = 15000;
// The most a Strum FX holds (values 1..63 up, 65..127 down)
constexpr uint32_t kMaxUnits = 63;

// The Strum FX value of n notes in arrival order, spanUs from the first to the last (0: none). units = mean gap / unitUs rounded, at most
// kMaxUnits. Up (value = units) when the first note is not above the last, down (value = 64 + units) when it is
__attribute__((noinline)) constexpr uint8_t strumValue(uint8_t firstNote, uint8_t lastNote, uint8_t n, uint32_t spanUs, uint32_t unitUs)
{
    if (n < 2)
    {
        return 0;
    }
    const uint32_t gapUs = spanUs / uint32_t(n - 1);
    if (gapUs < kDeadUs)
    {
        return 0;
    }
    uint32_t units = (gapUs + unitUs / 2) / unitUs;
    units = units > kMaxUnits ? kMaxUnits : units;
    return uint8_t(firstNote > lastNote ? 64u + units : units);
}

// Writes the roll into the other slot of the pair of the chord at chordSlot (0 or 1, -1 none): an empty slot takes it, a Strum that the
// same group wrote (own) is updated, or cleared when the roll is gone (value 0). Any other FX stays, never overwritten.
// Returns true when the slot holds the group's Strum
template <class Fx>
__attribute__((noinline)) bool strumWrite(Fx (&fx)[2], int8_t chordSlot, uint8_t value, bool own, uint8_t strumType)
{
    if (chordSlot < 0 || chordSlot > 1)
    {
        return false;
    }
    Fx& f = fx[chordSlot ^ 1];
    if (own && f.type == strumType)
    {
        f.value = value;
        if (!value)
        {
            f.type = 0;
        }
        return value != 0;
    }
    if (f.type == 0 && value)
    {
        f.type = strumType;
        f.value = value;
        return true;
    }
    return false;
}
} // namespace handroll
