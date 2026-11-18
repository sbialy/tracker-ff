#pragma once
// Pattern idea generator by track role (tracker-ff). A pure function: the same role, length, density, key, scale and seed always give
// the same steps, and nothing here touches the project, so the host test (synth-open/tools/pattern_gen_test.cpp) runs it alone.
// The Fill dialog (Note column, Fill Type "Role") calls it once per selected track, Sequencer::fillRoleNotes writes the result.
#include <stdint.h>

namespace patternGen
{
enum Role : uint8_t
{
    roleKick = 0,
    roleSnare,
    roleHat,
    roleBass,
    roleLead,
    roleAuto, // the selected tracks take Kick, Snare, Hat, Bass, Lead in order, then again
    roleCount
};
static constexpr uint8_t kPlainRoles = roleAuto; // the roles that have a rhythm of their own

struct Cell
{
    int8_t note;   // -1 no hit, drums: 0 = a hit that keeps the note of the step (the caller picks the default), bass and lead: 0..127
    int8_t volume; // 0..100 (the step volume FX scale), only read when note >= 0 or hit
    bool hit;
};

const char* roleName(uint8_t role);

// The role of a selected track: Auto walks the five roles by the position of the track in the selection, a plain role stays.
uint8_t roleOfTrack(uint8_t role, uint8_t trackInSelection);

// steps: how many steps to fill (1..128), cells[0..steps-1] are written. density 1..128: 64 plays the tables as designed, lower thins
// them out, higher thickens them, the anchors (the downbeat of a kick, the backbeat of a snare, the root of a bass) stay while
// density >= 8. root and scale are the pad board key and scale (scales.h), low is the lowest note of a bass or lead line, a role
// that is not pitched ignores all three.
void generate(uint8_t role,
              uint16_t steps,
              uint8_t density,
              uint8_t root,
              uint8_t scale,
              uint8_t low,
              uint32_t seed,
              Cell* cells);
} // namespace patternGen
