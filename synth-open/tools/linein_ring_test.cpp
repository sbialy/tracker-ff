// The line in block ring (core/libs/Audio/LineInRing.h) on a simulated timeline of input and output interrupts, against the
// 1.4.0 bare pointer read. Every input block carries its sequence number in all its words, so the render output shows each
// duplicate and skip. Scenarios: render jitter across the input interrupt, forced double renders (an output interrupt held off
// past the next period, output_i2s.cpp catch-up), both together, and a raised lead (more renders than input, a real underrun).
// Pass: the ring output never repeats or skips a block outside a real underrun, the latency stays at one block, and the ring's
// old race counters equal the duplicates and skips of the old model on the same timeline.
#include "LineInRing.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
constexpr uint32_t kWords = 128;
int failures = 0;

void check(bool ok, const char* scenario, const char* what)
{
    if (!ok)
    {
        std::printf("FAIL %s: %s\n", scenario, what);
        failures++;
    }
}

struct Event
{
    double t;
    int renders;   // 0 = input interrupt, else output interrupt rendering this many blocks
};

struct Result
{
    uint32_t oldDup = 0, oldSkip = 0;   // the bare pointer model
    uint32_t newRepeat = 0, newSkip = 0, maxLag = 0;
    uint32_t dup = 0, skip = 0, under = 0, drop = 0;
    uint32_t renders = 0;
};

// periods: number of input blocks. render(k) gives the output interrupts: time offset in periods and blocks rendered
Result run(int periods, double (*phase)(int), int (*count)(int))
{
    std::vector<Event> ev;
    for (int k = 0; k < periods; k++)
    {
        ev.push_back({ double(k + 1), 0 });   // block k is complete at the end of period k
        const int n = count(k);
        if (n > 0) ev.push_back({ k + 1 + phase(k), n });
    }
    // input first on equal times: the input interrupt has the higher priority (4 against 5)
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });

    static uint32_t slots[8][kWords];
    LineInRing<8, kWords> ring;
    ring.slots = slots;
    uint32_t dmaHalf[kWords];
    int latest = -1;         // the block the old stored pointer shows
    int oldLastRead = -1;    // old model: last block it read
    int newLast = -1;        // new model: last block handed out
    Result r;
    bool oldStarted = false;
    for (const Event& e : ev)
    {
        if (e.renders == 0)
        {
            latest++;
            for (uint32_t& w : dmaHalf) w = uint32_t(latest);
            ring.push(dmaHalf);
            continue;
        }
        for (int i = 0; i < e.renders; i++)
        {
            r.renders++;
            if (latest >= 0)
            {
                if (oldStarted)
                {
                    if (latest == oldLastRead) r.oldDup++;
                    else if (latest > oldLastRead + 1) r.oldSkip += uint32_t(latest - oldLastRead - 1);
                }
                oldStarted = true;
                oldLastRead = latest;
            }
            const uint32_t* b = ring.fetch();
            if (b == nullptr) continue;
            const int id = int(b[0]);
            for (uint32_t w = 1; w < kWords; w++)
                if (b[w] != b[0]) { check(false, "torn", "block words differ"); break; }
            if (newLast >= 0)
            {
                if (id == newLast) r.newRepeat++;
                else if (id > newLast + 1) r.newSkip += uint32_t(id - newLast - 1);
                else if (id < newLast) check(false, "order", "block older than the one before");
            }
            newLast = id;
            const uint32_t lag = uint32_t(latest - id);
            if (lag > r.maxLag) r.maxLag = lag;
        }
    }
    r.dup = ring.dup;
    r.skip = ring.skip;
    r.under = ring.under;
    r.drop = ring.drop;
    return r;
}

uint32_t rng = 12345;
double frand()
{
    rng = rng * 1664525u + 1013904223u;
    return double(rng >> 8) / double(1u << 24);
}

void report(const char* name, const Result& r)
{
    std::printf("%-22s renders=%u old dup=%u skip=%u | ring dup=%u skip=%u under=%u drop=%u | out repeat=%u skip=%u lag<=%u\n",
                name, r.renders, r.oldDup, r.oldSkip, r.dup, r.skip, r.under, r.drop, r.newRepeat, r.newSkip, r.maxLag);
    check(r.dup == r.oldDup, name, "ring dup counter != old model duplicates");
    check(r.skip == r.oldSkip, name, "ring skip counter != old model skips");
    check(r.newRepeat == r.under, name, "a repeated block outside a counted underrun");
    check(r.newSkip == r.drop, name, "a skipped block outside a counted drop");
    check(r.maxLag <= 3, name, "latency over two extra blocks even in a transient");
}
} // namespace

int main()
{
    const int N = 20000;
    // 1. render jitter across the input interrupt: render at -0.05..+0.05 periods around it
    Result a = run(N, [](int) { return frand() * 0.1 - 0.05; }, [](int) { return 1; });
    report("jitter", a);
    check(a.oldDup > 0 && a.oldSkip > 0, "jitter", "the old model shows no race (test is not exercising it)");
    check(a.under <= 1 && a.drop == 0, "jitter", "ring glitched more than the start");
    // 2. forced double renders: every 37th output interrupt is held off past the next period and renders two
    Result b = run(N, [](int k) { return (k % 37 == 0) ? 1.3 : 0.3; }, [](int k) { return (k % 37 == 1) ? 0 : (k % 37 == 0 ? 2 : 1); });
    // (k % 37 == 0) renders two at k+1.3, k % 37 == 1 renders none of its own: the late interrupt took its block
    report("double render", b);
    check(b.oldDup > 0 && b.oldSkip > 0, "double render", "the old model shows no race");
    check(b.under == 0 && b.drop == 0 && b.newRepeat == 0 && b.newSkip == 0, "double render", "duplicated or skipped blocks");
    // 3. both: jitter around 0.02 plus double renders
    Result c = run(N, [](int k) { return (k % 53 == 0) ? 1.02 + frand() * 0.05 : frand() * 0.06 - 0.03; },
                   [](int k) { return (k % 53 == 1) ? 0 : (k % 53 == 0 ? 2 : 1); });
    report("jitter+double", c);
    check(c.under <= 1 && c.drop == 0, "jitter+double", "ring glitched more than the start");
    // 4. a raised lead (FlexNVM install): 4 extra renders, then 4 interrupts render nothing. Real underruns are allowed here.
    Result d = run(N, [](int) { return 0.5; }, [](int k) { const int m = k % 1000; return m < 4 ? 2 : (m < 8 ? 0 : 1); });
    report("lead raise", d);
    check(d.under > 0, "lead raise", "no underrun counted for more renders than input");

    if (failures == 0) std::printf("PASS linein_ring_test (4 scenarios, %d blocks each)\n", N);
    return failures == 0 ? 0 : 1;
}
