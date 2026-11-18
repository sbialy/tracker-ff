// Host test of the sequencer clock the master inserts get (plug_transport.h, PLUG_EXT_TEMPO). NOT firmware.
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_transport_test.cpp -o plug_transport_test
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "plug_transport.h"

namespace
{
int g_fail = 0, g_checks = 0;
void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}
}

int main()
{
    using namespace plug;
    // stopped: beat 0, playing 0, the tempo is passed on
    Transport t = transportOf(133.0f, false, 12345);
    check(t.playing == 0 && t.beat == 0.0f && t.bpm == 133.0f, "stopped: playing 0, beat 0, tempo kept");
    // the first tick after Play (timerTicks 1) is beat 0
    t = transportOf(120.0f, true, 1);
    check(t.playing == 1 && t.beat == 0.0f, "Play: timerTicks 1 is the downbeat");
    // 2304 ticks are one beat, 576 one step
    t = transportOf(120.0f, true, 1 + 2304);
    check(t.beat == 1.0f, "2304 ticks later: beat 1");
    t = transportOf(120.0f, true, 1 + 576);
    check(t.beat == 0.25f, "one step (576 ticks) is a quarter beat");
    t = transportOf(120.0f, true, 1 + 4 * 2304 + 1152);
    check(t.beat == 4.5f, "4.5 beats");
    // timerTicks 0 while playing cannot happen, but must not wrap to a huge beat
    t = transportOf(120.0f, true, 0);
    check(t.playing == 0 && t.beat == 0.0f, "timerTicks 0 counts as stopped");
    // the wrap: PLUG_TEMPO_BEAT_WRAP beats, the beat just below it, then back to 0
    t = transportOf(120.0f, true, 1 + kBeatWrapTicks - 1);
    check(t.beat < PLUG_TEMPO_BEAT_WRAP && t.beat > PLUG_TEMPO_BEAT_WRAP - 0.001f, "the last tick before the wrap is just below 1024");
    t = transportOf(120.0f, true, 1 + kBeatWrapTicks);
    check(t.beat == 0.0f, "the wrap lands on 0");
    t = transportOf(120.0f, true, 0xFFFFFFFFu);
    check(t.beat >= 0.0f && t.beat < PLUG_TEMPO_BEAT_WRAP, "the largest tick count stays in range");
    // a plugin divides by the tempo
    check(transportOf(0.0f, true, 5).bpm == 120.0f && transportOf(-3.0f, false, 5).bpm == 120.0f, "a tempo that is not above 0 becomes 120");
    // every gate rate is a whole number of cycles in the wrap (the phase does not jump at the wrap): 16 slot cycles of 2, 4, 8, 6 slots per beat
    const float slotsPerBeat[] = {2, 4, 8, 6};
    for (float sp : slotsPerBeat)
    {
        const double cycles = double(PLUG_TEMPO_BEAT_WRAP) * sp / 16.0;
        char m[64];
        snprintf(m, sizeof m, "%g slots a beat: %g cycles in the wrap, a whole number", sp, cycles);
        check(cycles == std::floor(cycles), m);
    }
    // the beat is a float of the ticks, exact for every step boundary below 1024 beats
    bool exact = true;
    for (unsigned step = 0; step < 4096; step++)
        if (transportOf(120.0f, true, 1 + step * 576).beat != float(step) * 0.25f) exact = false;
    check(exact, "every step boundary of the first 1024 beats is an exact quarter beat");
    // the struct layout a plugin is built against
    check(offsetof(plug_ext_tempo, set_tempo) == sizeof(void*) && offsetof(plug_ext_tempo, set_transport) == 2 * sizeof(void*), "plug_ext_tempo: size, set_tempo, set_transport in order");
    printf("plug_transport_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
