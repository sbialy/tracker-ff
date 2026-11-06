// DIRT: live master bus drive / bitcrush insert (FEATURE_FF_DIRT, a SYNTH_OPEN build option).
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the DIRT module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx, modules/dirt/src/dirt.cpp,
// commit 6e5a7da), MIT licence, text in licenses/LICENSE-OpenTracker-fx-MIT.txt. Same signal path as the original: a pre-gain 1 + 15 d^2 (d = Drive / 100)
// into one of four shapers (Soft: rational tanh, Hard: clip at +-1, Fold: triangle fold, Asym: biased tanh with a 10 Hz DC
// block), make-up gain 1 / (1 + 0.5 d), a mid-tread quantiser to Bits (rounds to the nearest of 2^Bits levels, one of them is 0), a sample and hold every Rate frames, a TPT one-pole
// low-pass Tone and a dry / wet Mix.
//
// Modified: int16 blocks of the Teensy audio library instead of float buses, the parameters are a fixed table of presets
// (the master screen's Saturation list picks one), the coefficients are computed when a preset is picked and the block
// loop has no libm call. Dry / wet ramps over one block when the preset changes (out through dry), the original ramps
// every parameter. A silent bus (NULL blocks) is left alone and the state is cleared, like the original does not call
// its process() for a silent bus.
//
// Placement: after the tracks and sends mixer (dry tracks, delay and reverb returns, synth parts, saturation applied),
// before the limiter. That is the master bus insert point of OpenTracker 3.x (audio/inserts.h).
#pragma once

#include <stdint.h>

namespace ffdirt
{

constexpr float kFs = 44117.64706f;   // AUDIO_SAMPLE_RATE_EXACT
constexpr int kBlock = 128;

enum Type : uint8_t { kSoft, kHard, kFold, kAsym };

// One entry of the Saturation list. Tone 100 = off, Bits 16 = none, Rate 1 = none (units of the original's parameters).
struct Preset
{
    uint8_t type;
    uint8_t drive;   // 0..100 %
    uint8_t bits;    // 4..16
    uint8_t rate;    // 1..32 frames
    uint8_t tone;    // 0..100 (200 Hz .. 20 kHz), 100 = off
    const char* name;
};

constexpr int kPresetCount = 5;
extern const Preset kPresets[kPresetCount];

// Block coefficients of a preset.
struct Coef
{
    float pre, post, bias, biasY, dcA, g;   // pre includes the 1/3 of the tanh shapers, bias is Asym's bias / 3, biasY its output at 0
    uint32_t mask, half;
    uint32_t quant, tone, rate;
    uint8_t type;
};
void makeCoef(Coef& c, const Preset& p);   // ff_dirt_ctl.cpp

struct State
{
    float hold[2], lp[2], dc[2];
    uint32_t cnt;
    void reset()
    {
        hold[0] = hold[1] = lp[0] = lp[1] = dc[0] = dc[1] = 0.0f;
        cnt = 0;
    }
};

// Processes l[] and r[] (kBlock int16 each) in place. The wet amount ramps linearly from wet0 to wet1 across the block
// (0: the input untouched, 1: the effect only, the make-up gain included).
void process(const Coef& c, State& st, int16_t* l, int16_t* r, float wet0, float wet1);

}   // namespace ffdirt

#ifdef FEATURE_FF_DIRT
#include "AudioStream.h"

class AudioEffectDirt : public AudioStream
{
  public:
    AudioEffectDirt() : AudioStream(2, inputQueueArray) { st_.reset(); }
    virtual void update(void);
    // 0: off, 1..kPresetCount: that preset of ffdirt::kPresets. Main loop, takes effect at the next block.
    void setMode(uint8_t mode) { target_ = mode > ffdirt::kPresetCount ? ffdirt::kPresetCount : mode; }
    uint8_t mode() const { return target_; }

  private:
    audio_block_t* inputQueueArray[2];
    volatile uint8_t target_ = 0;
    uint8_t cur_ = 0;   // the preset the state belongs to (0: none)
    float wet_ = 0.0f;
    ffdirt::Coef coef_;
    ffdirt::State st_;
};
#endif
