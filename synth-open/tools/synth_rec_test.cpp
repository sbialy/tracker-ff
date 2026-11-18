// Host test of the Synth play page REC capture (core/source/synthPlayRec.cpp): the mono mix, whole blocks, the stop at the capacity with the memory
// behind it untouched, a start on an odd halfword address, an idle capture that writes nothing, and the trailing silence trim. Built plain and with
// ASan/UBSan by run_tests.sh:
//   c++ -std=c++20 -DSYNTHPLAYREC_TEST -I core/source synth-open/tools/synth_rec_test.cpp core/source/synthPlayRec.cpp
#include "synthPlayRec.h"

#include <stdio.h>
#include <string.h>
#include <vector>

using namespace synthplayrec;

namespace
{
int g_fail;
#define CHECK(c)                                               \
    do                                                         \
    {                                                          \
        if (!(c))                                              \
        {                                                      \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                          \
        }                                                      \
    } while (0)

constexpr int16_t kGuard = 0x5A5A;

// one block of a ramp: left k * 3 + base, right k * 5 - base
void blockOf(int base, int16_t* l, int16_t* r)
{
    for (uint32_t k = 0; k < kBlockFrames; k++)
    {
        l[k] = int16_t(int(k) * 3 + base);
        r[k] = int16_t(int(k) * 5 - base);
    }
}

void testBasic()
{
    std::vector<int16_t> pool(kBlockFrames * 8 + 16, kGuard);
    int16_t l[kBlockFrames], r[kBlockFrames];
    blockOf(0, l, r);
    CHECK(state() == kIdle);
    block(l, r);                                  // idle: nothing is written
    for (int16_t v : pool) CHECK(v == kGuard);

    begin(pool.data() + 8, kBlockFrames * 3 + 77);   // 3 whole blocks and a rest that is cut off
    CHECK(state() == kRecording && capacity() == kBlockFrames * 3 && frames() == 0);
    for (int b = 0; b < 3; b++)
    {
        blockOf(b * 10, l, r);
        block(l, r);
        CHECK(frames() == uint32_t(b + 1) * kBlockFrames);
        CHECK(state() == (b == 2 ? kFull : kRecording));   // the last whole block ends the take
    }
    blockOf(99, l, r);
    block(l, r);                                  // full: the memory behind the take is left alone
    CHECK(frames() == kBlockFrames * 3);
    for (int b = 0; b < 3; b++)
        for (uint32_t k = 0; k < kBlockFrames; k++)
        {
            const int L = int(k) * 3 + b * 10, R = int(k) * 5 - b * 10;
            CHECK(pool[8 + b * kBlockFrames + k] == int16_t((L + R) >> 1));
        }
    for (int i = 0; i < 8; i++) CHECK(pool[i] == kGuard);                                             // before the take
    for (size_t i = 8 + kBlockFrames * 3; i < pool.size(); i++) CHECK(pool[i] == kGuard);            // behind it
    CHECK(end() == kBlockFrames * 3 && state() == kIdle);
}

void testLimits()
{
    std::vector<int16_t> pool(kBlockFrames * 2 + 4, kGuard);
    int16_t l[kBlockFrames], r[kBlockFrames];
    blockOf(1, l, r);
    begin(pool.data() + 1, kBlockFrames - 1);     // less than a block: no take starts
    CHECK(state() == kIdle && capacity() == 0);
    block(l, r);
    for (int16_t v : pool) CHECK(v == kGuard);
    begin(pool.data() + 1, kBlockFrames);         // exactly one block: it is the first and the last
    block(l, r);
    CHECK(state() == kFull && frames() == kBlockFrames);
    CHECK(pool[0] == kGuard && pool[1 + kBlockFrames] == kGuard);   // an odd halfword start, nothing around it touched
    CHECK(end() == kBlockFrames);
    // a new take restarts from 0
    begin(pool.data(), kBlockFrames * 2);
    CHECK(frames() == 0 && state() == kRecording);
    CHECK(end() == 0 && state() == kIdle);
    // full scale: (L + R) / 2 never overflows
    for (uint32_t k = 0; k < kBlockFrames; k++) l[k] = r[k] = (k & 1) ? 32767 : -32768;
    begin(pool.data(), kBlockFrames);
    block(l, r);
    CHECK(pool[0] == -32768 && pool[1] == 32767);
    end();
}

void testTrim()
{
    const int16_t a[] = { 100, -200, 30, 0, -32, 32, 0 };
    CHECK(trimSilence(a, 7) == 2);                 // 30, 0, -32, 32, 0 are at or below -60 dBFS
    const int16_t b[] = { 0, 1, -2, 3 };
    CHECK(trimSilence(b, 4) == 0);                 // all silent
    const int16_t c[] = { 0, 0, 33 };
    CHECK(trimSilence(c, 3) == 3);                 // 33 is above
    const int16_t d[] = { -33, 0 };
    CHECK(trimSilence(d, 2) == 1);
    CHECK(trimSilence(d, 0) == 0);
}
}   // namespace

int main()
{
    testBasic();
    testLimits();
    testTrim();
    if (g_fail)
    {
        printf("synth_rec_test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("synth_rec_test: all passed\n");
    return 0;
}
