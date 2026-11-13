// Host test of the sample envelope steady segment fast path (core/libs/Audio/effect_envelope_steady.h) against the loop it shortcuts.
//
//   c++ -std=c++20 -O2 -Wall -I synth-open/tools/kern_host -I core/libs/Audio synth-open/tools/kern_env_steady_test.cpp -o /tmp/kern_env_steady_test
//
// The header is the production code. The reference is the arithmetic of AudioPlayMemory::envelopeUpdate of release-1.3.0-int 1d9c033
// (effect_envelope.cpp, the wtMode < 2 branch of the step loop), step by step with every member stored and reloaded as it was:
//  - the state check of each step (sustain loop release, count == 0) is modelled as "the real loop leaves the arithmetic here"
//  - the gain, clamp and count update as in the loop
// Per case: the fast path must (a) only take a block the reference runs without a state change and without a clamp, (b) give the same
// samples, mult_hires and count, (c) leave everything untouched when it declines, and (d) not decline a block that is clean and starts
// inside the gain range (it would silently cost the gain). Cases: every count from 0 to 40, gains and increments at the clamp edges
// (0, 0x3FFFFFFF, one step before and after), positive and negative increments, 0 increment, unity gain, int16 extremes in the samples,
// the sustain release pending flag, 16 bit and 32 bit increments (the loop shifts inc_hires by 17).
#include "effect_envelope_steady.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr int kBlock = 128;
constexpr int32_t kMax = 0x3FFFFFFF;

uint64_t rngState = 0xD1B54A32D192ED03ull;
uint32_t rnd()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return (uint32_t)(rngState >> 16);
}
uint32_t rndBelow(uint32_t n) { return n ? rnd() % n : 0; }

struct Env
{
    int32_t mult_hires, inc_hires;
    uint16_t count;
    // scratch members of the loop, written every step (not part of the comparison)
    uint32_t sample12, sample34, sample56, sample78, tmp1, tmp2;
};

// 1d9c033 envelopeUpdate, wtMode < 2 step loop. Returns false as soon as the real loop would run its state machine (or the sustain
// release), which the fast path must never have taken. clamped is set when a step hit a clamp.
bool oldLoop(uint32_t* p, Env& envelope, bool sustainReleasePending, bool& clamped)
{
    uint32_t* end = p + kBlock / 2;
    clamped = false;
    while (p < end)
    {
        if (sustainReleasePending)
            return false;
        if (envelope.count == 0)
            return false;

        int32_t mult = envelope.mult_hires >> 14;
        int32_t inc = envelope.inc_hires >> 17;

        envelope.sample12 = *p;
        envelope.sample34 = *(p + 1);
        envelope.sample56 = *(p + 2);
        envelope.sample78 = *(p + 3);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample12);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample12);
        envelope.sample12 = pack_16b_16b(envelope.tmp2, envelope.tmp1);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample34);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample34);
        envelope.sample34 = pack_16b_16b(envelope.tmp2, envelope.tmp1);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample56);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample56);
        envelope.sample56 = pack_16b_16b(envelope.tmp2, envelope.tmp1);
        mult += inc;
        envelope.tmp1 = signed_multiply_32x16b(mult, envelope.sample78);
        mult += inc;
        envelope.tmp2 = signed_multiply_32x16t(mult, envelope.sample78);
        envelope.sample78 = pack_16b_16b(envelope.tmp2, envelope.tmp1);

        *p++ = envelope.sample12;
        *p++ = envelope.sample34;
        *p++ = envelope.sample56;
        *p++ = envelope.sample78;

        envelope.mult_hires += envelope.inc_hires;
        if (envelope.mult_hires < 0)
        {
            envelope.mult_hires = 0;
            envelope.count = 1;
            clamped = true;
        }
        else if (envelope.mult_hires > 0x3FFFFFFF)
        {
            envelope.mult_hires = 0x3FFFFFFF;
            envelope.count = 1;
            clamped = true;
        }

        if (envelope.count > 0)
        {
            envelope.count--;
        }
    }
    return true;
}

uint64_t checks = 0, fails = 0, taken = 0, declined = 0;
void fail(const char* what, long a, long b)
{
    if (fails++ < 20)
        printf("FAIL %s %ld %ld\n", what, a, b);
}

int32_t pickGain()
{
    switch (rnd() % 6)
    {
        case 0: return 0;
        case 1: return kMax;
        case 2: return (int32_t)rndBelow(64);
        case 3: return kMax - (int32_t)rndBelow(64);
        case 4: return 0x40000000 / 2; // 0.5
        default: return (int32_t)rndBelow(kMax + 1);
    }
}

int32_t pickInc(int32_t mh)
{
    switch (rnd() % 8)
    {
        case 0: return 0;
        case 1: return (int32_t)rndBelow(1 << 17);               // below the shift: inc >> 17 is 0
        case 2: return -(int32_t)rndBelow(1 << 17);
        case 3: return (int32_t)(rnd());                         // anything
        case 4: return (kMax - mh) / 16 + (int32_t)rndBelow(5) - 2; // lands on the top clamp edge after 16 steps
        case 5: return -(mh / 16) + (int32_t)rndBelow(5) - 2;    // lands on the bottom clamp edge
        case 6: return (int32_t)rndBelow(1 << 22) - (1 << 21);
        default: return (int32_t)(rnd() >> 12) - (1 << 19);
    }
}

uint32_t pickSample()
{
    auto h = [] {
        switch (rnd() % 6)
        {
            case 0: return (uint16_t)0x7FFF;
            case 1: return (uint16_t)0x8000;
            case 2: return (uint16_t)0;
            default: return (uint16_t)rnd();
        }
    };
    return ((uint32_t)h() << 16) | h();
}
} // namespace

int main(int argc, char** argv)
{
    const uint32_t cases = 2000000u * (argc > 1 ? (uint32_t)atoi(argv[1]) : 1);
    for (uint32_t c = 0; c < cases; c++)
    {
        Env a{};
        a.mult_hires = pickGain();
        a.inc_hires = pickInc(a.mult_hires);
        a.count = (uint16_t)(rnd() % 4 == 0 ? rndBelow(40) : (rnd() % 2 ? 16 + rndBelow(3) : rndBelow(65536)));
        const bool pending = rnd() % 16 == 0;
        uint32_t blockA[kBlock / 2], blockB[kBlock / 2], orig[kBlock / 2];
        for (auto& w : orig)
            w = pickSample();
        memcpy(blockA, orig, sizeof orig);
        memcpy(blockB, orig, sizeof orig);

        Env b = a;
        bool clamped;
        const bool clean = oldLoop(blockA, a, pending, clamped) && !clamped; // a: the reference after the block
        const int32_t mhStart = b.mult_hires;
        uint16_t countB = b.count;
        int32_t mhB = b.mult_hires;
        const bool took = env_steady::processBlock<kBlock>(blockB, mhB, b.inc_hires, countB, pending);
        checks++;
        if (took)
        {
            taken++;
            if (!clean)
                fail("took a block the loop would have left or clamped", c, 0);
            else if (memcmp(blockA, blockB, sizeof blockA) || a.mult_hires != mhB || a.count != countB)
                fail("different result", c, 1);
        }
        else
        {
            declined++;
            if (memcmp(orig, blockB, sizeof orig) || mhStart != mhB || b.count != countB)
                fail("touched a declined block", c, 2);
            if (clean && mhStart >= 0 && mhStart <= kMax)
                fail("declined a clean block", c, 3);
        }
    }
    printf("kern_env_steady_test: %llu cases, %llu taken, %llu declined, %llu failures\n", (unsigned long long)checks,
           (unsigned long long)taken, (unsigned long long)declined, (unsigned long long)fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
