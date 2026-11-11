// The mono part path of PolySynth::update compiled for the Cortex-M4 with the firmware's compiler and flags, the old passes next to the new
// kernel (core/libs/Audio/synth_part_clip.h), run in the emulator by run_synth_part_arm.sh. Same inputs, the int16 blocks must be equal.
// The point of this build is the compiler: the soft clip is a fused multiply-add the GCC default contracts, and whether the old loop and the new
// one contract the same way is decided by the compiled code, not by the source.
#include "synth_part_clip.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdint.h>
#include <string.h>

constexpr int kBlock = 128;
constexpr float kSilenceGainDb = -90.0f;

// SynthVoiceSDK.h, FEATURE_SYNTH_OPEN
static float dbToAmplitude(float db)
{
    return db > kSilenceGainDb ? std::pow(10.0f, db * 0.05f) : 0.0f;
}
static void saturateSoftClip(std::span<float> buf, float gainDb)
{
    const float gainRaw = dbToAmplitude(gainDb);
    for (auto& element : buf)
    {
        float sample = std::clamp(element * gainRaw, -1.5f, 1.5f);
        sample = sample - (sample * sample * sample) * (1.0f / 6.72f);
        element = std::clamp(sample, -1.0f, 1.0f);
    }
}

extern "C"
{
float gRender[kBlock];
float gGainDb;
int32_t gReuse, gSkip;
int16_t gOldSend[kBlock], gOldPost[kBlock], gNewSend[kBlock], gNewPost[kBlock];
}

static float tempBufSendsCommonLeft[kBlock], tempBufPostfaderLeft[kBlock];

// synth_PolySynth.cpp of 1d9c033, the mono part, with the blocks as arrays
static void oldPath()
{
    memcpy(tempBufSendsCommonLeft, gRender, sizeof gRender);
    if (!gReuse)
        memcpy(tempBufPostfaderLeft, tempBufSendsCommonLeft, sizeof gRender);
    if (!gSkip)
    {
        saturateSoftClip(std::span<float>(tempBufSendsCommonLeft, kBlock), 0.0f);
        auto* src = tempBufSendsCommonLeft;
        auto* dest = gOldSend;
        for (int i = 0; i < kBlock; i++)
            *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
    }
    if (gReuse)
        memcpy(gOldPost, gOldSend, sizeof gOldPost);
    if (!gReuse)
    {
        saturateSoftClip(std::span<float>(tempBufPostfaderLeft, kBlock), gGainDb);
        auto* src = tempBufPostfaderLeft;
        auto* dest = gOldPost;
        for (int i = 0; i < kBlock; i++)
            *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
    }
}

static void newPath()
{
    part_clip::monoPart<kBlock>(gRender, gReuse ? 1.0f : dbToAmplitude(gGainDb), gReuse, gSkip, gSkip ? nullptr : gNewSend, gNewPost);
}

extern "C" void bench_setup(int) {}
extern "C" void bench_run(int which)
{
    if (which == 0)
        oldPath();
    else
        newPath();
}
