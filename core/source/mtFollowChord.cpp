// Follow chord (tracker-ff): the note mapping and the sidecar text. No card or sequencer access here (the host test
// synth-open/tools/follow_chord_test.cpp builds this file alone). mtFollowChord.h has the rules.
#include "mtFollowChord.h"

#include "mtChordDefs.h"
#include "../../synth-open/plug/plug_chord.h"

#include <stdio.h>
#include <string.h>

namespace followchord
{

Setting setting;
Live live;
uint16_t sourceMask;

namespace
{
const char kModeNames[kModeCount][8] = { "Off", "Snap", "Transp", "Tr+Snap" };

int clampNote(int n)
{
    return n < 0 ? 0 : (n > 127 ? 127 : n);
}

int pc(int note)
{
    return ((note % 12) + 12) % 12;
}

int snap(int note, uint16_t pcs)
{
    // the nearest chord note, down first on a tie (6 semitones at most). At the edge of the range it may be up to 11 the other way
    for (int d = 0; d <= 11; d++)
    {
        if (note - d >= 0 && (pcs >> pc(note - d)) & 1)
            return note - d;
        if (note + d <= 127 && (pcs >> pc(note + d)) & 1)
            return note + d;
    }
    return note;
}
} // namespace

uint16_t chordPitchClasses(int note, uint8_t type)
{
    const uint16_t mask = type <= MAX_CHORD ? chordDef[type] : 1;
    uint16_t pcs = 0;
    for (int b = 0; b < 16; b++)
    {
        if ((mask >> b) & 1)
            pcs |= uint16_t(1u << pc(note + b));
    }
    return pcs;
}

uint8_t chordRoot(int note, uint8_t type)
{
    return uint8_t(pc(note + (type <= MAX_CHORD ? plugchord::kRoot[type] : 0)));
}

int8_t mapNote(int note, uint8_t mode, uint16_t pcs, uint8_t root)
{
    if (pcs == 0 || mode == kOff || mode >= kModeCount)
        return int8_t(note);
    int n = note;
    if (mode == kTranspose || mode == kTransposeSnap)
    {
        int shift = root % 12;
        if (shift > 6)
            shift -= 12;
        n = clampNote(n + shift);
    }
    if (mode == kSnap || mode == kTransposeSnap)
        n = snap(n, pcs);
    return int8_t(n);
}

void recalc()
{
    uint16_t m = 0;
    for (int t = 0; t < kTracks; t++)
    {
        if (setting.mode[t] >= kModeCount || setting.source[t] >= kTracks || setting.source[t] == t)
            setting.mode[t] = kOff;
        if (setting.mode[t] != kOff)
            m |= uint16_t(1u << setting.source[t]);
    }
    sourceMask = m;
}

void resetLive()
{
    memset(&live, 0, sizeof(live));
    for (int t = 0; t < kTracks; t++)
        live.lastType[t] = kMajorTriad;
}

bool anyFollower()
{
    return sourceMask != 0;
}

const char* modeName(uint8_t mode)
{
    return mode < kModeCount ? kModeNames[mode] : "?";
}

} // namespace followchord
