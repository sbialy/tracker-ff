#ifndef INTERNAL_TICK_CLOCK_H_
#define INTERNAL_TICK_CLOCK_H_

#include <stdint.h>
#include <string.h>

// The clock of the internal sequencer tempo: how many sequencer ticks (nano steps) fall into one audio block, and where in the block.
//
// The M4F has no double precision unit, so the stock accumulator (double, microseconds) cost six soft double calls per tick and two soft
// double divisions per block. This one counts in integers, 1 / 2^26 us, and gives the same ticks with the same offsets as the stock
// code, bit for bit (synth-open/tools/seq_tick_test.cpp runs both side by side):
//  - the stock period is a float (getTimerPeriod_us returns float) with at most 24 significant bits, and the stock block length is
//    (double)intervalSeconds * 1e6, a 24 bit float times 15625. Both are whole multiples of 2^-26 us for the values in use (a period
//    of at least 0.125 us, a block of the 128 frame size), the stock double sums stay far below 2^53 units, so the stock double
//    arithmetic never rounded and this integer arithmetic is the same arithmetic.
//  - the period is not recomputed with a float divide (that rounds differently than the stock double expression and moves a tick by
//    1 ulp, a few us over minutes): the stock expression is evaluated only when the tempo or the swing factor changes, and the
//    result is kept. A few entries cover the two swing phases.
//  - one difference from the stock code, on purpose: the stock loop took the period once per audio block, so a tick that changes it
//    (the swing phase toggles at every step boundary, a Tempo FX sets a new tempo) left the rest of that block counting in the old
//    period. At a step boundary inside a block that is up to one block of the long phase counted as the short one or the other way round,
//    and the two errors of a pair of steps do not cancel (a measured +0.098 % at 125 BPM and swing 66, a project exports at 125.12).
//    Here the inputs are read again after every tick and the period follows at once. With no swing and no Tempo FX the inputs never
//    change inside a block and the ticks and offsets are bit for bit the stock ones (seq_tick_test.cpp checks both).
class InternalTickClock
{
  public:
    static constexpr int kFracBits = 26;
    static constexpr int64_t kNever = INT64_MAX;

    void reset() { accumulator = 0; }

    // The inputs of the period, as the sequencer holds them now (tempo in BPM, swingFactor 0 = no swing)
    struct Inputs
    {
        float tempo;
        float swingFactor;
    };

    // Adds one audio block and calls onTick(offsetUs) once per tick due in it, offsetUs = microseconds from the tick to the end of the block
    // (what the stock loop passed to midiSetClockSubBlockOffsetUs).
    //   inputs()            returns the Inputs now, called at the start of the block and again after every tick (a tick can change them)
    //   stockPeriodUs()     the stock float period in us for the inputs now, only called when they changed
    template<typename InputsFn, typename StockPeriodFn, typename TickFn>
    inline __attribute__((always_inline)) void advance(float intervalSeconds, InputsFn&& inputs, StockPeriodFn&& stockPeriodUs, TickFn&& onTick)
    {
        const int64_t block = blockFixed(intervalSeconds);
        Inputs in{ 0.0f, 0.0f };
        int64_t period = 0;
        bool known = false;

        accumulator += block;
        for (;;) // one place that reads the inputs and the period, before the first tick and after every tick
        {
            const Inputs now = inputs();
            if (!known || differs(now.tempo, in.tempo) || differs(now.swingFactor, in.swingFactor))
            {
                known = true;
                in = now;
                period = periodFixed(in.tempo, in.swingFactor, stockPeriodUs);
            }
            if (accumulator < period)
                break;
            accumulator -= period;
            const int64_t offset = block - accumulator;
            onTick(offset > 0 ? static_cast<uint32_t>((offset + kHalf) >> kFracBits) : 0U);
        }
    }

    int64_t accumulatorFixed() const { return accumulator; }

    // period in us -> units of 2^-26 us. A period that is not positive or not finite never ticks (the stock loop hung on a period of 0
    // and never ticked on inf or NaN); a positive one is at least 1 unit.
    static int64_t toFixed(float periodUs)
    {
        const float scaled = periodUs * 67108864.0f; // 2^26, exact
        if (!(scaled > 0.0f) || scaled >= 4.0e18f)
            return kNever;
        const int64_t v = static_cast<int64_t>(scaled);
        return v < 1 ? 1 : v;
    }

  private:
    static constexpr int64_t kHalf = int64_t(1) << (kFracBits - 1);

    static inline __attribute__((always_inline)) bool differs(float a, float b)
    {
        uint32_t ba, bb;
        memcpy(&ba, &a, sizeof(ba));
        memcpy(&bb, &b, sizeof(bb));
        return ba != bb;
    }

    inline __attribute__((always_inline)) int64_t blockFixed(float intervalSeconds)
    {
        uint32_t bits;
        memcpy(&bits, &intervalSeconds, sizeof(bits));
        if (!blockValid || bits != blockBits)
        {
            blockBits = bits;
            blockValid = true;
            // the stock expression, (double)intervalSeconds * 1e6, scaled by a power of two
            blockUnits = static_cast<int64_t>(static_cast<double>(intervalSeconds) * 1000000.0 * 67108864.0);
        }
        return blockUnits;
    }

    template<typename StockPeriodFn>
    inline __attribute__((always_inline)) int64_t periodFixed(float tempo, float swingFactor, StockPeriodFn&& stockPeriodUs)
    {
        uint32_t tempoBits, factorBits;
        memcpy(&tempoBits, &tempo, sizeof(tempoBits));
        memcpy(&factorBits, &swingFactor, sizeof(factorBits));
        for (int i = 0; i < kCacheEntries; i++)
        {
            if (cache[i].valid && cache[i].tempoBits == tempoBits && cache[i].factorBits == factorBits)
                return cache[i].period;
        }
        CacheEntry& e = cache[nextVictim];
        nextVictim = (nextVictim + 1) % kCacheEntries;
        e.tempoBits = tempoBits;
        e.factorBits = factorBits;
        e.period = toFixed(stockPeriodUs());
        e.valid = true;
        return e.period;
    }

    static constexpr int kCacheEntries = 3; // no swing, swing step off, swing step on

    struct CacheEntry
    {
        uint32_t tempoBits = 0;
        uint32_t factorBits = 0;
        int64_t period = 0;
        bool valid = false;
    };

    int64_t accumulator = 0; // units of 2^-26 us
    CacheEntry cache[kCacheEntries];
    int nextVictim = 0;
    uint32_t blockBits = 0;
    int64_t blockUnits = 0;
    bool blockValid = false;
};

#endif
