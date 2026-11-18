#ifndef LIBS_AUDIO_DUCK_MATH_H_
#define LIBS_AUDIO_DUCK_MATH_H_

// The arithmetic of the send ducking (effect_duck.h), plain C++ with no audio graph: tools/duck_test.cpp runs it on the Mac.
//
// One value per block (128 samples, 2.9 ms): the peak of the chosen source. The envelope jumps up to a louder peak and falls
// by a fixed factor per block (the release). How far the returns are pulled down comes from where the envelope stands against
// the threshold: nothing at or under it, the full depth from twice the threshold (6 dB over it) up. The result is a gain for
// the delay and reverb returns, on a grid of 1/4096 so that a steady state sends nothing to the mixer.

#include <math.h>
#include <stdint.h>

constexpr uint8_t kDuckSources = 13;      // inputs of the detector: tracks 1..8, line in L and R, synth parts 1..3 (AudioMux numbering)
constexpr uint8_t kDuckPctMax = 100;
constexpr float kDuckBlockMs = 128.0f * 1000.0f / 44117.64706f;   // one audio block
constexpr float kDuckGrid = 4096.0f;

// The project's ducking: all zero = off. Stored in the plugin sidecar (synth-open/plug_sidecar.h), only when used.
struct DuckParams
{
    uint8_t source;    // 0 off, else the detector input + 1 (1..8 tracks, 9 and 10 line in L and R, 11.. synth parts)
    uint8_t depth;     // 0..100 %, how far the returns go down
    uint8_t release;   // 0..100, 30 ms at 0 to 960 ms at 100 (exponential)
    uint8_t thresh;    // 0..100, -40 dBFS at 0 to 0 dBFS at 100
};

constexpr DuckParams kDuckDefaults = {0, 60, 50, 50};   // what a project that switches ducking on starts from

constexpr uint8_t kDuckSourceMax = kDuckSources;

inline bool duckValid(const DuckParams& p)
{
    return p.source <= kDuckSourceMax && p.depth <= kDuckPctMax && p.release <= kDuckPctMax && p.thresh <= kDuckPctMax;
}

inline DuckParams duckClamp(DuckParams p)
{
    if (p.source > kDuckSourceMax)
        p.source = 0;
    if (p.depth > kDuckPctMax)
        p.depth = kDuckPctMax;
    if (p.release > kDuckPctMax)
        p.release = kDuckPctMax;
    if (p.thresh > kDuckPctMax)
        p.thresh = kDuckPctMax;
    return p;
}

inline float duckReleaseMs(uint8_t release)
{
    return 30.0f * powf(32.0f, release / 100.0f);
}

inline float duckThresholdDb(uint8_t thresh)
{
    return -40.0f + 0.4f * thresh;
}

// Largest |sample| of a block (-32768 counts as 32768)
inline uint16_t duckBlockPeak(const int16_t* d)
{
    uint32_t peak = 0;
    for (int n = 0; n < 128; n++)
    {
        const int32_t v = d[n];
        const uint32_t a = uint32_t(v < 0 ? -v : v);
        peak = a > peak ? a : peak;
    }
    return uint16_t(peak > 65535u ? 65535u : peak);
}

struct DuckState
{
    DuckParams params = {0, 0, 0, 0};
    float env = 0.0f;       // the envelope, in sample units
    float relCoef = 0.0f;   // envelope factor per block
    float thrInv = 0.0f;    // 1 / threshold in sample units
    float depthF = 0.0f;    // 0..1
    float gain = 1.0f;      // the gain of the last block, on the grid
    float sent = 1.0f;      // what the mixers hold

    void configure(const DuckParams& in)
    {
        const DuckParams p = duckClamp(in);
        if (p.source != params.source)
        {   // another source or off: nothing of the old one is left to duck from
            env = 0.0f;
        }
        params = p;
        relCoef = expf(-kDuckBlockMs / duckReleaseMs(p.release));
        thrInv = 1.0f / (32768.0f * powf(10.0f, duckThresholdDb(p.thresh) / 20.0f));
        depthF = p.depth / 100.0f;
        if (p.source == 0)
        {
            env = 0.0f;
            gain = 1.0f;
            sent = 1.0f;
        }
    }

    // One block of the source. Returns true when the mixers must be given `g` (the gain moved to a new grid value)
    bool step(uint16_t peak, float& g)
    {
        float e = env * relCoef;
        const float pk = float(peak);
        e = pk > e ? pk : e;
        if (e < 1.0f)
            e = 0.0f;
        env = e;
        float r = e * thrInv - 1.0f;
        r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
        float target = 1.0f - depthF * r;
        target = floorf(target * kDuckGrid + 0.5f) * (1.0f / kDuckGrid);
        gain = target;
        g = target;
        if (target == sent)
            return false;
        sent = target;
        return true;
    }
};

#endif /* LIBS_AUDIO_DUCK_MATH_H_ */
