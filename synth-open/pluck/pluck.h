// PLK ("Pluck"): clean-room Karplus-Strong plucked string engine for the open PolySynth shim (MK66, Cortex-M4F).
//
// Own implementation of the textbook algorithm: Karplus and Strong, "Digital Synthesis of Plucked-String and Drum Timbres"
// (CMJ 1983), with the extensions of Jaffe and Smith, "Extensions of the Karplus-Strong Plucked-String Algorithm" (CMJ 1983):
// a delay line that is excited once and recirculates through a loop filter, a first order allpass for fractional tuning,
// a one pole lowpass with a decay compensating gain, a pick position comb on the exciter and an allpass dispersion
// cascade (Stretch). No third-party code. See synth-open/pluck/README.md.
//
// Engine contract: audio/synth_engine.h. Volume (index 8) is never applied here, the output is mono (L = R).
#pragma once

#include "audio/synth_engine.h"

namespace ot {
namespace audio {
namespace pluck {

constexpr int32_t kEngineId = 0x8A;   // no Polyend id: ours, after OpenTracker's Arc-Type (0x89)

enum Param : int {
    kExciter = 0,   // Noise, Click, Saw, Mallet
    kBrightness,    // exciter colour and loop filter cutoff
    kDamping,       // 0 = rings for 15 s, 1 = 0.1 s (time to -60 dB of the fundamental, the same on every key)
    kPickPos,       // 0 = at the bridge (no comb), 1 = in the middle of the string
    kStretch,       // allpass dispersion: partials drift sharp, bell like
    kRelease,       // note off: time to -60 dB, the string is damped, not cut
    kTune,          // semitones
    kFine,          // cents
    kVolume,        // framework gain
    kParamCount,
};

enum Exciter : int { kNoise = 0, kClick, kSaw, kMallet, kExciterCount };

ISynthEngine* engine();

// PLUCK_WORK_POOL (the card plugin, plug/synth/pluck_plug.cpp): the delay line pool is not in .bss but host memory given once
// through setWork (PLUG_EXT_SYNTH work_bytes, SDRAM on the device) before any audio call. The DSP is the same code.
#ifdef PLUCK_WORK_POOL
uint32_t workBytes();
void setWork(void* work, uint32_t bytes);
#endif

}  // namespace pluck
}  // namespace audio
}  // namespace ot
