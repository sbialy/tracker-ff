// Host test of the Follow chord note mapping and sidecar text (core/source/mtFollowChord.cpp, built alone). Exhaustive checks over
// every note, every chord entry and every bass: Snap lands on a chord note at most 6 semitones away and nearer than any other, Transp
// moves by the root, Off and "no chord yet" leave the note alone. Prints "follow_chord_test: all passed" or the failures (exit 1).
//   c++ -std=c++20 -I core/source -I core/include -I core/config synth-open/tools/follow_chord_test.cpp core/source/mtFollowChord.cpp
#include "mtFollowChord.h"
#include "mtChordDefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace followchord;

static int s_fail;
#define CHECK(c, ...)                                                                                                                      \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(c) && s_fail++ < 20)                                                                                                         \
        {                                                                                                                                  \
            printf("FAIL %s:%d ", __FILE__, __LINE__);                                                                                     \
            printf(__VA_ARGS__);                                                                                                           \
            printf("\n");                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

static bool inSet(int n, uint16_t pcs) { return (pcs >> (n % 12)) & 1; }

int main()
{
    // the chord sets of a few known entries
    CHECK(chordPitchClasses(48, 5) == ((1 << 0) | (1 << 4) | (1 << 7)), "C maj");
    CHECK(chordPitchClasses(57, 4) == ((1 << 9) | (1 << 0) | (1 << 4)), "A m");
    CHECK(chordPitchClasses(52, 30) == ((1 << 4) | (1 << 7) | (1 << 0)), "C maj inv1 on E");
    CHECK(chordRoot(52, 30) == 0, "inv1 root is C");
    CHECK(chordRoot(55, 31) == 0, "inv2 on G root is C");
    CHECK(chordPitchClasses(60, 200) == 1, "past the table: the note alone");

    for (int type = 0; type <= MAX_CHORD; type++)
    {
        for (int bass = 0; bass < 128; bass++)
        {
            const uint16_t pcs = chordPitchClasses(bass, uint8_t(type));
            const uint8_t root = chordRoot(bass, uint8_t(type));
            CHECK(inSet(bass, pcs), "bass in its chord type %d", type);
            CHECK(inSet(root, pcs), "root in its chord type %d bass %d", type, bass);
            for (int n = 0; n < 128; n++)
            {
                CHECK(mapNote(n, kOff, pcs, root) == n, "off");
                CHECK(mapNote(n, kSnap, 0, root) == n, "no chord yet");
                const int s = mapNote(n, kSnap, pcs, root);
                CHECK(s >= 0 && s <= 127 && inSet(s, pcs), "snap lands on a chord note");
                const int d = abs(s - n);
                CHECK(d <= 6 || n < 6 || n > 121, "snap distance %d", d);
                for (int k = 0; k < 128; k++)
                    if (inSet(k, pcs) && abs(k - n) < d)
                        CHECK(false, "snap %d -> %d, %d is nearer (type %d)", n, s, k, type);
                if (inSet(n, pcs))
                    CHECK(s == n, "a chord note stays");
                int shift = root > 6 ? root - 12 : root;
                int t = n + shift;
                t = t < 0 ? 0 : (t > 127 ? 127 : t);
                CHECK(mapNote(n, kTranspose, pcs, root) == t, "transp");
                const int ts = mapNote(n, kTransposeSnap, pcs, root);
                CHECK(ts == mapNote(t, kSnap, pcs, root), "tr+snap is snap after transp");
            }
        }
    }
    // a tie goes down: D between C and E (C maj)
    CHECK(mapNote(50, kSnap, chordPitchClasses(48, 5), 0) == 48, "tie down");

    // the source of a follower is never the follower itself (the project.mtff key is checked by plug_mtff_test)
    memset(&setting, 0, sizeof(setting));
    setting.mode[1] = kSnap;
    setting.source[1] = 0;
    setting.mode[15] = kTransposeSnap;
    setting.source[15] = 14;
    setting.mode[3] = kTranspose;
    setting.source[3] = 3; // itself: dropped
    recalc();
    CHECK(setting.mode[3] == kOff, "self source dropped");
    CHECK(sourceMask == ((1 << 0) | (1 << 14)), "source mask");

    if (s_fail)
    {
        printf("follow_chord_test: %d failures\n", s_fail);
        return 1;
    }
    printf("follow_chord_test: all passed\n");
    return 0;
}
