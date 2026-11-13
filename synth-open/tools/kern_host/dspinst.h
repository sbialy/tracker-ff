// Host stand-in of core/libs/Audio/utility/dspinst.h for the kern_*_test.cpp tests (-I synth-open/tools/kern_host, ahead of core/libs/Audio/utility).
// The firmware header uses one Cortex-M4 DSP instruction per function (smulwb, smulwt, ssat, pkhbt). These are the semantics of
// those instructions as the KINETISL branch of the firmware header writes them in C, which is what the tests need on a host.
#ifndef dspinst_h_
#define dspinst_h_

#include <stdint.h>

static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift)
{
    int32_t out = val >> rshift;
    const int32_t max = 1 << (bits - 1);
    if (out >= 0)
    {
        if (out > max - 1)
            out = max - 1;
    }
    else
    {
        if (out < -max)
            out = -max;
    }
    return out;
}

// ((a[31:0] * b[15:0]) >> 16)
static inline int32_t signed_multiply_32x16b(int32_t a, uint32_t b)
{
    return (int32_t)(((int64_t)a * (int16_t)(b & 0xFFFF)) >> 16);
}

// ((a[31:0] * b[31:16]) >> 16)
static inline int32_t signed_multiply_32x16t(int32_t a, uint32_t b)
{
    return (int32_t)(((int64_t)a * (int16_t)(b >> 16)) >> 16);
}

// ((a[15:0] << 16) | b[15:0])
static inline uint32_t pack_16b_16b(int32_t a, int32_t b)
{
    return ((uint32_t)a << 16) | ((uint32_t)b & 0x0000FFFF);
}

#endif
