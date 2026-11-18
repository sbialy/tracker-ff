#pragma once

// Resample on REC for the Synth play page (ref/synth-ab-resample.md, option A, ref/synth-ab-rec.md): REC records the master output as the
// codec gets it (the same block the skip-back hook sees) as a mono take straight into the free tail of the sample memory, from the first
// byte after the last used instrument to the end of the pool. Nothing is copied: the take is where the new instrument's sample lives. The
// page (core/interface/synthPlay/fSynthPlay.cpp) lands it as an instrument in the first slot after the last used one.
//
// Threads: block() runs in the audio interrupt, everything else on the main thread. The main thread sets the capture up while it is idle and
// reads the take only after end(), so the interrupt never writes what the main thread reads. 0 B SDRAM of its own: the take is the pool tail.

#include <stdint.h>

#if defined(FEATURE_SYNTH_OPEN) && !defined(PLATFORM_IMX)
#define FEATURE_SYNTH_REC 1
#endif

namespace synthplayrec
{
constexpr uint32_t kBlockFrames = 128;      // kAudioBlockSamples
constexpr uint32_t kMinFrames = 44100;      // a take needs about 1 s of free pool to start
constexpr int32_t kSilence = 32;            // |sample| <= 32 is below -60 dBFS (32768 / 1000 = 32.8): the trailing silence a stop cuts off

enum State : uint8_t
{
    kIdle,        // no take
    kRecording,   // the interrupt writes blocks
    kFull,        // the pool end came before the stop: the interrupt wrote the last whole block and left the rest alone
};

void begin(int16_t* dst, uint32_t capacityFrames);          // main thread: start a take at dst, up to capacityFrames (cut down to whole blocks)
void block(const int16_t* left, const int16_t* right);       // audio interrupt: one block of kBlockFrames frames, (L + R) / 2
State state();
uint32_t frames();                                           // written so far
uint32_t capacity();                                         // the whole blocks the take may hold
uint32_t end();                                              // main thread: stop writing, return the frames written (state is idle again)
uint32_t trimSilence(const int16_t* take, uint32_t frames);  // frames without the tail of samples at or below kSilence (0 when all is silent)
}   // namespace synthplayrec
