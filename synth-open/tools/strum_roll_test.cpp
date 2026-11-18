// Host test of the hand roll helpers (core/source/mtRoll.h, tracker-ff): the Strum FX value that record join and Capture write beside a
// recognised MIDI Chord, and how it goes into the other slot of the pair. NOT firmware.
//   c++ -std=c++20 -O1 -Wall -Wextra -I core/source synth-open/tools/strum_roll_test.cpp -o strum_roll_test && ./strum_roll_test
// The ui_shot side (the real join and the real Capture) is synth-open/tools/ui_shot/strum_recog_sim.sh.
#include "mtRoll.h"

#include <cstdint>
#include <cstdio>

namespace
{
int g_checks = 0, g_fails = 0;
void expect(const char* what, long want, long got)
{
    g_checks++;
    if (want != got)
    {
        g_fails++;
        printf("FAIL  %s: expected %ld, got %ld\n", what, want, got);
    }
}

constexpr uint32_t kUnit = 2000;   // Sequencer::kStrumUnitUs
constexpr uint8_t kStrum = 47;     // FX_TYPE_STRUM (the value only has to differ from the chord and the other FX below)
constexpr uint8_t kChord = 14;     // FX_TYPE_MIDI_CHORD
constexpr uint8_t kChance = 4;

struct Fx
{
    uint8_t type, value;
};

uint8_t value(uint8_t a, uint8_t b, uint8_t n, uint32_t spanUs) { return handroll::strumValue(a, b, n, spanUs, kUnit); }
} // namespace

int main()
{
    // the dead zone: a mean gap under 15 ms writes nothing, 15 ms is the first roll (7.5 units, rounded up to 8)
    expect("2 notes 13 ms apart (USB spread): none", 0, value(60, 67, 2, 13000));
    expect("3 notes 13 ms apart: none", 0, value(60, 67, 3, 26000));
    expect("4 notes 14.9 ms apart: none", 0, value(60, 72, 4, 44700));
    expect("2 notes 14,999 us: none", 0, value(60, 64, 2, 14999));
    expect("2 notes 15 ms: first roll, 8 units up", 8, value(60, 64, 2, 15000));
    expect("1 note: none", 0, value(60, 60, 1, 0));
    expect("0 notes: none", 0, value(0, 0, 0, 0));
    // the mean gap is span / (n - 1), rounded to units
    expect("slow roll 40 ms: 20 units up", 20, value(60, 67, 2, 40000));
    expect("3 notes 40 ms apart: 20", 20, value(60, 67, 3, 80000));
    expect("4 notes 30 ms apart: 15", 15, value(60, 71, 4, 90000));
    expect("4 notes 90 ms span, mean 30 ms: 15", 15, value(60, 71, 4, 90000));
    expect("rounds to nearest: 41 ms is 20.5 units, up to 21", 21, value(60, 67, 2, 41000));
    expect("rounds to nearest: 40.9 ms is 20", 20, value(60, 67, 2, 40999));
    expect("8 notes 100 ms apart (the window): 50", 50, value(60, 84, 8, 700000));
    // the direction: first note above the last = down (64 + units), otherwise up, an equal pair up
    expect("down roll 40 ms: 64 + 20", 84, value(67, 60, 2, 40000));
    expect("down roll below the dead zone: none", 0, value(67, 60, 2, 13000));
    expect("up roll, middle note ignored (C G E)", 20, value(60, 64, 3, 80000));
    expect("down roll by first and last (G C E is up: C < E)", 20, value(67, 71, 3, 80000));
    expect("equal first and last: up", 20, value(60, 60, 2, 40000));
    // the clamp at 63 units (a mean gap of 126 ms or more) in both directions, and its edges
    expect("62 units: 124 ms", 62, value(60, 67, 2, 124000));
    expect("63 units: 126 ms", 63, value(60, 67, 2, 126000));
    expect("clamp: 130 ms is 63", 63, value(60, 67, 2, 130000));
    expect("clamp: 2 s is 63", 63, value(60, 67, 2, 2000000));
    expect("clamp down: 64 + 63 = 127", 127, value(67, 60, 2, 500000));
    expect("clamp keeps the value a legal Strum (never 64, never over 127)", 1, value(60, 67, 8, 4000000000u) <= 127 && value(60, 67, 8, 4000000000u) != 64);

    // the slot: the other slot of the pair of the chord
    {
        Fx fx[2] = {{kChord, 5}, {0, 0}};
        expect("empty partner takes the Strum (chord in 0, Strum in 1)", 1, handroll::strumWrite(fx, 0, 20, false, kStrum));
        expect("... type", kStrum, fx[1].type);
        expect("... value", 20, fx[1].value);
        expect("... the chord stays", kChord, fx[0].type);
    }
    {
        Fx fx[2] = {{0, 0}, {kChord, 5}};
        expect("chord in 1: the Strum goes to 0", 1, handroll::strumWrite(fx, 1, 84, false, kStrum));
        expect("... type", kStrum, fx[0].type);
        expect("... value", 84, fx[0].value);
    }
    {   // a busy partner: the chord stays, the Strum is skipped, the other FX is never overwritten
        Fx fx[2] = {{kChord, 5}, {kChance, 50}};
        expect("busy partner: not written", 0, handroll::strumWrite(fx, 0, 20, false, kStrum));
        expect("... the other FX type stays", kChance, fx[1].type);
        expect("... the other FX value stays", 50, fx[1].value);
        expect("... the chord stays", kChord, fx[0].type);
        expect("busy partner, own = true (not our Strum): still not written", 0, handroll::strumWrite(fx, 0, 20, true, kStrum));
        expect("... still the other FX", kChance, fx[1].type);
    }
    {   // a Strum that is not this group's (own false) is kept as it is
        Fx fx[2] = {{kChord, 5}, {kStrum, 10}};
        expect("foreign Strum: not touched", 0, handroll::strumWrite(fx, 0, 20, false, kStrum));
        expect("... value stays", 10, fx[1].value);
        expect("foreign Strum, roll gone: not cleared", 0, handroll::strumWrite(fx, 0, 0, false, kStrum));
        expect("... type stays", kStrum, fx[1].type);
    }
    {   // the group's own Strum is updated with every joined note and cleared when the roll drops under the dead zone
        Fx fx[2] = {{kChord, 5}, {0, 0}};
        bool own = handroll::strumWrite(fx, 0, 20, false, kStrum);
        expect("own: written", 1, own);
        own = handroll::strumWrite(fx, 0, 17, own, kStrum);
        expect("own: updated, still ours", 1, own);
        expect("own: new value", 17, fx[1].value);
        own = handroll::strumWrite(fx, 0, 84, own, kStrum);
        expect("own: direction flips with the notes", 84, fx[1].value);
        own = handroll::strumWrite(fx, 0, 0, own, kStrum);
        expect("own: cleared under the dead zone, no longer ours", 0, own);
        expect("own: type cleared", 0, fx[1].type);
        expect("own: value cleared", 0, fx[1].value);
        expect("own: the chord stays", kChord, fx[0].type);
        own = handroll::strumWrite(fx, 0, 12, own, kStrum);
        expect("after the clear a new roll value may write again", 1, own);
        expect("... value", 12, fx[1].value);
    }
    {   // no roll, nothing there: nothing is written
        Fx fx[2] = {{kChord, 5}, {0, 0}};
        expect("value 0, empty partner: nothing", 0, handroll::strumWrite(fx, 0, 0, false, kStrum));
        expect("... type 0", 0, fx[1].type);
        expect("no chord slot (-1): nothing", 0, handroll::strumWrite(fx, -1, 20, false, kStrum));
        expect("... type 0", 0, fx[1].type);
        expect("... and 0 untouched", kChord, fx[0].type);
    }
    printf("%d checks\n", g_checks);
    if (g_fails)
    {
        printf("RESULT: RED (%d failing)\n", g_fails);
        return 1;
    }
    printf("RESULT: GREEN\n");
    return 0;
}
