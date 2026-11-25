// Host test of the Parts data model and song order (core/source/parts_format.h, the very header the firmware uses).
//   c++ -std=c++20 -O1 -Wall -I core/source synth-open/tools/parts_format_test.cpp -o /tmp/parts_format_test && /tmp/parts_format_test
#include "parts_format.h"

#include <stddef.h>
#include <stdio.h>

static int g_fail;
#define CHECK(c)                                                                                                                   \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!(c))                                                                                                                  \
        {                                                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                                     \
            g_fail++;                                                                                                              \
        }                                                                                                                          \
    } while (0)

using namespace parts;

static List g_a;

int main()
{
    memset(&g_a, 0, sizeof(g_a));
    // the record is the 20 bytes the text codec of mtff_codec.h writes field by field (plug_mtff_test checks that round trip)
    CHECK(sizeof(Part) == kRecordSize && offsetof(Part, length) == kTracks && offsetof(Part, baked) == kTracks + 3);

    // song order: End stops the song, nextIndex wraps by returning -1
    memset(&g_a, 0, sizeof(g_a));
    g_a.count = 4;
    for (int i = 0; i < 4; i++) partInit(g_a.part[i], 1);
    g_a.part[2].repeats = 0;
    CHECK(songLength(g_a) == 2);
    CHECK(nextIndex(g_a, 0) == 1 && nextIndex(g_a, 1) == -1 && nextIndex(g_a, 2) == 3 && nextIndex(g_a, 3) == -1);

    // Loop Song is the flag of Part 0 only: songLoops reads it, an empty list never loops
    CHECK(!songLoops(g_a));
    g_a.part[0].flags |= kFlagLoop;
    CHECK(songLoops(g_a));
    g_a.part[1].flags |= kFlagLoop;   // set on another Part it is ignored
    g_a.part[0].flags &= uint8_t(~kFlagLoop);
    CHECK(!songLoops(g_a));
    List empty;
    memset(&empty, 0, sizeof(empty));
    empty.part[0].flags = kFlagLoop;
    CHECK(!songLoops(empty));

    // tiling: a 16 step source under a 64 step Part
    CHECK(sourceStep(0, 16, 64, true) == 0 && sourceStep(17, 16, 64, true) == 1 && sourceStep(63, 16, 64, true) == 15);
    CHECK(sourceStep(17, 16, 64, false) == -1 && sourceStep(64, 16, 64, true) == -1 && sourceStep(5, 16, 8, true) == 5);

    // start step: the window start..end plays and tiles, not the whole source
    CHECK(sourceStep(0, 32, 16, true, 16) == 16 && sourceStep(15, 32, 16, true, 16) == 31 && sourceStep(16, 32, 16, true, 16) == -1);
    CHECK(sourceStep(16, 32, 48, true, 16) == 16 && sourceStep(31, 32, 48, true, 16) == 31 && sourceStep(47, 32, 48, true, 16) == 31);
    CHECK(sourceStep(20, 32, 64, true, 28) == 28 + 20 % 4 && sourceStep(4, 32, 64, true, 28) == 28);   // a 4 step window tiles 16 times
    CHECK(sourceStep(16, 32, 48, false, 16) == -1 && sourceStep(15, 32, 48, false, 16) == 31);
    CHECK(sourceStep(3, 16, 16, true, 16) == 3 && sourceStep(3, 16, 16, true, 99) == 3);   // a start at or past the end: the whole source
    CHECK(sourceStep(5, 16, 8, true, 0) == 5 && sourceStep(5, 16, 8, true) == 5);          // start 0 is the old behaviour
    CHECK(windowLen(32, 16) == 16 && windowLen(32, 0) == 32 && windowLen(16, 16) == 16 && windowLen(16, 127) == 16 && windowLen(128, 127) == 1);
    // the start table is part of the list: one row of 16 a Part
    CHECK(sizeof(g_a.start) == size_t(kMax) * kTracks && kMaxStart == 127);

    printf("parts_format_test: %s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
