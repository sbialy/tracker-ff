// Host test of the mono part path of PolySynth::update (core/libs/Audio/synth_part_clip.h) against the passes it replaces.
//
//   c++ -std=c++20 -O2 -ffp-contract=fast -Wall -I core/libs/Audio synth-open/tools/kern_synth_part_test.cpp -o /tmp/kern_synth_part_test
//   /tmp/kern_synth_part_test [scale | full]      full: every one of the 2^32 float bit patterns through the clip, about 30 s
//
// The header is the production code. The reference is release-1.3.0-int 1d9c033, copied here as it was:
//  - polysynth::sdk::saturateSoftClip (synth/include/PolySynth/sdk/SynthVoiceSDK.h, the FEATURE_SYNTH_OPEN form) and dbToAmplitude
//  - the mono branch of PolySynth::update (synth_PolySynth.cpp): the float copy for the post fader, the clip of the send buffer with gain
//    0 dB and of the post buffer with the part gain, the two int16 conversion passes, the memcpy of a 0 dB part
// The firmware compiles both with -ffp-contract=fast (the GCC default), so the clip is a vnmul + vfma. This test is built the same way, the
// fused multiply-add is the part that decides the last bit. The compiled ARM code is checked separately, see the commit message.
//
// Compared bit for bit: both int16 output blocks, for the three cases (0 dB part, silent sends, normal), gains from -inf (silenced) over
// -90 dB to +24 dB, blocks of random floats (all exponents, NaN, inf, subnormals, -0), the clip knee at +-1.5 and the peak region around
// +-1.4967, and the gain cache against dbToAmplitude for every sequence of gain changes of 3 parts, NaN and -0 included.
#include "synth_part_clip.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

namespace
{
constexpr int kBlock = 128;
constexpr float kSilenceGainDb = -90.0f;

uint64_t rngState = 0xA0761D6478BD642Full;
uint32_t rnd()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return (uint32_t)(rngState >> 16);
}

// SynthVoiceSDK.h
float dbToAmplitude(float db)
{
    return db > kSilenceGainDb ? std::pow(10.0f, db * 0.05f) : 0.0f;
}

void saturateSoftClip(std::span<float> buf, float gainDb)
{
    const float gainRaw = dbToAmplitude(gainDb);
    for (auto& element : buf)
    {
        float sample = std::clamp(element * gainRaw, -1.5f, 1.5f);
        sample = sample - (sample * sample * sample) * (1.0f / 6.72f);
        element = std::clamp(sample, -1.0f, 1.0f);
    }
}

// synth_PolySynth.cpp, the mono part (hasSends false, isStereo false), blocks as arrays
void oldMonoPart(const float* render, float gainDb, bool reuseUnmodifiedMono, bool skipSendBlocks, int16_t* send, int16_t* post)
{
    float common[kBlock], postfader[kBlock];
    memcpy(common, render, sizeof common);
    if (!reuseUnmodifiedMono)
        memcpy(postfader, common, sizeof common);
    if (!skipSendBlocks)
    {
        saturateSoftClip(std::span<float>(common, kBlock), 0.0f);
        const float* src = common;
        int16_t* dest = send;
        for (int i = 0; i < kBlock; i++)
            *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
    }
    if (reuseUnmodifiedMono)
        memcpy(post, send, kBlock * sizeof(int16_t));
    if (!reuseUnmodifiedMono)
    {
        saturateSoftClip(std::span<float>(postfader, kBlock), gainDb);
        const float* src = postfader;
        int16_t* dest = post;
        for (int i = 0; i < kBlock; i++)
            *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
    }
}

uint64_t checks = 0, fails = 0;
void fail(const char* what, long a, long b)
{
    if (fails++ < 20)
        printf("FAIL %s %ld %ld\n", what, a, b);
}

float floatFromBits(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

float pickSample()
{
    switch (rnd() % 10)
    {
        case 0: return floatFromBits(rnd());                                    // anything, NaN and inf included
        case 1: return ((int32_t)rnd() / 2147483648.0f) * 2.0f;                  // -2..2
        case 2: return (rnd() & 1 ? 1.5f : -1.5f) + ((int32_t)(rnd() % 9) - 4) * 1e-6f; // the knee
        case 3: return (rnd() & 1 ? 1.4967f : -1.4967f) + ((int32_t)(rnd() % 9) - 4) * 1e-5f; // the peak of the cubic
        case 4: return (rnd() & 1 ? 1.0f : -1.0f) * (1.0f + ((int32_t)(rnd() % 9) - 4) * 1e-7f);
        case 5: return rnd() & 1 ? 0.0f : -0.0f;
        case 6: return floatFromBits(rnd() & 0x807FFFFF);                        // subnormals
        default: return ((int32_t)rnd() / 2147483648.0f) * 1.2f;                 // the usual range
    }
}

float pickGainDb()
{
    switch (rnd() % 10)
    {
        case 0: return 0.0f;
        case 1: return -0.0f;
        case 2: return kSilenceGainDb;
        case 3: return kSilenceGainDb + ((int32_t)(rnd() % 5) - 2) * 0.01f;
        case 4: return -INFINITY;
        case 5: return INFINITY;
        case 6: return __builtin_nanf("");
        case 7: return (float)(int)(rnd() % 49) - 24.0f;
        default: return ((int32_t)rnd() / 2147483648.0f) * 40.0f - 10.0f;
    }
}

void testBlocks(uint32_t cases)
{
    for (uint32_t c = 0; c < cases; c++)
    {
        float render[kBlock];
        for (auto& v : render)
            v = pickSample();
        const float gainDb = pickGainDb();
        const bool reuse = gainDb == 0.0f; // PolySynth::update: reuseUnmodifiedMono = gainDB == 0.0f for a mono part
        const bool skip = !reuse && (rnd() % 3 == 0);
        int16_t oSend[kBlock], oPost[kBlock], nSend[kBlock], nPost[kBlock];
        memset(oSend, 0x11, sizeof oSend);
        memset(nSend, 0x11, sizeof nSend);
        memset(oPost, 0x22, sizeof oPost);
        memset(nPost, 0x22, sizeof nPost);
        oldMonoPart(render, gainDb, reuse, skip, oSend, oPost);
        part_clip::monoPart<kBlock>(render, reuse ? 1.0f : dbToAmplitude(gainDb), reuse, skip, skip ? nullptr : nSend, nPost);
        checks++;
        if (memcmp(oPost, nPost, sizeof oPost))
            fail("post block", c, (long)(gainDb * 100));
        if (!skip && memcmp(oSend, nSend, sizeof oSend))
            fail("send block", c, (long)(gainDb * 100));
    }
}

void testGainCache(uint32_t cases)
{
    part_clip::GainCache<3> cache = { { __builtin_nanf(""), __builtin_nanf(""), __builtin_nanf("") }, { 0.0f, 0.0f, 0.0f } };
    float cur[3] = { 0, 0, 0 };
    for (uint32_t c = 0; c < cases; c++)
    {
        const int part = rnd() % 3;
        if (rnd() % 4 == 0)
            cur[part] = pickGainDb();
        const float got = cache.get(part, cur[part], [](float db) { return dbToAmplitude(db); });
        const float want = dbToAmplitude(cur[part]);
        checks++;
        if (memcmp(&got, &want, 4) && !(std::isnan(got) && std::isnan(want)))
            fail("gain cache", c, part);
    }
}

// every float through the clip (gain 1): the whole input space of the shared kernel, old pass against new
void testExhaustive()
{
    for (uint64_t u = 0; u <= 0xFFFFFFFFull; u++)
    {
        float x = floatFromBits((uint32_t)u);
        float one[1] = { x };
        saturateSoftClip(std::span<float>(one, 1), 0.0f);
        const int16_t o = static_cast<int16_t>(one[0] * INT16_MAX);
        const int16_t n = part_clip::clipToInt16(x);
        checks++;
        if (o != n)
            fail("clip exhaustive", (long)u, 0);
    }
}
} // namespace

int main(int argc, char** argv)
{
    const bool full = argc > 1 && !strcmp(argv[1], "full");
    const uint32_t scale = (argc > 1 && !full) ? (uint32_t)atoi(argv[1]) : 1;
    testBlocks(400000 * scale);
    testGainCache(2000000 * scale);
    if (full)
        testExhaustive();
    printf("kern_synth_part_test: %llu comparisons%s, %llu failures\n", (unsigned long long)checks, full ? " (incl. all 2^32 floats)" : "", (unsigned long long)fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
