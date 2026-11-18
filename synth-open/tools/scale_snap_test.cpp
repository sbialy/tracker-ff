// Host test of the scale snap function (core/source/scales.cpp snapToScale), every note x root x scale x mode against a brute force.
//
//   c++ -std=c++20 -O1 -Wall -DFEATURE_PLUG_HOST -DAUDIO_CORE_RAM= -I synth-open/tools/scale_stub -I core/source \
//       synth-open/tools/scale_snap_test.cpp core/source/scales.cpp -o /tmp/scale_snap_test && /tmp/scale_snap_test
//
// Properties: Off, a note already in the scale, a note above 127 and a mode past Up change nothing. Every result is a scale note in
// 0..127 (every scale holds its root, so each pitch class set of a scale reaches both ends of the range). Nearest has no scale note
// closer and takes the lower one on a tie. Down has no scale note between result and note and falls back to the nearest above only when
// there is none at or below. Up the mirror. Snapping twice gives the same as once.
#include "scales.h"

#include <cstdio>
#include <cstdlib>

namespace
{
int g_fail = 0, g_checks = 0;
void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("FAIL  %s\n", what);
    }
}
bool in(int n, int root, int scale) { return n >= 0 && n <= 127 && isInScale(uint8_t(n), uint8_t(root), uint8_t(scale)); }
}

int main()
{
    int bad[8] = {};
    long cases = 0;
    for (int scale = 0; scale <= MAX_SCALE; scale++)
        for (int root = 0; root < 48; root++)
            for (int note = 0; note < 128; note++)
            {
                const bool inScale = in(note, root, scale);
                // brute force: nearest below, nearest above
                int dn = -1, up = -1;
                for (int k = note; k >= 0 && dn < 0; k--) if (in(k, root, scale)) dn = k;
                for (int k = note; k <= 127 && up < 0; k++) if (in(k, root, scale)) up = k;
                for (int mode = 0; mode < kSnapModeCount + 2; mode++)
                {
                    cases++;
                    const int r = snapToScale(uint8_t(note), uint8_t(root), uint8_t(scale), uint8_t(mode));
                    if (mode == kSnapOff || mode >= kSnapModeCount || inScale) { if (r != note) bad[0]++; continue; }
                    if (!in(r, root, scale)) { bad[1]++; continue; }
                    int want;
                    if (mode == kSnapNearest)
                        want = dn < 0 ? up : (up < 0 ? dn : (note - dn <= up - note ? dn : up));
                    else if (mode == kSnapDown)
                        want = dn >= 0 ? dn : up;
                    else
                        want = up >= 0 ? up : dn;
                    if (r != want) bad[2]++;
                    if (snapToScale(uint8_t(r), uint8_t(root), uint8_t(scale), uint8_t(mode)) != r) bad[3]++;
                }
            }
    check(bad[0] == 0, "Off, in-scale notes and a mode past Up change nothing");
    check(bad[1] == 0, "every result is a scale note in 0..127");
    check(bad[2] == 0, "Nearest (tie down), Down and Up match the brute force");
    check(bad[3] == 0, "snapping twice equals snapping once");
    check(snapToScale(200, 24, 2, kSnapNearest) == 200, "a note above 127 is left alone");
    check(snapToScale(49, 24, 2, kSnapNearest) == 48, "C# in C Major goes down to C (tie)");
    check(snapToScale(0, 11, 2, kSnapDown) == 1, "Down with no scale note at or below goes up (B Major has no C: 0 -> 1)");
    check(snapToScale(127, 6, 2, kSnapUp) == 126, "Up with no scale note above goes down (F# Major has no G: 127 -> 126)");
    int off = 0;
    for (int t = 0; t < kScaleSnapTracks; t++) off += scaleSnapMode[t];
    check(off == 0, "the mode table starts all Off");
    printf("%d checks, %ld snap cases, %d failing\n", g_checks, cases, g_fail);
    printf(g_fail ? "RESULT: RED\n" : "RESULT: GREEN\n");
    return g_fail ? 1 : 0;
}
