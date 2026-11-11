// When the master inserts of a project load (plug_host.cpp), kept free of the firmware so the host test
// (synth-open/tools/plug_defer_test.cpp) runs it on the Mac.
//
// The synth engines wait before they install: a Model pick rests kModelRestMs on the list before its engine installs
// (SynthEditor.cpp synthModelTick), so the install never runs in the moment the list moves. The inserts of an opened project (or of the
// project that is up at power on) take the same road: they wait kOpenRestMs after the load finished, and then load ONE per
// main loop pass, kStaggerMs apart, in slot order. Insert 1 and insert 2 therefore never read the card in the same pass, and neither reads
// it in the pass that finished the load (patterns, synth patches, the UI switching to the pattern editor).
// A pick on the Inserts page (assign) is not part of this: it keeps its short debounce and loads alone.
#pragma once

#include <stdint.h>

namespace plug
{
constexpr uint32_t kOpenRestMs = 1000;   // after the project load, as the synth model list rests (kModelRestMs)
constexpr uint32_t kStaggerMs = 250;     // between two inserts of the same load

struct OpenGate
{
    uint32_t nextMs = 0;   // earliest time of the next load that comes from a project load

    void arm(uint32_t nowMs) { nextMs = nowMs + kOpenRestMs; }                 // the project load finished
    bool ready(uint32_t nowMs) const { return int32_t(nowMs - nextMs) >= 0; }   // wrap safe
    void took(uint32_t nowMs) { nextMs = nowMs + kStaggerMs; }                  // one insert loaded
};
}   // namespace plug
