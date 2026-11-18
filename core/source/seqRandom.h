// Private random generator of the sequencer (seeded random takes).
//
// Every random decision of a playing pattern (Chance, Random Note, Random Value, Random Volume, Random Instrument, the random roll
// and arp directions, the Random play mode) draws from one xorshift32 state that belongs to the Sequencer. The shared generator
// utilMathRandom is re-seeded from the clock by the envelope code while a pattern plays, so it cannot repeat a take.
//
// A seed of 1..255 makes a take reproducible: the same seed, the same pattern data and a Play from the same position give the same
// decisions on every playback. Seed 0 is Off: the state starts from a draw of the shared generator at Play, which the clock
// keeps changing, so a take is different every time as before. Host only dependency: <stdint.h>, so the host tests include it as is.
#pragma once

#include <stdint.h>

namespace seqrand
{

// A well mixed non-zero state for a user seed (a murmur3 finaliser over the seed and a constant): neighbouring seeds 1, 2, 3
// must not start neighbouring streams, and xorshift32 must never be given 0.
constexpr uint32_t stateForSeed(uint8_t seed)
{
    uint32_t x = uint32_t(seed) * 0x9E3779B1u + 0x6D2B79F5u;
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x ? x : 0x2545F491u;
}

struct Rng
{
    uint32_t s = 0x2545F491u;

    // seed 1..255: the stream of that seed. seed 0 (Off): the stream of `clockDraw`, any value that changes from play to play
    void reseed(uint8_t seed, uint32_t clockDraw)
    {
        if (seed)
        {
            s = stateForSeed(seed);
            return;
        }
        s = clockDraw ? clockDraw : 0x2545F491u;
    }

    uint32_t next()
    {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x;
        return x;
    }

    // 0 .. howbig-1 (0 when howbig is 0). The upper bits are the better ones of a xorshift.
    uint32_t below(uint32_t howbig) { return howbig ? (next() >> 8) % howbig : 0u; }

    // howsmall .. howbig-1 (howsmall when the range is empty): utilMathRandom::random(howsmall, howbig)
    int32_t range(int32_t howsmall, int32_t howbig)
    {
        if (howsmall >= howbig) return howsmall;
        return int32_t(below(uint32_t(howbig - howsmall))) + howsmall;
    }
};

// A new seed for the Reroll action: 1..255 and not `current`, from any entropy word
constexpr uint8_t rerolledSeed(uint8_t current, uint32_t entropy)
{
    uint32_t x = entropy * 0x9E3779B1u;
    x ^= x >> 15;
    uint8_t n = uint8_t(1u + (x >> 8) % 255u);
    if (n == current) n = uint8_t(n % 255u + 1u);   // the next one, wrapping 255 -> 1
    return n;
}

}   // namespace seqrand
