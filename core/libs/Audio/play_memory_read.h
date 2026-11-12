#ifndef play_memory_read_h_
#define play_memory_read_h_

// Sample read paths of AudioPlayMemory (not streamed voices). Pure functions of the sample data and the playhead, no class state, so the
// host test synth-open/tools/kern_sample_read_test.cpp runs this very code against the loops it replaced.
//
// The interpolation reads a sample and its neighbour. Both are two int16 values that sit next to each other in memory, so one 32 bit
// load gets both (the old code made two halfword loads, on SDRAM each of them is a bus cycle of its own). The word load may be
// unaligned: the Cortex-M4 does it in hardware (UNALIGN_TRP is not set) and the words read are the same two halfwords as before.

#include <stdint.h>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the word load unpacks the sample in the low half");

namespace pm_read
{
constexpr int32_t kMax16Bit = 65535;

// in[0] and in[1] with one 32 bit load
static inline __attribute__((always_inline)) void load2(const int16_t* in, int32_t& a, int32_t& b)
{
    uint32_t w;
    __builtin_memcpy(&w, in, 4);
    a = (int16_t)w;
    b = (int16_t)(w >> 16);
}

// The forward reading loops (loop forward, granular forward): the sample at idx and, when the voice interpolates, its right neighbour.
// cur = in[idx], dif = in[idx + 1] - in[idx] (0 when the interpolation is off)
static inline __attribute__((always_inline)) void readForward(const int16_t* in,
                                                              int32_t idx,
                                                              uint32_t interpolationCondition,
                                                              int16_t& cur,
                                                              int32_t& dif)
{
    if (interpolationCondition)
    {
        cur = in[idx];
        dif = 0;
    }
    else
    {
        int32_t a, b;
        load2(in + idx, a, b);
        cur = (int16_t)a;
        dif = b - a;
    }
}

// Loop ping pong: the neighbour is on the right (interpOffset 1) or on the left (interpOffset -1, the playhead runs backward)
static inline __attribute__((always_inline)) void readPingPong(const int16_t* in,
                                                               int32_t idx,
                                                               int32_t interpOffset,
                                                               uint32_t interpolationCondition,
                                                               int16_t& cur,
                                                               int32_t& dif)
{
    if (interpolationCondition)
    {
        cur = in[idx];
        dif = 0;
    }
    else
    {
        int32_t lo, hi;
        load2(in + idx + (interpOffset < 0 ? -1 : 0), lo, hi);
        const int32_t c = interpOffset < 0 ? hi : lo;
        cur = (int16_t)c;
        dif = (interpOffset < 0 ? lo : hi) - c;
    }
}

// One block of single shot, forward, not streamed and without glide: out[0..blockSamples) from the playhead. The state is in the
// arguments (the class keeps it in members, the caller copies it into locals and back). Same results as the one loop it replaced:
//  - integer pitch (no fraction, now or ever in the block): the interpolation term is ((0 >> 2) * dif) >> 14 = 0, so one read per sample
//  - pitch 1.0 and the whole block inside the sample: two samples per word load
//  - otherwise the loop with the sample and its neighbour in one word load, or the plain loop when the voice does not interpolate
// interpolationDif is not touched by the integer pitch paths. It is scratch: every read of it follows an assignment in the same loop.
template<int kBlockSamples>
static inline __attribute__((always_inline)) void singleShotBlock(int16_t* out,
                                                                  const int16_t* in,
                                                                  uint32_t length,
                                                                  int32_t endPoint,
                                                                  int32_t castPitchControl,
                                                                  int32_t currentFractionPitchControl,
                                                                  uint32_t interpolationCondition,
                                                                  int32_t& iPitchCounter,
                                                                  int32_t& currentFractionPitchCounter,
                                                                  int32_t& interpolationDif,
                                                                  int16_t& currentSampelValue,
                                                                  uint8_t& playing)
{
    if (currentFractionPitchControl == 0 && currentFractionPitchCounter == 0 && castPitchControl == 1 &&
        iPitchCounter + kBlockSamples <= (int32_t)length && iPitchCounter + kBlockSamples < endPoint)
    {
        // unpitched, whole block inside the sample (no end of sample, no wrap to the end): a copy of the samples
        const int16_t* src = in + iPitchCounter;
        for (int k = 0; k < kBlockSamples / 2; k++)
        {
            uint32_t w;
            __builtin_memcpy(&w, src + 2 * k, 4);
            __builtin_memcpy(out + 2 * k, &w, 4);
        }
        currentSampelValue = src[kBlockSamples - 1];
        iPitchCounter += kBlockSamples;
    }
    else if (currentFractionPitchControl == 0 && currentFractionPitchCounter == 0)
    {
        for (int i = 0; i < kBlockSamples; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                currentSampelValue = in[iPitchCounter];
                *out++ = currentSampelValue;
                iPitchCounter += castPitchControl;
                if (iPitchCounter >= endPoint)
                    iPitchCounter = length;
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
    }
    else if (!interpolationCondition)
    {
        // interpolating, fractional pitch: the sample and its neighbour in one word load. The flag is the same for the whole block and
        // the compiler does not hoist it out of the loop, so each case is a loop of its own
        for (int i = 0; i < kBlockSamples; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                int32_t a, b;
                load2(in + iPitchCounter, a, b);
                currentSampelValue = (int16_t)a;
                interpolationDif = b - a;
                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= kMax16Bit)
                {
                    currentFractionPitchCounter -= kMax16Bit;
                    iPitchCounter++;
                }
                if (iPitchCounter >= endPoint)
                    iPitchCounter = length;
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
    }
    else
    {
        // no interpolation (the playhead is near the end of the sample, or the voice does not interpolate): the old loop
        for (int i = 0; i < kBlockSamples; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                currentSampelValue = in[iPitchCounter];
                interpolationDif = 0;
                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= kMax16Bit)
                {
                    currentFractionPitchCounter -= kMax16Bit;
                    iPitchCounter++;
                }
                if (iPitchCounter >= endPoint)
                    iPitchCounter = length;
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
    }
}
} // namespace pm_read

#endif
