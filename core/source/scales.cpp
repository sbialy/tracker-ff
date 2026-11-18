#include "scales.h"
#include "config.h"
#include <stdint.h>

// return 1 if in the scale
AUDIO_CORE_RAM uint8_t isInScale(uint8_t note,  // note to check
                                 uint8_t root,  // root note
                                 uint8_t scale) // scale number
{

    if (scale > MAX_SCALE)
        scale = MAX_SCALE;

    uint8_t noteName = note % 12;
    uint8_t rootName = root % 12;

    if (noteName == rootName)
    {
        return 1;
    }
    else if (noteName > rootName)
    {
        if (scaleDef[scale] & (1 << (noteName - rootName)))
        {
            return 1;
        }
    }
    else
    {
        if (scaleDef[scale] & (1 << ((12 - rootName) + noteName)))
        {
            return 1;
        }
    }
    return 0;
}

#ifdef FEATURE_PLUG_HOST
uint8_t scaleSnapMode[kScaleSnapTracks];

// Every scale holds its root, so a scale note is at most 11 semitones away on each side (12 only for a note past the 0..127 edge, which
// the other direction then serves).
AUDIO_CORE_RAM uint8_t snapToScale(uint8_t note, uint8_t root, uint8_t scale, uint8_t mode)
{
    if (mode == kSnapOff || mode >= kSnapModeCount || note > 127 || isInScale(note, root, scale))
        return note;

    for (uint8_t d = 1; d < 12; d++)
    {
        const int dn = note - d;
        const int up = note + d;
        const bool okDn = dn >= 0 && isInScale(uint8_t(dn), root, scale);
        const bool okUp = up <= 127 && isInScale(uint8_t(up), root, scale);
        if (mode == kSnapNearest)
        {
            if (okDn)
                return uint8_t(dn);
            if (okUp)
                return uint8_t(up);
        }
        else if (mode == kSnapDown && okDn)
            return uint8_t(dn);
        else if (mode == kSnapUp && okUp)
            return uint8_t(up);
    }
    // Down with nothing below, Up with nothing above: the other side
    for (uint8_t d = 1; d < 12; d++)
    {
        const int dn = note - d;
        const int up = note + d;
        if (mode == kSnapDown && up <= 127 && isInScale(uint8_t(up), root, scale))
            return uint8_t(up);
        if (mode == kSnapUp && dn >= 0 && isInScale(uint8_t(dn), root, scale))
            return uint8_t(dn);
    }
    return note;
}
#endif
