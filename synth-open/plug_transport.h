// The sequencer's clock for the master inserts that follow it (PLUG_EXT_TEMPO, plug_abi.h). Header only, no firmware types: the
// host test (tools/plug_transport_test.cpp) includes it alone.
#pragma once

#include "plug/plug_abi.h"
#include <stdint.h>

namespace plug
{

// Sequencer::timerTicks ticks in one beat: 4 steps (kStepsPerBeat) of 576 ticks (kTicksInStep = 48 uSteps x 12 nano steps),
// core/source/mtSequencer.h. The ui_shot scenario plug_tempo_sim.sh plays the real sequencer and checks the beat against
// the elapsed time, so a change of the clock resolution fails there.
constexpr uint32_t kTicksInBeat = 4u * 576u;
constexpr uint32_t kBeatWrapTicks = kTicksInBeat * 1024u;   // PLUG_TEMPO_BEAT_WRAP beats
static_assert(PLUG_TEMPO_BEAT_WRAP == 1024.0f, "kBeatWrapTicks follows PLUG_TEMPO_BEAT_WRAP");

struct Transport
{
    float bpm;
    float beat;
    uint32_t playing;
};

// The values of one block. timerTicks is 1 at the first tick after Play (Sequencer::play), so beat 0 is the downbeat. A stopped
// sequencer (or a tempo that is not a number above 0: it never is, but a plugin divides by it) gives beat 0 and the tempo of the
// project.
inline Transport transportOf(float bpm, bool playing, uint32_t timerTicks)
{
    Transport t;
    t.bpm = bpm > 0.0f ? bpm : 120.0f;
    t.playing = playing && timerTicks > 0 ? 1u : 0u;
    t.beat = t.playing ? float((timerTicks - 1u) % kBeatWrapTicks) * (1.0f / float(kTicksInBeat)) : 0.0f;
    return t;
}

}   // namespace plug
