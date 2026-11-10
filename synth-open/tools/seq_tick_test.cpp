// Host test of the integer sequencer clock (core/source/internalTickClock.h) against the stock double accumulator it replaced.
//
//   c++ -std=c++20 -O2 -Wall -I core/source synth-open/tools/seq_tick_test.cpp -o /tmp/seq_tick_test && /tmp/seq_tick_test [minutes]
//
// Both clocks run side by side, block by block, on the same inputs. The check is exact: every block must give the same number of ticks
// with the same offsets (the value midiSetClockSubBlockOffsetUs gets), so no step and no MIDI clock moves to another block, ever.
// Runs: a tempo sweep 5..800 BPM, the named tempos 97.3 133 200 300, swing 25..75 with the step toggle, tempo and swing changes in the
// middle of a run, restarts, two block lengths (MK66 and IMX sample rates). The "naive" column is the first idea (int64 in 1/1024 us and
// the period as one float divide): it is counted only to show what the exact version avoids, it is not part of the verdict.
// Default simulated length per run is 10 minutes.
#include "internalTickClock.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
// stock Sequencer::getTimerPeriod_us
float stockPeriodUs(float tempo, float swingFactor)
{
    if (swingFactor != 0.0f)
        return ((3125.0 * swingFactor) / tempo) / 12.0;
    return (312500.0 / tempo) / 12.0;
}

// the stock accumulator, verbatim
struct StockClock
{
    double acc = 0.0;
    void reset() { acc = 0.0; }
    template<typename F>
    void advance(float intervalSeconds, float tempo, float swingFactor, F&& onTick)
    {
        double uStepPeriod_us = static_cast<double>(stockPeriodUs(tempo, swingFactor));
        double blockDurationUs = static_cast<double>(intervalSeconds) * 1000000.0;
        acc += blockDurationUs;
        while (acc >= uStepPeriod_us)
        {
            acc -= uStepPeriod_us;
            double offsetUs = blockDurationUs - acc;
            onTick(offsetUs > 0.0 ? static_cast<uint32_t>(offsetUs + 0.5) : 0U);
        }
    }
};

// the first proposal: int64 in 1/1024 us, the period as one float divide
struct NaiveClock
{
    int64_t acc = 0;
    void reset() { acc = 0; }
    template<typename F>
    void advance(float intervalSeconds, float tempo, float swingFactor, F&& onTick)
    {
        const int64_t block = (int64_t)((double)intervalSeconds * 1000000.0 * 1024.0);
        const float periodUs = swingFactor != 0.0f ? (3125.0f * swingFactor) / tempo / 12.0f : (312500.0f / tempo) / 12.0f;
        const int64_t per = (int64_t)(periodUs * 1024.0f);
        acc += block;
        while (acc >= per)
        {
            acc -= per;
            int32_t off = (int32_t)((block - acc + 512) >> 10);
            onTick(off > 0 ? (uint32_t)off : 0U);
        }
    }
};

struct Ev
{
    uint32_t n = 0;
    uint32_t off[4096];
    void clear() { n = 0; }
    void add(uint32_t o)
    {
        if (n < 4096)
            off[n++] = o;
    }
};

struct Totals
{
    uint64_t blocks = 0, ticks = 0;
    uint64_t exactMismatchBlocks = 0, exactCountMismatch = 0;
    uint64_t naiveMismatchBlocks = 0, naiveCountMismatch = 0, naiveOffsetOnly = 0;
    uint64_t inexactBlockUnits = 0, inexactPeriodUnits = 0;
    uint64_t runs = 0;
    uint64_t firstBad = 0;
};

uint32_t g_rng = 12345;
uint32_t rnd()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

struct Scenario
{
    float tempo;
    int swing;          // 0 = off, else the swing value 25..75
    bool changes;       // tempo / swing changes and restarts during the run
    float intervalSeconds;
    const char* name;
};

void run(const Scenario& sc, double minutes, Totals& t)
{
    StockClock stock;
    NaiveClock naive;
    InternalTickClock fixedClock;

    float tempo = sc.tempo;
    int swing = sc.swing;
    bool toggle = false;
    const double blocksPerRun = minutes * 60.0 / (double)sc.intervalSeconds;
    const uint64_t blocks = (uint64_t)blocksPerRun;
    uint64_t ticksSinceToggle = 0;

    // the integer representation of the block length must be exact for the lengths in use
    {
        double units = (double)sc.intervalSeconds * 1000000.0 * 67108864.0;
        if (units != std::floor(units))
            t.inexactBlockUnits++;
    }

    Ev a, b, c;
    const uint64_t naiveCountBefore = t.naiveCountMismatch;
    for (uint64_t blk = 0; blk < blocks; blk++)
    {
        if (sc.changes)
        {
            const uint32_t r = rnd();
            if ((r % 1500) == 0) // a new tempo, any of the three kinds of values
            {
                const uint32_t k = (r >> 12) % 4;
                tempo = k == 0 ? 5.0f + (float)((r >> 8) % 7950) / 10.0f : k == 1 ? (float)(20 + ((r >> 8) % 400)) : k == 2 ? 97.3f : 133.0f + (float)((r >> 8) % 7) / 3.0f;
            }
            if ((r % 2900) == 1) // swing on, off, other value
            {
                const int vals[6] = { 0, 25, 33, 50, 62, 75 };
                swing = vals[(r >> 10) % 6];
                if (swing == 0)
                    toggle = false;
            }
            if ((r % 9000) == 2) // play restarted
            {
                stock.reset();
                naive.reset();
                fixedClock.reset();
            }
        }
        const float factor = swing ? (toggle ? (100.0f - (float)swing) + 50.0f : (float)swing + 50.0f) : 0.0f;

        a.clear();
        b.clear();
        c.clear();
        stock.advance(sc.intervalSeconds, tempo, factor, [&](uint32_t o) { a.add(o); });
        fixedClock.advance(
          sc.intervalSeconds, tempo, factor, [&]() { return stockPeriodUs(tempo, factor); }, [&](uint32_t o) { b.add(o); });
        naive.advance(sc.intervalSeconds, tempo, factor, [&](uint32_t o) { c.add(o); });

        t.blocks++;
        t.ticks += a.n;
        bool same = a.n == b.n;
        for (uint32_t i = 0; same && i < a.n; i++)
            same = a.off[i] == b.off[i];
        if (!same)
        {
            t.exactMismatchBlocks++;
            if (a.n != b.n)
                t.exactCountMismatch++;
            if (!t.firstBad)
            {
                t.firstBad = 1;
                printf("MISMATCH %s tempo %.4f swing %d block %llu: stock %u ticks, fixed %u ticks (first offsets %u / %u)\n", sc.name, tempo, swing,
                       (unsigned long long)blk, a.n, b.n, a.n ? a.off[0] : 0, b.n ? b.off[0] : 0);
            }
        }
        bool sameNaive = a.n == c.n;
        for (uint32_t i = 0; sameNaive && i < a.n; i++)
            sameNaive = a.off[i] == c.off[i];
        if (!sameNaive)
        {
            t.naiveMismatchBlocks++;
            if (a.n != c.n)
                t.naiveCountMismatch++;
            else
                t.naiveOffsetOnly++;
        }
        // the swing step toggle: once per 48 ticks, as the sequencer toggles it once per step
        if (swing)
        {
            ticksSinceToggle += a.n;
            while (ticksSinceToggle >= 48)
            {
                ticksSinceToggle -= 48;
                toggle = !toggle;
            }
        }
    }
    t.runs++;
    if (!strcmp(sc.name, "named"))
        printf("  named tempo %7.3f BPM, %.0f min: naive clock has a tick in another block in %llu of %llu blocks (%.2f %%), the shipped clock in 0\n", sc.tempo,
               minutes, (unsigned long long)(t.naiveCountMismatch - naiveCountBefore), (unsigned long long)blocks,
               100.0 * (double)(t.naiveCountMismatch - naiveCountBefore) / (double)blocks);
    // the period must be an exact multiple of 2^-26 us as well
    if (!sc.changes)
    {
        const float p = stockPeriodUs(tempo, swing ? (100.0f - (float)swing) + 50.0f : 0.0f);
        const double units = (double)p * 67108864.0;
        if (units != std::floor(units))
            t.inexactPeriodUnits++;
    }
}
}   // namespace

int main(int argc, char** argv)
{
    const double minutes = argc > 1 ? atof(argv[1]) : 10.0;
    const float mk66 = 128 / 44117.6470588f; // Tracker.cpp: length / kAudioSampleRate
    const float imx = 128 / 44100.0f;
    std::vector<Scenario> list;

    for (int i = 0; i <= 1575; i++) // 5 .. 800 BPM in steps of 0.5
        list.push_back({ 5.0f + 0.5f * i, 0, false, mk66, "sweep" });
    for (float tp : { 97.3f, 133.0f, 200.0f, 300.0f, 120.0f, 99.99f, 33.333f, 187.5f, 143.7f, 800.0f, 5.0f })
    {
        list.push_back({ tp, 0, false, mk66, "named" });
        list.push_back({ tp, 0, false, imx, "named-imx" });
        for (int sw : { 25, 33, 50, 62, 75 })
        {
            list.push_back({ tp, sw, false, mk66, "swing" });
            list.push_back({ tp, sw, false, imx, "swing-imx" });
        }
    }
    for (int i = 0; i < 40; i++) // changes in the middle of the run, different seeds through the start tempo
    {
        list.push_back({ 60.0f + 17.3f * i, (i % 3) ? 0 : 50, true, mk66, "changes" });
        list.push_back({ 60.0f + 11.1f * i, (i % 2) ? 62 : 0, true, imx, "changes-imx" });
    }

    Totals t;
    for (const Scenario& sc : list)
        run(sc, minutes, t);

    printf("runs %llu, %.1f simulated minutes each, %llu blocks (%.1f hours of audio), %llu ticks\n", (unsigned long long)t.runs, minutes,
           (unsigned long long)t.blocks, t.blocks * (double)mk66 / 3600.0, (unsigned long long)t.ticks);
    printf("block length not a whole number of 2^-26 us in %llu runs, period not whole in %llu runs\n", (unsigned long long)t.inexactBlockUnits,
           (unsigned long long)t.inexactPeriodUnits);
    printf("fixed point clock vs stock:  blocks with a different tick list %llu (different tick count %llu)\n", (unsigned long long)t.exactMismatchBlocks,
           (unsigned long long)t.exactCountMismatch);
    printf("naive int64 1/1024 us + float divide vs stock (not shipped): different tick list in %llu blocks, a tick in another block in %llu, offset only %llu\n",
           (unsigned long long)t.naiveMismatchBlocks, (unsigned long long)t.naiveCountMismatch, (unsigned long long)t.naiveOffsetOnly);
    const bool ok = t.exactMismatchBlocks == 0 && t.inexactBlockUnits == 0 && t.inexactPeriodUnits == 0;
    printf("%s\n", ok ? "RESULT: PASS (no step or clock tick moved)" : "RESULT: FAIL");
    return ok ? 0 : 1;
}
