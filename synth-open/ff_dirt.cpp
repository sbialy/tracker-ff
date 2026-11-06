// DIRT live master bus insert, the per block DSP (see ff_dirt.h). It has no dependency on the audio library, so the host render
// (tools/dirt_render.sh) and the cycle bench (tools/dirt_cycles) run the same code as the firmware. The coefficients, the
// presets and the AudioStream object are in ff_dirt_ctl.cpp.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the DIRT module of opentracker-fx (modules/dirt/src/dirt.cpp, commit 6e5a7da), MIT licence, text in licenses/LICENSE-OpenTracker-fx-MIT.txt.
#include "ff_dirt.h"

namespace ffdirt
{


namespace
{

constexpr float kInv = 1.0f / 32768.0f;

#if defined(__arm__) && defined(__ARM_FP)
// Clamp to [-1, 1): the saturating VCVT to Q31 and back.
inline float clamp1(float x)
{
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(x));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(x));
    return x;
}
// Mid-tread quantiser of y in [-1, 1) to 2^bits levels: y as Q31 (the saturating VCVT clamps), half a step added with
// saturation (QADD) and the low 32 - bits bits cleared. Rounds to the nearest step and has a level at 0, so zero in is
// exactly zero out (half is below one step, so half & mask = 0).
inline float quantQ31(float y, uint32_t mask, uint32_t half)
{
    uint32_t k;
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(y));
    __asm__("vmov %0, %1" : "=r"(k) : "t"(y));
    __asm__("qadd %0, %0, %1" : "+r"(k) : "r"(half));
    k &= mask;
    __asm__("vmov %0, %1" : "=t"(y) : "r"(k));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(y));
    return y;
}
// Float (full scale 1.0) to int16: truncate, saturate.
inline int16_t toS16(float y)
{
    int32_t v = (int32_t)(y * 32768.0f);
    __asm__("ssat %0, #16, %0" : "+r"(v));
    return (int16_t)v;
}
#else
inline float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }
inline float quantQ31(float y, uint32_t mask, uint32_t half)
{
    const double v = double(y) * 2147483648.0;
    const int32_t k = v >= 2147483647.0 ? 2147483647 : (v <= -2147483648.0 ? int32_t(-2147483647 - 1) : int32_t(v));
    const int64_t sum = int64_t(k) + int64_t(half);
    const int32_t sat = sum > 2147483647 ? 2147483647 : int32_t(sum);
    const uint32_t u = uint32_t(sat) & mask;
    return float(int32_t(u)) * (1.0f / 2147483648.0f);
}
inline int16_t toS16(float y)
{
    float v = y * 32768.0f;
    v = v < -32768.0f ? -32768.0f : (v > 32767.0f ? 32767.0f : v);
    return (int16_t)(int32_t)v;
}
#endif

inline float absf(float x) { return __builtin_fabsf(x); }

// Rational tanh of 3 u (exact at 0 and +-3, clamped beyond), u clamped to +-1.
inline float tanhU(float u)
{
    u = clamp1(u);
    const float u2 = u * u;
    return u * (3.0f + u2) / (1.0f + 3.0f * u2);
}

// F 0: the tanh family. x is the pre-gained sample (the pre-gain holds the 1/3 of tanhU), the bias and its output at 0 are
// Asym's, 0 for Soft, as in the original's fast loop (Soft and Asym share it). F 1: Hard (clip) and Fold.
inline float fold(float x)
{   // triangle fold: period 4, +-1 at x = +-1
    float t = (x + 1.0f) * 0.25f;
    const int32_t k = int32_t(t);
    t -= float(k) - (t < float(k) ? 1.0f : 0.0f);
    return 1.0f - 4.0f * absf(t - 0.5f);
}

template <int F>
void run(const Coef& c, State& st, int16_t* __restrict l, int16_t* __restrict r, float wet, const float dWet)
{
    const float pre = c.pre, post = c.post, bias = c.bias, biasY = c.biasY, a = c.dcA, g = c.g;
    const bool isFold = c.type == kFold;
    const uint32_t quant = c.quant, mask = c.mask, half = c.half, tone = c.tone, rate = c.rate;
    float h0 = st.hold[0], h1 = st.hold[1], lp0 = st.lp[0], lp1 = st.lp[1], dc0 = st.dc[0], dc1 = st.dc[1];
    uint32_t cnt = st.cnt > rate ? rate : st.cnt;
    for (int i = 0; i < kBlock; ++i)
    {
        const float x0 = float(l[i]) * kInv, x1 = float(r[i]) * kInv;
        if (cnt == 0)
        {   // the shaper only at the hold points
            float y0, y1;
            if (F == 0)
            {
                y0 = tanhU(pre * x0 + bias) - biasY;
                y1 = tanhU(pre * x1 + bias) - biasY;
                dc0 += a * (y0 - dc0);   // Asym's DC block (a = 0 for Soft)
                dc1 += a * (y1 - dc1);
                y0 -= dc0;
                y1 -= dc1;
            }
            else
            {
                y0 = isFold ? fold(pre * x0) : clamp1(pre * x0);
                y1 = isFold ? fold(pre * x1) : clamp1(pre * x1);
            }
            if (quant)
            {
                y0 = quantQ31(y0, mask, half);
                y1 = quantQ31(y1, mask, half);
            }
            h0 = y0;
            h1 = y1;
            cnt = rate;
        }
        --cnt;
        float y0 = h0, y1 = h1;
        if (tone)
        {   // TPT one-pole low-pass
            const float v0 = (y0 - lp0) * g, v1 = (y1 - lp1) * g;
            y0 = v0 + lp0;
            y1 = v1 + lp1;
            lp0 = y0 + v0;
            lp1 = y1 + v1;
        }
        else
        {   // bypassed: the state follows (no step when it comes in)
            lp0 = y0;
            lp1 = y1;
        }
        l[i] = toS16(x0 + wet * (y0 * post - x0));   // exact at wet 0
        r[i] = toS16(x1 + wet * (y1 * post - x1));
        wet += dWet;
    }
    // flush the small stuff, no denormals in the state
    st.hold[0] = h0;
    st.hold[1] = h1;
    st.lp[0] = absf(lp0) < 1e-20f ? 0.0f : lp0;
    st.lp[1] = absf(lp1) < 1e-20f ? 0.0f : lp1;
    st.dc[0] = absf(dc0) < 1e-20f ? 0.0f : dc0;
    st.dc[1] = absf(dc1) < 1e-20f ? 0.0f : dc1;
    st.cnt = cnt;
}

}   // namespace

void process(const Coef& c, State& st, int16_t* l, int16_t* r, float wet0, float wet1)
{
    const float dWet = (wet1 - wet0) * (1.0f / float(kBlock));
    if (c.type == kSoft || c.type == kAsym)
        run<0>(c, st, l, r, wet0, dWet);
    else
        run<1>(c, st, l, r, wet0, dWet);
}

}   // namespace ffdirt

