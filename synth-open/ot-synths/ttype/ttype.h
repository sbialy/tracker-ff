// Copied from opentracker-synths (https://github.com/opentracker-fw/opentracker-synths, modules/ttype/src/ttype.h, commit eb150a5f741b),
// Copyright (c) 2026 Marcin Spoczynski, MIT licence, see licenses/LICENSE-OpenTracker-synths-MIT.txt. Not modified, except this notice.
// T-Type: a transistor-rhythm style drum synth (OpenTracker 3.x module engine,
// id 0x85, module TTY1, Patches/TTYPE/, "T-Type" on the Synth page;
// docs/PLAN_TECHNO.md §3-9). Our own design after the classic analog drum
// machines as their owner's manuals describe them: one patch is one kit of
// ten drum channels, every drum built from sines with pitch sweeps, filtered
// noise, struck resonators and a shared bank of six square oscillators (the
// hats and cymbals). Not a copy of any instrument.
//
// Notes (C4 = 60, compatible with PERC's kick / snare / hats): 60 kick, 61
// rim, 62 snare, 63 and 71 clap, 64..67 hat closed .. open (decay and level
// between Closed and Open), 68 cowbell, 69 crash, 70 ride; 48..51 / 52..55 /
// 56..59 the low / mid / high tom, a semitone up per note in each group.
// Notes from 72 up wrap into 60..71 (by octaves); notes below 48 play the low
// tom transposed. Chokes: the hats are one channel (a closed hat cuts an
// open one), crash and ride share the cymbal channel.
//
// Every drum has a Character: three named variants of its model (the kick's
// Punch / Boom / Knock, ...): 0 a punchy hybrid, 1 a deep analog one, 2 a
// small tight one.
//
// Kit model (PERC's): one voice per slot owns the kit; the kit lives in the
// slot scratch (SynthSlotScratch; slotStateBytes()), the voice keeps a token
// and the kit master. A new voice of the slot takes the kit over (its drums
// keep ringing). Velocity applies per hit through Accent: the hit's gain is
// (1 - Accent) + Accent x velocity, and its click / noise parts are a little
// darker when soft (x 0.6 + 0.4 gain). The framework's voice gain is the last
// hit's velocity; the engine divides it back out, so a Volume FX on an empty
// step scales the ringing kit. A stolen / cut voice (SynthVoice::fading)
// ramps the kit out over its last block and frees it. Volume is never
// applied (the descriptor has no Volume parameter). At most kRingingMax
// drums ring at once: a hit over the cap fades the quietest other one out
// over one block.
//
// Per-drum sends: through the per-slot send tap (setSendTap, as PERC): the
// kit output feeds the Delay / Reverb buses with each send's level-weighted
// mean over the ringing drums. Stereo: every drum has a fixed place (kick,
// snare and mid tom centred), spread by Width; Drive (global) saturates the
// kit, bypassed at 0.
#pragma once

#include <cstdint>

#include "audio/synth_engine.h"

namespace ot {
namespace audio {
namespace ttype {

constexpr int32_t kEngineId = 0x85;

// Drum channels.
enum Drum : int8_t {
    kNone = -1, kKick = 0, kSnare, kClap, kTomLo, kTomMid, kTomHi, kRim, kCowbell, kHat, kCymbal, kDrums,
};
// Sounds with their own sends (the cymbal channel plays crash or ride).
enum Sound : int { kSKick = 0, kSSnare, kSClap, kSTomLo, kSTomMid, kSTomHi, kSRim, kSBell, kSHat, kSCrash, kSRide, kSounds };

// Parameter indices.
enum Param : int {
    kKickLevel = 0, kKickTune, kKickDecay, kKickAttack, kKickSweep, kKickDrive, kKickChar,               // KICK 0-6
    kSnareLevel, kSnareTune, kSnareDecay, kSnareTone, kSnareSnappy, kSnareChar,                         // SNARE 7-12
    kClapLevel, kClapTone, kClapDecay, kClapTail, kClapChar,                                            // CLAP 13-17
    kTomLoLevel, kTomMidLevel, kTomHiLevel, kTomLoTune, kTomMidTune, kTomHiTune,                        // TOMS 18-28
    kTomLoDecay, kTomMidDecay, kTomHiDecay, kTomTone, kTomChar,
    kRimLevel, kRimTune, kRimChar, kBellLevel, kBellTune, kBellDecay, kBellChar,                        // PERC 29-35
    kHatClosed, kHatOpen, kHatTune, kHatClosedDecay, kHatOpenDecay, kHatTone, kHatChar,                 // HATS 36-42
    kCymLevel, kCymTune, kCymDecay, kCymTone, kCymChar,                                                 // CYMBAL 43-47
    kSends,                                                                                             // SENDS 48-69: + 2 x Sound: Delay, Reverb
    kAccent = kSends + 2 * kSounds, kDrive, kWidth,                                                     // GLOBAL 70-72
    kParamCount,   // 73
};
static_assert(kAccent == 70 && kParamCount == 73, "T-Type parameter layout");
constexpr int sendParam(int sound, bool reverb) { return kSends + 2 * sound + (reverb ? 1 : 0); }

// The drum a note plays (kNone: none). variant: the hat 0 (closed) .. 3
// (open), the cymbal 0 crash / 1 ride, else 0. semis: the tom's offset from
// its Tune (semitones), else 0.
Drum drumForNote(uint8_t note, int* variant = nullptr, float* semis = nullptr);
// A drum's fixed stereo place, -1 (left) .. 1 (right), before Width.
float placeOf(Drum d, int variant = 0);

ISynthEngine* engine();

// Per-slot send tap (see above): mono delay / reverb buses of kBlock floats.
void setSendTap(int slot, float* delay, float* reverb, float gainD = 1.0f, float gainR = 1.0f);

constexpr int kRingingMax = 8;   // drums ringing at once
// The cost governor (as PERC's): the estimated M4 instructions per block of
// the slot's kit (steady drum costs, the output stage, Drive, the send tap,
// the square bank) stays within kKitBudget less 1k of headroom for the hit
// block: a hit over it fades the quietest other drums out.
constexpr int32_t kKitBudget = 30000;
int32_t kitCost(const SynthSlotScratch& slot, const SynthParams& p);
// Slot state size (tests / docs); the drums ringing in a slot's kit.
int slotStateBytes();
int ringing(const SynthSlotScratch& slot);
bool ringingDrum(const SynthSlotScratch& slot, Drum d);

// TTYPE_WORK_POOL (the card plugin, plug/synth/ttype_plug.cpp): the square bank block of render is not on the stack but host memory
// given once through setWork (PLUG_EXT_SYNTH work_bytes, SDRAM on the device) before any audio call. The DSP is the same code.
#ifdef TTYPE_WORK_POOL
uint32_t workBytes();
void setWork(void* work, uint32_t bytes);
#endif

}  // namespace ttype
}  // namespace audio
}  // namespace ot
