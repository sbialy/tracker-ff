// Host test of the sequencer's private random generator (core/source/seqRandom.h), seeded random takes.
//
//   c++ -std=c++20 -O2 -Wall -I core/source synth-open/tools/seq_random_test.cpp -o /tmp/seq_random_test && /tmp/seq_random_test
//
// The properties the feature stands on: the same seed gives the same stream, every one of the 255 seeds gives its own stream, the
// state never reaches 0 (xorshift32 would stick), Off (seed 0) follows the clock word it is given, below() and range() stay in range
// and are close to uniform (the Chance and Random FX decisions are modulo draws), and Reroll never hands back the current seed.
#include "seqRandom.h"

#include <cstdio>
#include <set>
#include <vector>

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}
}   // namespace

int main()
{
    using seqrand::Rng;

    // same seed, same stream (and the clock word is ignored once there is a seed)
    {
        Rng a, b;
        a.reseed(42, 111u);
        b.reseed(42, 999u);
        bool same = true;
        for (int i = 0; i < 100000; i++) same = same && a.next() == b.next();
        check(same, "seed 42: two generators give the same 100,000 words whatever the clock word was");
    }

    // a reseed returns to the start of the stream (what Play does)
    {
        Rng a;
        a.reseed(7, 1u);
        std::vector<uint32_t> first;
        for (int i = 0; i < 64; i++) first.push_back(a.next());
        for (int i = 0; i < 1000; i++) a.below(100);   // a take that played on
        a.reseed(7, 5u);
        bool same = true;
        for (int i = 0; i < 64; i++) same = same && a.next() == first[size_t(i)];
        check(same, "reseed with the same seed restarts the stream");
    }

    // every seed has its own stream: the first 8 words of the 255 seeds are all different, no state is 0
    {
        std::set<std::vector<uint32_t>> streams;
        std::set<uint32_t> firstWords;
        bool nonzero = true;
        for (int sd = 1; sd <= 255; sd++)
        {
            Rng r;
            r.reseed(uint8_t(sd), 0u);
            nonzero = nonzero && r.s != 0;
            std::vector<uint32_t> w;
            for (int i = 0; i < 8; i++) w.push_back(r.next());
            streams.insert(w);
            firstWords.insert(w[0]);
        }
        check(nonzero, "the start state of every seed 1..255 is not 0");
        check(streams.size() == 255 && firstWords.size() == 255, "255 seeds give 255 different streams and first words");
    }

    // neighbouring seeds do not start neighbouring streams: the first Chance 50 outcomes of seeds 1..64 are all different
    {
        std::set<uint64_t> takes;
        for (int sd = 1; sd <= 64; sd++)
        {
            Rng r;
            r.reseed(uint8_t(sd), 0u);
            uint64_t bits = 0;
            for (int i = 0; i < 64; i++) bits = (bits << 1) | (r.below(100) < 50 ? 1u : 0u);
            takes.insert(bits);
        }
        check(takes.size() == 64, "seeds 1..64: 64 different 64 step Chance 50 takes");
    }

    // Off follows the clock word, and never starts from 0
    {
        Rng a, b, c;
        a.reseed(0, 12345u);
        b.reseed(0, 12346u);
        c.reseed(0, 0u);
        check(a.next() != b.next(), "Off: different clock words give different takes");
        check(c.s != 0 && c.next() != 0, "Off with a clock word of 0 still has a live state");
    }

    // below() and range() stay in range, and the draws are close to uniform
    {
        Rng r;
        r.reseed(99, 0u);
        bool inRange = true;
        int hist[100] = {};
        const int N = 1000000;
        for (int i = 0; i < N; i++)
        {
            const uint32_t v = r.below(100);
            inRange = inRange && v < 100;
            hist[v]++;
        }
        int lo = N, hi = 0;
        for (int h : hist)
        {
            if (h < lo) lo = h;
            if (h > hi) hi = h;
        }
        check(inRange, "below(100) stays in 0..99");
        // expected 10,000 per bucket, sigma 100: 5 sigma is 500
        check(lo > 9500 && hi < 10500, "below(100): 1,000,000 draws, every bucket within 5 sigma of 10,000");
        check(r.below(0) == 0, "below(0) is 0");
        bool rangeOk = true;
        for (int i = 0; i < 100000; i++)
        {
            const int32_t v = r.range(-12, 13);
            rangeOk = rangeOk && v >= -12 && v < 13;
        }
        check(rangeOk, "range(-12, 13) stays in -12..12");
        check(r.range(5, 5) == 5 && r.range(9, 3) == 9, "an empty range gives the lower bound (utilMathRandom::random(lo, hi) rule)");
        int heads = 0;
        for (int i = 0; i < N; i++) heads += r.below(100) < 50 ? 1 : 0;
        check(heads > 498000 && heads < 502000, "Chance 50: 1,000,000 draws fire 49.8 to 50.2 percent");
    }

    // Reroll: 1..255, never the current seed, reaches the whole range
    {
        bool ok = true;
        std::set<int> seen;
        for (uint32_t e = 0; e < 200000; e++)
        {
            const uint8_t cur = uint8_t(e % 256u);
            const uint8_t n = seqrand::rerolledSeed(cur, e * 2654435761u + 17u);
            ok = ok && n >= 1 && n != cur;
            seen.insert(n);
        }
        check(ok, "rerolledSeed: always 1..255 and never the current seed (including Off and 255)");
        check(seen.size() == 255, "rerolledSeed reaches all 255 seeds");
        static_assert(seqrand::rerolledSeed(255, 0) >= 1, "constexpr");
    }

    printf("%s\n", g_fail ? "RESULT: RED" : "RESULT: GREEN");
    return g_fail ? 1 : 0;
}
