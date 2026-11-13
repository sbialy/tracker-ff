// Host test of the sample read paths of AudioPlayMemory (core/libs/Audio/play_memory_read.h) against the loops they replaced.
//
//   c++ -std=c++20 -O2 -Wall -I core/libs/Audio synth-open/tools/kern_sample_read_test.cpp -o /tmp/kern_sample_read_test && /tmp/kern_sample_read_test [seconds-scale]
//
// The header is the production code (the firmware includes the same file). The reference is the loop of release-1.3.0-int 1d9c033
// (play_memory_single_shot.cpp updateSingleShotNormal fast path, play_memory_loop_forward.cpp, play_memory_loop_pingpong.cpp, play_memory_granular.cpp),
// copied here verbatim, the reads as they were: a halfword load for the sample and a second one for the neighbour.
// What is compared, bit for bit:
//  - single shot, block by block with the state carried over (up to 40 blocks per run, until the sample has ended): the 128 output samples,
//    the playhead, the pitch fraction, the last sample and the playing flag. interpolationDif is scratch (every read of it follows an
//    assignment in the same loop), it is not compared
//  - the read helper of the loop forward and granular forward loops and the one of the ping pong loop, for every playhead of buffers
//    with random data, at all four alignments, with and without interpolation, both ping pong directions, at the first and last
//    positions of the buffer (the neighbour of the first sample for a backward ping pong is in[-1], the neighbour of the last one in[len])
// Pitches: integer 0..9, exactly 1.0 (the two samples per word path) with the whole block inside the sample and across the end, fractional
// with a fraction counter of 0 or random, the pitch fraction of every note of the table. 15 sample voice variants = these loops in the
// 4 voice types (single shot, loop forward, ping pong, granular forward): the read statement is the only thing that changed in them.
#include "play_memory_read.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
constexpr int kBlock = 128;
constexpr int32_t MAX_16BIT = 65535;

uint64_t rngState = 0x9E3779B97F4A7C15ull;
uint32_t rnd()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return (uint32_t)(rngState >> 16);
}
uint32_t rndBelow(uint32_t n) { return n ? rnd() % n : 0; }

struct Ps
{
    int32_t ip;
    int32_t frac;
    int32_t dif;
    int16_t cur;
    uint8_t playing;
};

// 1d9c033 updateSingleShotNormal, fast path loop
void oldSingleShot(int16_t* out, const int16_t* in, uint32_t length, int32_t endPoint, int32_t castPitchControl,
                   int32_t currentFractionPitchControl, uint32_t interpolationCondition, Ps& s)
{
    int32_t iPitchCounter = s.ip;
    int32_t currentFractionPitchCounter = s.frac;
    int32_t interpolationDif = s.dif;
    int16_t currentSampelValue = s.cur;
    uint8_t playing = s.playing;
    for (int i = 0; i < kBlock; i++)
    {
        if ((int32_t)length > iPitchCounter)
        {
            currentSampelValue = in[iPitchCounter];
            if (interpolationCondition)
                interpolationDif = 0;
            else
                interpolationDif = (in[iPitchCounter + 1] - currentSampelValue);
            *out++ = currentSampelValue + (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
            iPitchCounter += castPitchControl;
            currentFractionPitchCounter += currentFractionPitchControl;
            if (currentFractionPitchCounter >= MAX_16BIT)
            {
                currentFractionPitchCounter -= MAX_16BIT;
                iPitchCounter++;
            }
            if (iPitchCounter >= endPoint)
            {
                iPitchCounter = length;
            }
        }
        else
        {
            *out++ = 0;
            playing = 0;
        }
    }
    s.ip = iPitchCounter;
    s.frac = currentFractionPitchCounter;
    s.dif = interpolationDif;
    s.cur = currentSampelValue;
    s.playing = playing;
}

uint64_t checks = 0, fails = 0;
void fail(const char* what, long a, long b, long c)
{
    if (fails++ < 20)
        printf("FAIL %s %ld %ld %ld\n", what, a, b, c);
}

// sample data: random, with a guard of random words on both sides (the interpolation reads one past the end and one before the start)
struct Buf
{
    std::vector<int16_t> mem;
    int16_t* base; // sample 0
    size_t len;
    Buf(size_t n, unsigned align) : mem(n + 16), len(n)
    {
        for (auto& v : mem)
        {
            uint32_t r = rnd();
            // include the extremes: the difference of two neighbours must not overflow the arithmetic
            v = (r & 7) == 0 ? (int16_t)((r & 8) ? 32767 : -32768) : (int16_t)(r >> 8);
        }
        base = mem.data() + 4 + (align & 1); // align 0/1 = the sample at an even or odd halfword (word aligned or not)
    }
};

void testSingleShot(uint32_t cases)
{
    for (uint32_t c = 0; c < cases; c++)
    {
        const uint32_t len = 1 + rndBelow(rnd() % 8 == 0 ? 300 : 4000);
        Buf buf(len + 8, rnd() & 1);
        const int16_t* in = buf.base;
        int32_t endPoint = (rnd() % 4 == 0) ? (int32_t)(1 + rndBelow(len + 3)) : (int32_t)len;
        int32_t cast;
        int32_t fracCtl;
        switch (rnd() % 6)
        {
            case 0: cast = 1; fracCtl = 0; break;                 // exactly 1.0
            case 1: cast = rndBelow(10); fracCtl = 0; break;      // integer
            case 2: cast = 1; fracCtl = 1 + rndBelow(65534); break;
            default: cast = rndBelow(8); fracCtl = rnd() % 3 == 0 ? 0 : (int32_t)rndBelow(65535); break;
        }
        const uint32_t cond = rnd() % 3 == 0;
        Ps a;
        a.ip = (rnd() % 3 == 0) ? (int32_t)rndBelow(len) : 0;
        if (rnd() % 5 == 0 && len > 130)
            a.ip = (int32_t)len - 128 - (int32_t)rndBelow(4) + (int32_t)rndBelow(3); // around "the whole block fits"
        a.frac = (fracCtl == 0 && rnd() % 2) ? 0 : (int32_t)rndBelow(rnd() % 2 ? 1 : 65535);
        a.dif = 12345;
        a.cur = (int16_t)rnd();
        a.playing = 1;
        Ps b = a;
        for (int blk = 0; blk < 40 && a.playing; blk++)
        {
            int16_t oa[kBlock], ob[kBlock];
            memset(oa, 0x5a, sizeof oa);
            memset(ob, 0x5a, sizeof ob);
            // the class decides the interpolation once per block: the voice interpolates while 128 pitched samples still fit
            oldSingleShot(oa, in, len, endPoint, cast, fracCtl, cond, a);
            pm_read::singleShotBlock<kBlock>(ob, in, len, endPoint, cast, fracCtl, cond, b.ip, b.frac, b.dif, b.cur, b.playing);
            checks++;
            if (memcmp(oa, ob, sizeof oa) || a.ip != b.ip || a.frac != b.frac || a.cur != b.cur || a.playing != b.playing)
                fail("single shot", c, blk, a.ip);
        }
    }
}

void testReadForward(uint32_t cases)
{
    for (uint32_t c = 0; c < cases; c++)
    {
        const uint32_t len = 2 + rndBelow(200);
        Buf buf(len + 4, rnd() & 1);
        const int16_t* in = buf.base;
        for (int32_t idx = 0; idx < (int32_t)len; idx++) // every playhead, first and last included (in[len] is the guard word)
            for (uint32_t cond = 0; cond < 2; cond++)
            {
                int16_t curOld = in[idx];
                int32_t difOld = cond ? 0 : (in[idx + 1] - curOld);
                int16_t curNew = 0x1234;
                int32_t difNew = 777;
                pm_read::readForward(in, idx, cond, curNew, difNew);
                checks++;
                if (curOld != curNew || difOld != difNew)
                    fail("readForward", idx, cond, c);
            }
    }
}

void testReadPingPong(uint32_t cases)
{
    for (uint32_t c = 0; c < cases; c++)
    {
        const uint32_t len = 2 + rndBelow(200);
        Buf buf(len + 4, rnd() & 1);
        const int16_t* in = buf.base;
        for (int32_t idx = 0; idx < (int32_t)len; idx++)
            for (uint32_t cond = 0; cond < 2; cond++)
                for (int32_t off = -1; off <= 1; off += 2) // idx 0 with -1 reads in[-1], idx len - 1 with +1 reads in[len]
                {
                    int16_t curOld = in[idx];
                    int32_t difOld = cond ? 0 : (in[idx + off] - curOld);
                    int16_t curNew = 0x1234;
                    int32_t difNew = 777;
                    pm_read::readPingPong(in, idx, off, cond, curNew, difNew);
                    checks++;
                    if (curOld != curNew || difOld != difNew)
                        fail("readPingPong", idx, off, c);
                }
    }
}
} // namespace

int main(int argc, char** argv)
{
    const uint32_t scale = argc > 1 ? (uint32_t)atoi(argv[1]) : 1;
    testSingleShot(300000 * scale);
    testReadForward(3000 * scale);
    testReadPingPong(3000 * scale);
    printf("kern_sample_read_test: %llu comparisons, %llu failures\n", (unsigned long long)checks, (unsigned long long)fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
