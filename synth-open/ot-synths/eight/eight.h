// Copied from opentracker-synths (https://github.com/opentracker-fw/opentracker-synths, modules/eight/src/eight.h, commit eb150a5f741b),
// Copyright (c) 2026 Marcin Spoczynski, MIT licence, see licenses/LICENSE-OpenTracker-synths-MIT.txt. Not modified, except this notice.
// 8-Type: a chip-style voice in the spirit of a 3-voice 8-bit home computer
// sound chip (OpenTracker 3.x module engine, id 0x86, module 8TY1,
// Patches/8TYPE/, "8-Type" on the Synth page). Our own design from general
// knowledge of such chips: three oscillators (triangle, saw, pulse with PWM,
// noise from a 23-bit shift register, the combined waves where the pulse
// gates the other wave, Tri+Saw), each with ring modulation (the sign of a
// triangle-carrying wave flips with the master's square; on Tri+Saw the whole
// mix) and hard sync
// from the oscillator before it (oscillator 1's is oscillator 3), routed
// through or around one 12 dB multimode filter per voice (any OR of low /
// band / high-pass; two cutoff curves; 16 resonance steps that never
// self-oscillate; Dirt), a DAC Grit on the oscillator mix, a 16-step ADSR,
// glide and the CHIP tricks (audio/chip_dsp.h). Not a copy of any chip.
//
// Engine contract (audio/synth_engine.h): Volume (index 46) is never applied
// here; the voice is mono (L = R).
#pragma once

#include "audio/synth_engine.h"

namespace ot {
namespace audio {
namespace eight {

constexpr int32_t kEngineId = 0x86;

// Per oscillator (k = 0..2): osc(k, field).
enum OscField : int { kWave = 0, kPw, kPwmDepth, kLevel, kSemi, kFine, kRing, kSync, kFilt, kClean, kOscStride };
constexpr int osc(int k, int field) { return kOscStride * k + field; }
// Parameter indices.
enum Param : int {
    kCutoff = 30, kResonance, kMode, kCurve, kDirt, kKeyTrack, kEnvAmt, kEnvDecay,           // FILTER
    kAttack, kDecay, kSustain, kRelease,                                                    // ENV
    kPwmRate, kGrit, kGlideMode, kGlideTime, kVolume,                                       // VOICE
    kArpMode, kArpRate, kArpNote2, kArpNote3, kVibRate, kVibDepth, kVibDelay, kSlideStart,  // CHIP
    kSlideTime,
    kParamCount,   // 56
};
enum Wave : int { kTri = 0, kSaw, kPulse, kNoise, kTriPulse, kSawPulse, kTriSaw, kOff, kWaveCount };
enum Mode : int { kLP = 0, kBP, kHP, kLPBP, kNotch, kBPHP, kAll };
enum Curve : int { kBright = 0, kDark };
enum GlideMode : int { kAlways = 0, kOverlap, kLegato, kLegatoOverlap };

// The ADSR tables: seconds at index 0..15 (attack: 0 to full; decay and
// release: to about 1 %).
float attackSeconds(int i);
float decaySeconds(int i);
// The cutoff a setting gives (Hz) on a Curve, before Key Track and the envelope.
float curveHz(float cutoff, int curve);
// The resonance step 0..15 as the SVF's k (2 x damping, 2.0 .. 0.1).
float resonanceK(int step);

ISynthEngine* engine();

// EIGHT_WORK_POOL (the card plugin, plug/synth/eight_plug.cpp): the two Links of the voice pass are not on the stack but host memory
// given once through setWork (PLUG_EXT_SYNTH work_bytes, SDRAM on the device) before any audio call. The DSP is the same code.
#ifdef EIGHT_WORK_POOL
uint32_t workBytes();
void setWork(void* work, uint32_t bytes);
#endif

}  // namespace eight
}  // namespace audio
}  // namespace ot
