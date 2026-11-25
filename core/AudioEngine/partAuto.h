// Part destinations of the synth automation: Pan, Gain, Reverb send and Delay send of a synth part (ff_config.h FEATURE_PART_AUTO). Once per
// audio block audioEngine::updateSynthAuto reads them into the kAuto source of the part's params, the stock setSynthParam path applies them.
// Original Tracker: the open synth stack's part slots (open_mod.h ModKind::kPart, polysynth::open::partAuto). Tracker+ and Mini: the
// partauto store below, the stock synth parts have no slots of their own (its writer, the Synth Automation page, is IMX port round 3).
#pragma once

#include "ff_config.h"

#ifdef FEATURE_PART_AUTO
#include <stdint.h>
#ifdef FEATURE_SYNTH_OPEN
#include "open_load.h"
#include "open_mod.h"
#else
#include "open_modrun.h"
#endif

namespace partauto
{
#ifdef FEATURE_SYNTH_OPEN
constexpr int kParts = polysynth::open::kModParts;
constexpr int kGain = polysynth::open::kPartGain;
// the mask of the destinations that have a value (bit n = destination n), the values in v[kParts]. Audio interrupt
inline uint8_t read(int part, float* v)
{
    return polysynth::open::partAuto(part, v);
}
// project load: the open stack's slots belong to the part's patches, which load anew
inline void clearAll()
{
}
// the open stack runs its slots in the engine: nothing per block here
inline void tick(int, float)
{
}
#else
constexpr int kParts = 4;   // 0 Pan, 1 Gain, 2 Reverb send, 3 Delay send (the open_mod.h ModPart order)
constexpr int kGain = 1;
constexpr int kStoreParts = 3;   // kSynthParts
struct Store
{
    uint8_t mask[kStoreParts];
    float v[kStoreParts][kParts];   // offsets in the param's own unit, added after the Performance source (SynthValuesSource::kAuto)
};
inline Store g_store = {};
inline uint8_t read(int part, float* v)
{
    if (part < 0 || part >= kStoreParts)
        return 0;
    for (int n = 0; n < kParts; n++)
        v[n] = g_store.v[part][n];
    return g_store.mask[part];
}
// main thread: destination n of a part gets value x (mask bit on), clear() takes it off. The audio interrupt reads a float and a byte, a
// half written pair costs one block of the old value
inline void set(int part, int n, float x)
{
    if (part < 0 || part >= kStoreParts || n < 0 || n >= kParts)
        return;
    g_store.v[part][n] = x;
    g_store.mask[part] = uint8_t(g_store.mask[part] | (1u << n));
}
inline void clear(int part, int n)
{
    if (part < 0 || part >= kStoreParts || n < 0 || n >= kParts)
        return;
    g_store.mask[part] = uint8_t(g_store.mask[part] & ~(1u << n));
}
// ---- the slots of the Synth Automation page (mtAudioEngine.cpp): the config in the SDRAM, the run state in internal RAM. A slot drives
// a part destination only (ModKind::kPart, dest 0..3): the stock synth parts take no engine param or macro from here
constexpr int kSlots = polysynth::open::kModSlots;
using Slot = polysynth::open::ModRun;
const Slot& slot(int part, int k);
void setSlot(int part, int k, const Slot& s);   // main thread, the copy runs with the audio interrupt off
void clearSlots();                              // every slot Off, the run states reset
// a note of the part went out to the stock synth (MidiAdapter): LFOs restart after silence, envelopes start, the last release gates off
void noteOn(int part, uint8_t note);
void noteOff(int part, uint8_t note);
// audio interrupt, once per block before read(): runs the part's slots and sets the destinations they drive, clears the ones they
// drove last block and no longer do. A destination no slot drives keeps what set() gave it
void tick(int part, float bpm);
// project load: no part keeps a destination or a slot of the old project
inline void clearAll()
{
    for (int p = 0; p < kStoreParts; p++)
        g_store.mask[p] = 0;
    clearSlots();
}
#endif
} // namespace partauto
#endif
