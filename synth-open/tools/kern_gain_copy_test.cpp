// Host test of applyGainCopy16 (core/libs/Audio/dspApplyGainCopy.h, used by MonoToStereo and AudioAmplifier) against the pair it replaces:
// memcpy of the block followed by applyGain in place (dspApplyGain.cpp of release-1.3.0-int 1d9c033, copied here verbatim).
//
//   c++ -std=c++20 -O2 -Wall -I synth-open/tools/kern_host -I core/libs/Audio synth-open/tools/kern_gain_copy_test.cpp -o /tmp/kern_gain_copy_test
//
// Compared bit for bit: the output block, the previous_mult that applyGain leaves behind, the target_mult, and that the source stays
// untouched. Gains: unity both (the memcpy case), zero both (the memset case), unity to anything, anything to unity, ramps in both
// directions, steps that are not a multiple of 128, gains above unity (saturating) and below zero, int16 extremes in the blocks.
// The MonoToStereo and AudioAmplifier update() code around it (which channel transmits the input as it is, the shared input test, the
// pool exhaustion paths) needs the block pool, it is covered by the graph bench (synth-open/tools/audio_opt, run_graph.sh).
#include "dspApplyGainCopy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr int kBlock = 128;
constexpr int32_t MULTI_UNITYGAIN = 65536;

uint64_t rngState = 0x2545F4914F6CDD1Dull;
uint32_t rnd()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return (uint32_t)(rngState >> 16);
}

// 1d9c033 dspApplyGain.cpp applyGain, verbatim
void applyGain(int16_t* data, int32_t& target_mult, int32_t& previous_mult)
{
    if (target_mult == MULTI_UNITYGAIN && previous_mult == MULTI_UNITYGAIN)
    {
        return;
    }
    if (target_mult == 0 && previous_mult == 0)
    {
        memset(data, 0, kBlock * sizeof(int16_t));
        return;
    }

    uint32_t* p = (uint32_t*)data;
    const uint32_t* end = (uint32_t*)(data + kBlock);

    int32_t multStep = (target_mult - previous_mult) / kBlock;
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

int32_t pickGain()
{
    switch (rnd() % 8)
    {
        case 0: return MULTI_UNITYGAIN;
        case 1: return 0;
        case 2: return (int32_t)(rnd() % 65537);
        case 3: return (int32_t)(rnd() % 400000) - 100000; // above unity and below zero
        case 4: return MULTI_UNITYGAIN + (int32_t)(rnd() % 5) - 2;
        case 5: return (int32_t)(rnd() % 3);
        default: return (int32_t)(rnd() % 65537);
    }
}
} // namespace

int main(int argc, char** argv)
{
    const uint32_t cases = 3000000u * (argc > 1 ? (uint32_t)atoi(argv[1]) : 1);
    uint64_t fails = 0, unity = 0, zero = 0;
    for (uint32_t c = 0; c < cases; c++)
    {
        alignas(4) int16_t src[kBlock], oldDst[kBlock], newDst[kBlock];
        for (auto& v : src)
        {
            const uint32_t r = rnd();
            v = (r & 7) == 0 ? (int16_t)((r & 8) ? 32767 : -32768) : (int16_t)(r >> 8);
        }
        const int32_t t = pickGain();
        const int32_t pv = (rnd() % 3 == 0) ? t : pickGain();
        int32_t tOld = t, pOld = pv, tNew = t, pNew = pv;
        int16_t srcCopy[kBlock];
        memcpy(srcCopy, src, sizeof src);
        memset(newDst, 0xA5, sizeof newDst);

        memcpy(oldDst, src, sizeof src);
        applyGain(oldDst, tOld, pOld);
        gain_copy::applyGainCopy16<kBlock>(newDst, src, tNew, pNew);

        if (t == MULTI_UNITYGAIN && pv == MULTI_UNITYGAIN)
            unity++;
        if (t == 0 && pv == 0)
            zero++;
        if (memcmp(oldDst, newDst, sizeof oldDst) || pOld != pNew || tOld != tNew || memcmp(src, srcCopy, sizeof src))
        {
            if (fails++ < 10)
                printf("FAIL case %u target %d previous %d\n", c, t, pv);
        }
    }
    printf("kern_gain_copy_test: %u cases (%llu unity, %llu zero), %llu failures\n", cases, (unsigned long long)unity, (unsigned long long)zero,
           (unsigned long long)fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
