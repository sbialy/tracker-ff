// Soft Knee mixer saturation (FEATURE_SAT_SOFTKNEE, PLUG_HOST builds): the per sample curve of SaturationType::kSoftKnee in
// saturate() (core/libs/Audio/dspApplyGain.cpp), on the mixer's int32 accumulator (int16 units) like the stock types.
//
//   |x| <= K:  y = x                                  bit exact passthrough below the knee, K = 23197 (-3.0 dBFS of 32767)
//   |x| >  K:  y = K + R - R^2 / (R + |x| - K)        R = 32767 - K, sign of x
//
// The upper branch starts at y = K with slope 1 (the derivative R^2 / (R + d)^2 is 1 at d = 0), rises monotonically and
// approaches 32767 without passing it: full scale in gives 27982 (-1.37 dBFS), 2 x (+6 dB) 31003 (-0.48 dBFS), 4 x 31988.
// Integer only, one unsigned 32 bit division per sample above the knee, no libm. Host tested in tools/sat_softknee_test.cpp.
#pragma once

#include <stdint.h>

namespace softknee
{

constexpr int32_t kKnee = 23197;                                      // round(32767 * 10^(-3/20))
constexpr int32_t kRoom = 32767 - kKnee;                              // 9570
constexpr uint32_t kRoom2 = uint32_t(kRoom) * uint32_t(kRoom);        // 91,584,900, fits 32 bits

inline int16_t apply(int32_t x)
{
    const uint32_t a = x < 0 ? 0u - uint32_t(x) : uint32_t(x);   // |x|, INT32_MIN gives 2^31
    if (a <= uint32_t(kKnee)) return int16_t(x);
    const uint32_t q = kRoom2 / (uint32_t(kRoom) + (a - uint32_t(kKnee)));   // kRoom down to 0, no overflow: a <= 2^31
    const int32_t y = kKnee + kRoom - int32_t(q);                             // kKnee .. 32767
    return int16_t(x < 0 ? -y : y);
}

}   // namespace softknee
