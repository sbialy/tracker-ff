// Tracker FF strum velocity ramp: the one place of the numbers. MIT licence, see synth-open/plug/README.md.
//
// A strummed chord (the Strum step FX next to a MIDI Chord) is not played at one level. A real strum loses a little energy per
// string in time order, a fast one stays nearly even, and the upstroke is lighter than the downstroke. All of it is a function of two
// numbers every chord path already has: the position of a note in the strum ORDER (0 = the note that starts first) and the strum
// size in units (1 unit = 2 ms between two notes). Nothing else is needed, so there is no velocity field in the ABI.
//
//   fall      the note of time position `order` plays at  100 - kFallPct * order * min(units, kFullUnits) / kFullUnits  percent of the
//             first note, never below kFloorPct. Units 1..4 are nearly flat, kFullUnits and up have the full fall.
//   stroke    the strum direction: strum > 0 starts at the LOWEST note (a guitar downstroke, the accent), strum < 0 at the highest
//             (a guitar upstroke): the upstroke plays the whole chord at kUpStrokePct percent.
//
// Who applies what:
//   host (mtSequencer.cpp, sendMidiChordOn): velocity(v, order, strum, depth) on every note of a strummed chord, Poly synth parts and
//     MIDI out. A Para chord (one note-on) takes only the stroke part (order 0).
//   Para engines (ACD, JU6, ALCO): gain(order, units, depth) per chord note, computed in the note-on latch from the strum order
//     they already have. It is relative to the first note, so it is <= 1. Strum 0 or depth 0 never calls it (the render is bit identical).
//
// Depth: the whole ramp scales with a per project setting `pct`, 0..100 (the "Strum ramp" row of the File screen, project.mtff key
// strum.ramp). 100 is the numbers below, 0 is no ramp at all (every function returns the plain value, bit for bit the old
// behaviour), in between the fall per note and the upstroke drop are that percent of theirs. A host gives the depth to a Para engine
// through the PLUG_EXT_STRUM host query (plug_abi.h): ask once at init, read ramp_pct at every note-on, no answer (a host before
// this query) = apply the 100 % forms below (gain(order, units)), which is what such a host's sequencer does too.
//
// STRUM_RAMP=0 (a build define, for A/B renders): every function returns the plain value, the old behaviour.
// Header only, plain C++ (constexpr integer maths), no includes beyond <stdint.h>.
#pragma once

#include <stdint.h>

#ifndef STRUM_RAMP
#define STRUM_RAMP 1
#endif

namespace strumramp
{
inline constexpr int kFallPct = 7;       // percent of the first note lost per note in time order at a full size strum
inline constexpr int kFullUnits = 16;    // strum units from where the fall is the full kFallPct (16 units = 32 ms between notes)
inline constexpr int kFloorPct = 50;     // no note falls below this percent of the first note
inline constexpr int kUpStrokePct = 88;  // the whole chord of an upstroke (strum < 0, highest note first), percent

// Percent (50..100) of the first note's level for the note at time position `order` (0 = first) of a strum of `units` (>= 1), at
// depth `pct` (0..100, a larger value counts as 100).
inline constexpr int fallPct(int order, int units, int pct)
{
#if STRUM_RAMP
    // unsigned: no signed division on the Cortex-M4. Order 0, units 0 and pct 0 give 100. At pct 100 this is the earlier
    // kFallPct * order * u / kFullUnits exactly (x * 100 / (16 * 100) == x / 16 for whole x)
    const unsigned u = unsigned(units > kFullUnits ? kFullUnits : units);
    const unsigned d = pct <= 0 ? 0u : (pct > 100 ? 100u : unsigned(pct));
    const int p = 100 - int(unsigned(kFallPct) * unsigned(order) * u * d / unsigned(kFullUnits * 100));
    return p < kFloorPct ? kFloorPct : p;
#else
    (void)order;
    (void)units;
    (void)pct;
    return 100;
#endif
}
inline constexpr int fallPct(int order, int units)   // depth 100
{
    return fallPct(order, units, 100);
}

// Percent for the whole chord by direction (strum is the signed units: > 0 lowest note first, < 0 highest first), at depth `pct`:
// the drop of the upstroke (100 - kUpStrokePct) scales with it.
inline constexpr int strokePct(int strum, int pct)
{
#if STRUM_RAMP
    const int d = pct <= 0 ? 0 : (pct > 100 ? 100 : pct);
    return strum < 0 ? 100 - (100 - kUpStrokePct) * d / 100 : 100;
#else
    (void)strum;
    (void)pct;
    return 100;
#endif
}
inline constexpr int strokePct(int strum)   // depth 100
{
    return strokePct(strum, 100);
}

// Host: the MIDI velocity of the note at time position `order` of a strum (signed units, != 0) at depth `pct`. A velocity of 0 stays
// 0 (that is a note off to the synth, the step asked for it), anything else stays >= 1. Depth 0 returns v untouched.
inline constexpr int velocity(int v, int order, int strum, int pct)
{
#if STRUM_RAMP
    if (v <= 0 || pct <= 0) return v;
    const int units = strum < 0 ? -strum : strum;
    int r = v * fallPct(order, units, pct) / 100;
    r = r * strokePct(strum, pct) / 100;
    return r < 1 ? 1 : r;
#else
    (void)order;
    (void)strum;
    (void)pct;
    return v;
#endif
}
inline constexpr int velocity(int v, int order, int strum)   // depth 100
{
    return velocity(v, order, strum, 100);
}

// Engines: the factor for the chord note at time position `order` of a strum of `units` at depth `pct`. Exactly 1.0f for a plain
// value, so a multiplication by it changes no bit.
inline constexpr float gain(int order, int units, int pct)
{
    const int p = fallPct(order, units, pct);
    return p >= 100 ? 1.0f : float(p) * 0.01f;
}
inline constexpr float gain(int order, int units)   // depth 100
{
    return gain(order, units, 100);
}
}  // namespace strumramp
