#include "scales.h"
#include "config.h"
#include "mtTuning.h"
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

// A tuning of N notes per period (phase 3, mtTuning.h snapDegrees): the scale is a mask of N degrees, rooted at the 1/1 key (the pad root is a 12 note name),
// built from the 12 note scale of the project and rebuilt when N or the scale changes. 128 bits are the 128 keys, so any N fits
static uint32_t s_mask[4];
static uint8_t s_maskN, s_maskScale = 0xFF;
static bool inDegrees(uint8_t note, uint8_t scale)
{
    const uint8_t n = tuning::cfg.n;
    if (n != s_maskN || scale != s_maskScale)
    {
        for (uint32_t& w : s_mask) w = 0;
        for (int s = 0; s < 12; s++)
            if (scale == 0)
            {   // Chromatic: every degree of the tuning is in
                for (int d = 0; d < n; d++) s_mask[d >> 5] |= 1u << (d & 31);
                break;
            }
            else if (scaleDef[scale > MAX_SCALE ? MAX_SCALE : scale] & (1 << s))
            {
                const int d = tuning::semiDegree(s, n);
                s_mask[d >> 5] |= 1u << (d & 31);
            }
        s_maskN = n;
        s_maskScale = scale;
    }
    const int d = ((note - int(tuning::cfg.root)) % n + n) % n;
    return (s_mask[d >> 5] >> (d & 31)) & 1;
}
bool inProjectScale(uint8_t note, uint8_t root, uint8_t scale)
{
    return tuning::snapDegrees() ? inDegrees(note, scale) : isInScale(note, root, scale);
}

#ifdef FEATURE_FF_CORE
uint8_t scaleSnapMode[kScaleSnapTracks];

// Every scale holds its root, so a scale note is at most 11 semitones away on each side (12 only for a note past the 0..127 edge, which
// the other direction then serves). In a tuning of N notes a scale note is at most N - 1 keys away.
AUDIO_CORE_RAM uint8_t snapToScale(uint8_t note, uint8_t root, uint8_t scale, uint8_t mode)
{
    const bool deg = tuning::snapDegrees();
    const uint8_t lim = deg ? tuning::cfg.n : 12;
    auto in = [&](uint8_t k) { return deg ? inDegrees(k, scale) : isInScale(k, root, scale) != 0; };
    if (mode == kSnapOff || mode >= kSnapModeCount || note > 127 || in(note))
        return note;

    for (uint8_t d = 1; d < lim; d++)
    {
        const int dn = note - d;
        const int up = note + d;
        const bool okDn = dn >= 0 && in(uint8_t(dn));
        const bool okUp = up <= 127 && in(uint8_t(up));
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
    for (uint8_t d = 1; d < lim; d++)
    {
        const int dn = note - d;
        const int up = note + d;
        if (mode == kSnapDown && up <= 127 && in(uint8_t(up)))
            return uint8_t(up);
        if (mode == kSnapUp && dn >= 0 && in(uint8_t(dn)))
            return uint8_t(dn);
    }
    return note;
}
#endif
