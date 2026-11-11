#ifndef synth_part_clip_h_
#define synth_part_clip_h_

// The mono part path of PolySynth::update in one pass: the soft clip of polysynth::sdk::saturateSoftClip (the FEATURE_SYNTH_OPEN form, a
// multiply instead of the divide) followed by the conversion to int16, straight from the rendered float block into the output blocks.
// The host test synth-open/tools/kern_synth_part_test.cpp runs this very code against the passes it replaces.
//
// What the old code did for a mono part with a gain other than 0 dB (a 0 dB part reuses the send block):
//   copy the block to a second float buffer, clip the first with gain 0 dB and the copy with the part gain (two passes over 128 floats each),
//   convert both to int16 in two more passes. Now every sample is clipped and converted once per output block.
// The old second clamp of the clip output to [-1, 1] is dead: for |x| <= 1.5 the cubic x - x^3 / 6.72 peaks at 0.99779 (x = 1.4967), so the
// int16 conversion of the clamped and the unclamped value are the same, for every float (NaN passes both clamps and converts to 0 on the
// Cortex-M4, in both). The clip with gain 0 dB multiplied by dbToAmplitude(0) = 1.0f, which changes no float.

#include <algorithm>
#include <stdint.h>

namespace part_clip
{
static inline __attribute__((always_inline)) int16_t clipToInt16(float x)
{
    float sample = std::clamp(x, -1.5f, 1.5f);
    sample = sample - (sample * sample * sample) * (1.0f / 6.72f);
    return static_cast<int16_t>(sample * INT16_MAX);
}

// send: the block both send mixers get (clip with gain 0 dB), post: the part output (clip with the part gain). reuseUnmodifiedMono is a part
// at 0 dB: one block serves both. skipSendBlocks: the sends are silent, only post is built.
template<int kBlockSamples>
static inline __attribute__((always_inline)) void monoPart(const float* src,
                                                           float postGainAmp,
                                                           bool reuseUnmodifiedMono,
                                                           bool skipSendBlocks,
                                                           int16_t* send,
                                                           int16_t* post)
{
    if (reuseUnmodifiedMono)
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            const int16_t v = clipToInt16(src[i]);
            send[i] = v;
            post[i] = v;
        }
    }
    else if (skipSendBlocks)
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            post[i] = clipToInt16(src[i] * postGainAmp);
        }
    }
    else
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            send[i] = clipToInt16(src[i]);
            post[i] = clipToInt16(src[i] * postGainAmp);
        }
    }
}

// dbToAmplitude(gainDb) per part, computed (a powf) only when the gain of the part changed. The same value bit for bit as computing it
// every block. The first use of a part always computes (the NaN never compares equal), so does a NaN gain.
template<int kParts>
struct GainCache
{
    float db[kParts];
    float amp[kParts];

    template<typename F>
    inline float get(int part, float gainDb, F&& dbToAmplitude)
    {
        if (db[part] != gainDb)
        {
            db[part] = gainDb;
            amp[part] = dbToAmplitude(gainDb);
        }
        return amp[part];
    }
};
} // namespace part_clip

#endif
