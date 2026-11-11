#ifndef effect_envelope_steady_h_
#define effect_envelope_steady_h_

// Steady segment fast path of AudioPlayMemory::envelopeUpdate (the sample envelope, wtMode < 2). Pure function of the block and the
// envelope gain state, so the host test synth-open/tools/kern_env_steady_test.cpp runs this very code against the loop it shortcuts.
//
// The envelope loop works in steps of 8 samples. A step can change the state of the envelope (the count of the segment reaches 0, a
// sustain loop is released, the 30 bit gain hits a clamp), and then it does things the loop must see. When none of that can happen
// in this block the 16 steps are the same arithmetic on the gain, which then stays in registers: the stores to the block through p may
// alias the envelope members, so the plain loop stores and reloads sample12..tmp2, mult_hires and count in every step.
//  - count >= 16: the count is not 0 at the start of any of the 16 steps and it is decremented in each of them
//  - the sustain loop release check is not pending
//  - mult_hires and mult_hires + 16 * inc_hires are inside [0, 0x3FFFFFFF]: the gain moves linearly, so no step is clamped
// The members sample12 .. tmp2 of the envelope are scratch of the plain loop (nothing reads them), they are not written here.

#include "dspinst.h"
#include <stdint.h>

namespace env_steady
{
constexpr int32_t kMaxHires = 0x3FFFFFFF;

// p: the block as 64 words. Returns true when the block was processed (p, mult_hires and count are updated as the plain loop would),
// false when nothing was touched and the plain loop has to run.
template<int kBlockSamples>
static inline __attribute__((always_inline)) bool processBlock(uint32_t* p,
                                                               int32_t& mult_hires,
                                                               int32_t inc_hires,
                                                               uint16_t& count,
                                                               bool sustainReleasePending)
{
    constexpr int kSteps = kBlockSamples / 8;
    if (sustainReleasePending || count < kSteps)
        return false;
    int32_t mh = mult_hires;
    const int64_t last = (int64_t)mh + (int64_t)inc_hires * kSteps;
    if (mh < 0 || mh > kMaxHires || last < 0 || last > kMaxHires)
        return false;

    const int32_t inc = inc_hires >> 17;
    uint32_t* const end = p + kBlockSamples / 2;
    do
    {
        int32_t mult = mh >> 14;
        const uint32_t s12 = p[0], s34 = p[1], s56 = p[2], s78 = p[3];
        int32_t t1, t2;
        mult += inc;
        t1 = signed_multiply_32x16b(mult, s12);
        mult += inc;
        t2 = signed_multiply_32x16t(mult, s12);
        p[0] = pack_16b_16b(t2, t1);
        mult += inc;
        t1 = signed_multiply_32x16b(mult, s34);
        mult += inc;
        t2 = signed_multiply_32x16t(mult, s34);
        p[1] = pack_16b_16b(t2, t1);
        mult += inc;
        t1 = signed_multiply_32x16b(mult, s56);
        mult += inc;
        t2 = signed_multiply_32x16t(mult, s56);
        p[2] = pack_16b_16b(t2, t1);
        mult += inc;
        t1 = signed_multiply_32x16b(mult, s78);
        mult += inc;
        t2 = signed_multiply_32x16t(mult, s78);
        p[3] = pack_16b_16b(t2, t1);
        p += 4;
        mh += inc_hires;
    } while (p < end);
    mult_hires = mh;
    count -= kSteps;
    return true;
}
} // namespace env_steady

#endif
