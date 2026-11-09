#include "dspApplyGain.h"
#include "AudioStream.h"
#include "dspinst.h"
#ifdef FEATURE_SAT_SOFTKNEE
#include "sat_softknee.h"
#endif

#define MULTI_UNITYGAIN 65536

AUDIO_CORE_RAM void applyGain(int16_t* data, int32_t& target_mult, int32_t& previous_mult)
{
    if (target_mult == MULTI_UNITYGAIN && previous_mult == MULTI_UNITYGAIN)
    {
        return;
    }
    if (target_mult == 0 && previous_mult == 0)
    {
        memset(data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        return;
    }

    uint32_t* p = (uint32_t*)data;
    const uint32_t* end = (uint32_t*)(data + AUDIO_BLOCK_SAMPLES);

    int32_t multStep = (target_mult - previous_mult) / AUDIO_BLOCK_SAMPLES;
    int32_t multToApply = previous_mult;

    do
    {
        uint32_t tmp32 = *p; // read 2 samples from *data
        int32_t val1 = signed_multiply_32x16b(multToApply, tmp32);
        multToApply += multStep;
        int32_t val2 = signed_multiply_32x16t(multToApply, tmp32);
        multToApply += multStep;
        val1 = signed_saturate_rshift(val1, 16, 0);
        val2 = signed_saturate_rshift(val2, 16, 0);
        *p++ = pack_16b_16b(val2, val1);
    } while (p < end);

    previous_mult = target_mult;
}

AUDIO_CORE_RAM void applyGainAndCopy(int32_t* dst,
                                 const int16_t* in,
                                 int32_t& target_mult,
                                 int32_t& previous_mult)
{
    if (target_mult == 0 && previous_mult == 0)
    {
        memset(dst, 0, AUDIO_BLOCK_SAMPLES * sizeof(int32_t));
        return;
    }

    const uint32_t* src = (uint32_t*)in;
    const int32_t* end = dst + AUDIO_BLOCK_SAMPLES;

    if (target_mult == MULTI_UNITYGAIN && previous_mult == MULTI_UNITYGAIN)
    {
        do
        {
            uint32_t tmp32 = *src++;
            *dst++ = (int16_t)(tmp32 & 0xffff);
            *dst++ = (int16_t)((tmp32 >> 16) & 0xffff);
        } while (dst < end);
    }
    else
    {
        int32_t multStep = (target_mult - previous_mult) / AUDIO_BLOCK_SAMPLES;
        int32_t multToApply = previous_mult;
        do
        {
            uint32_t tmp32 = *src++;
            int32_t val1 = signed_multiply_32x16b(multToApply, tmp32);
            multToApply += multStep;
            int32_t val2 = signed_multiply_32x16t(multToApply, tmp32);
            multToApply += multStep;
            *dst++ = val1;
            *dst++ = val2;

        } while (dst < end);

        previous_mult = target_mult;
    }
}

AUDIO_CORE_RAM void applyGainThenAdd(int16_t* data,
                                 const int16_t* in,
                                 int32_t& target_mult,
                                 int32_t& previous_mult)
{
    if (target_mult == 0 && previous_mult == 0)
    {
        return;
    }

    uint32_t* dst = (uint32_t*)data;
    const uint32_t* src = (uint32_t*)in;
    const uint32_t* end = (uint32_t*)(data + AUDIO_BLOCK_SAMPLES);

    if (target_mult == MULTI_UNITYGAIN && previous_mult == MULTI_UNITYGAIN)
    {
        do
        {
            uint32_t tmp32 = *dst;
            *dst++ = signed_add_16_and_16(tmp32, *src++);
            tmp32 = *dst;
            *dst++ = signed_add_16_and_16(tmp32, *src++);
        } while (dst < end);
    }
    else
    {
        int32_t multStep = (target_mult - previous_mult) / AUDIO_BLOCK_SAMPLES;
        int32_t multToApply = previous_mult;

        do
        {
            uint32_t tmp32 = *src++; // read 2 samples from *data
            int32_t val1 = signed_multiply_32x16b(multToApply, tmp32);
            multToApply += multStep;
            int32_t val2 = signed_multiply_32x16t(multToApply, tmp32);
            multToApply += multStep;
            val1 = signed_saturate_rshift(val1, 16, 0);
            val2 = signed_saturate_rshift(val2, 16, 0);
            tmp32 = pack_16b_16b(val2, val1);
            uint32_t tmp32b = *dst;
            *dst++ = signed_add_16_and_16(tmp32, tmp32b);
        } while (dst < end);

        previous_mult = target_mult;
    }
}

AUDIO_CORE_RAM void applyGainThenAdd(int32_t* dst,
                                 const int16_t* in,
                                 int32_t& target_mult,
                                 int32_t& previous_mult)
{
    if (target_mult == 0 && previous_mult == 0)
    {
        return;
    }

    const uint32_t* src = (uint32_t*)in;
    const int32_t* end = dst + AUDIO_BLOCK_SAMPLES;

    if (target_mult == MULTI_UNITYGAIN && previous_mult == MULTI_UNITYGAIN)
    {
        do
        {
            uint32_t tmp32 = *src++;
            *dst++ += (int16_t)(tmp32 & 0xffff);
            *dst++ += (int16_t)((tmp32 >> 16) & 0xffff);
        } while (dst < end);
    }
    else
    {
        int32_t multStep = (target_mult - previous_mult) / AUDIO_BLOCK_SAMPLES;
        int32_t multToApply = previous_mult;

        do
        {
            uint32_t tmp32 = *src++;
            int32_t val1 = signed_multiply_32x16b(multToApply, tmp32);
            multToApply += multStep;
            int32_t val2 = signed_multiply_32x16t(multToApply, tmp32);
            multToApply += multStep;
            *dst++ += val1;
            *dst++ += val2;
        } while (dst < end);

        previous_mult = target_mult;
    }
}

AUDIO_CORE_RAM void saturate(int16_t* dstBuf, const int32_t* inBuf, SaturationType saturationType)
{
    const int16_t* endBuf = dstBuf + AUDIO_BLOCK_SAMPLES;

    switch (saturationType)
    {
        case SaturationType::kHardClip:
            do
            {
                *dstBuf++ = saturate16(*inBuf++);
            } while (dstBuf < endBuf);
            break;
        case SaturationType::kSoftClip:
            do
            {
                const int32_t in = *inBuf++;
                int32_t out;

                // out = in - in^3/6.72
                // 6.72 set experimentally
                // https://www.desmos.com/calculator/rjynbskttb
                out = in - multiply_32x32_rshift(
                             multiply_32x32_rshift(multiply_32x32_rshift(in, in, 16), in, 16),
                             (int32_t)((1 << 16) / 6.72f),
                             15);
                *dstBuf++ = saturate16(out);
            } while (dstBuf < endBuf);
            break;
        case SaturationType::kSaturate:
            do
            {
                const int32_t in = *inBuf++;
                const int32_t inAbs = in >= 0 ? in : -in;
                const int32_t inSign = in >= 0 ? INT16_MAX : INT16_MIN;
                int32_t out;
                if (inAbs > INT16_MAX)
                {
                    *dstBuf++ = inSign;
                }
                else
                {
                    // out = 3 * in / 2 - in^3 / 2
                    // https://www.desmos.com/calculator/svadljg4am
                    out =
                      3 * in / 2 - multiply_32x32_rshift(multiply_32x32_rshift(in, in, 15), in, 16);
                    *dstBuf++ = saturate16(out); // saturate needed due to rounding errors
                                                 // out == 0x8000 when in == 0x7fff
                }
            } while (dstBuf < endBuf);
            break;
        case SaturationType::kOverdrive:
            do
            {
                const int32_t in = *inBuf++;
                const int32_t inAbs = in >= 0 ? in : -in;
                const int32_t inSign = in >= 0 ? INT16_MAX : INT16_MIN;
                int32_t out;

                if (inAbs > (int32_t)2 * INT16_MAX / 3)
                {
                    // y = 1
                    out = inSign;
                }
                else if (inAbs > INT16_MAX / 3)
                {
                    // y = (3 - (2 - 3x)^2) / 3
                    int32_t temp = 2 * (int32_t)INT16_MAX - 3 * (int32_t)inAbs;
                    out = (3 * (int32_t)INT16_MAX - multiply_32x32_rshift(temp, temp, 16)) / 3;
                    if (inSign < 0)
                    {
                        out = -out;
                    }
                }
                else
                {
                    // y = 2x
                    out = 2 * in;
                }
                *dstBuf++ = out;
            } while (dstBuf < endBuf);
            break;
        case SaturationType::kOverdrive2:
            do
            {
                const int32_t in = *inBuf++;
                const int32_t inAbs = in >= 0 ? in : -in;
                const int32_t inSign = in >= 0 ? INT16_MAX : INT16_MIN;
                int32_t out;

                if (inAbs > (int32_t)INT16_MAX / 2)
                {
                    // y = 1
                    out = inSign;
                }
                else if (inAbs > INT16_MAX / 4)
                {
                    // y = (4 - (2 - 4x)^3) / 4
                    // https://www.desmos.com/calculator/svadljg4am
                    int32_t temp = 2 * (int32_t)INT16_MAX - 4 * inAbs;
                    out = (4 * (int32_t)INT16_MAX -
                           multiply_32x32_rshift(multiply_32x32_rshift(temp, temp, 16), temp, 16)) /
                          4;
                    if (inSign < 0)
                    {
                        out = -out;
                    }
                }
                else
                {
                    // y = 3x
                    out = 3 * in;
                }
                *dstBuf++ = out;
            } while (dstBuf < endBuf);
            break;
#ifdef FEATURE_SAT_SOFTKNEE
        case SaturationType::kSoftKnee:
            // passthrough below -3 dBFS, a smooth curve into the int16 ceiling above (sat_softknee.h)
            do
            {
                *dstBuf++ = softknee::apply(*inBuf++);
            } while (dstBuf < endBuf);
            break;
#endif
        case SaturationType::kSaturationTypeEnd: assert(false); break;
    }
}
