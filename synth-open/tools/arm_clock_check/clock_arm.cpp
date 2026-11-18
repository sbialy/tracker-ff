// The firmware's integer sequencer clock (core/source/internalTickClock.h) as the release compiler builds it for the Cortex-M4F:
// the stock period expression (double, soft float on the M4F) and the clock, run by arm_clock_check.py in the Unicorn emulator
// and compared with the exact integer model. Built with the flags of platforms/mk66/makefile (-O2, -mfpu=fpv4-sp-d16 -mfloat-abi=hard).
#include "internalTickClock.h"

extern "C" void* memcpy(void* d, const void* s, size_t n)
{
    uint8_t* dd = static_cast<uint8_t*>(d);
    const uint8_t* ss = static_cast<const uint8_t*>(s);
    while (n--)
        *dd++ = *ss++;
    return d;
}

extern "C" void* memset(void* d, int c, size_t n)
{
    uint8_t* dd = static_cast<uint8_t*>(d);
    while (n--)
        *dd++ = uint8_t(c);
    return d;
}

namespace
{
// Sequencer::getTimerPeriod_us, no swing (core/source/mtSequencer.cpp)
float timerPeriodUs(float tempo)
{
    return (312500.0 / tempo) / 12.0;
}
float bitsToFloat(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}
}   // namespace

struct Result
{
    uint32_t ticks;
    uint32_t lastOffsetUs;
    int64_t accumulator;
    uint64_t periodFixed;
};
Result g_result;

// blocks audio blocks of intervalSeconds (float bits) at tempo (float bits), no swing: the ticks counted, the accumulator left
extern "C" void clock_run(uint32_t tempoBits, uint32_t intervalBits, uint32_t blocks)
{
    InternalTickClock clock;
    const float tempo = bitsToFloat(tempoBits);
    const float interval = bitsToFloat(intervalBits);
    uint32_t ticks = 0, last = 0;
    for (uint32_t b = 0; b < blocks; b++)
    {
        clock.advance(interval, [tempo]() { return InternalTickClock::Inputs{ tempo, 0.0f }; }, [tempo]() { return timerPeriodUs(tempo); },
                      [&](uint32_t offsetUs)
                      {
                          ticks++;
                          last = offsetUs;
                      });
    }
    g_result.ticks = ticks;
    g_result.lastOffsetUs = last;
    g_result.accumulator = clock.accumulatorFixed();
    g_result.periodFixed = static_cast<uint64_t>(InternalTickClock::toFixed(timerPeriodUs(tempo)));
}
