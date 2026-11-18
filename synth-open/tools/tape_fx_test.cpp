// Host test of the Tape (Stop 1..64 / Start 65..128) arithmetic (core/source/tapeFx.h):
//   c++ -std=c++20 -O2 -Wall -I core/source synth-open/tools/tape_fx_test.cpp -o /tmp/tape_fx_test && /tmp/tape_fx_test
// The audio proof of the two FX on the firmware code is synth-open/tools/ui_shot/tape_fx_sim.sh.
#include "tapeFx.h"

#include <cmath>
#include <cstdio>

namespace
{
int fails = 0, checks = 0;
void check(bool ok, const char* what)
{
    checks++;
    if (!ok)
    {
        fails++;
        printf("FAIL  %s\n", what);
    }
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

int main()
{
    using namespace tapefx;
    // value domain: 0 reads as 1, 64 is the top, a quarter of a step a unit
    check(clampValue(0) == 1 && clampValue(1) == 1 && clampValue(64) == 64 && clampValue(65) == 65 && clampValue(128) == 128 && clampValue(129) == 128 && clampValue(255) == 128, "value clamp");
    // one FX: 1..64 Stop, 65..128 Start of v - 64 quarters, 129..255 reserved and clamped to 128
    check(!isStart(0) && !isStart(1) && !isStart(64) && isStart(65) && isStart(128) && isStart(129) && isStart(255), "direction");
    check(runValue(0) == 1 && runValue(1) == 1 && runValue(64) == 64 && runValue(65) == 1 && runValue(68) == 4 && runValue(128) == 64 && runValue(129) == 64 && runValue(255) == 64, "run length of either direction");
    check(kValueMin == 1 && kValueMax == 128 && kRunMax == 64, "editor range 1..128");
    check(letter(1) == 'W' && letter(64) == 'W' && letter(65) == 'Z' && letter(128) == 'Z', "grid letter");
    check(near(steps(72), 2.0f) && near(lengthMs(72, 125.0f), 250.0f) && near(lengthMs(8, 125.0f), lengthMs(72, 125.0f)), "a start of v - 64 runs as long as the stop of v - 64");
    check(near(steps(1), 0.25f) && near(steps(4), 1.0f) && near(steps(64), 16.0f) && near(steps(0), 0.25f), "value to steps");
    check(near(lengthMs(4, 125.0f), 125.0f) && near(lengthMs(8, 125.0f), 250.0f), "value to ms");
    check(lengthFrames(0.0f) == 1 && lengthFrames(250.0f) == uint32_t(250.0f * kFramesPerMs), "ms to frames");
    check(kValueDefault == 4, "default is one step");

    // the speed: Stop 1 -> 0, Start 0 -> 1, straight lines, off is 1
    check(near(ratio(kStop, 0, 1000), 1.0f) && near(ratio(kStop, 500, 1000), 0.5f) && near(ratio(kStop, 1000, 1000), 0.0f) && near(ratio(kStop, 5000, 1000), 0.0f), "stop ratio");
    check(near(ratio(kStart, 0, 1000), 0.0f) && near(ratio(kStart, 250, 1000), 0.25f) && near(ratio(kStart, 1000, 1000), 1.0f) && near(ratio(kStart, 9999, 1000), 1.0f), "start ratio");
    check(near(ratio(kOff, 10, 1000), 1.0f) && near(ratio(kStop, 10, 0), 1.0f), "off and empty run are speed 1");
    bool mono = true;
    for (uint32_t p = 0; p < 1000; p += 7)
    {
        mono = mono && ratio(kStop, p + 7, 1000) <= ratio(kStop, p, 1000) && ratio(kStart, p + 7, 1000) >= ratio(kStart, p, 1000);
    }
    check(mono, "ratio is monotonic");

    // the level: full from half speed, a straight fade below, 0 at standstill
    check(near(gainFor(1.0f), 1.0f) && near(gainFor(0.5f), 1.0f) && near(gainFor(0.25f), 0.5f) && near(gainFor(0.0f), 0.0f) && near(gainFor(-1.0f), 0.0f), "gain");

    // the pitch: -12 st at half speed, floor at -60 st (5 octaves), 0 at full speed
    check(near(semitonesFor(1.0f), 0.0f) && near(semitonesFor(0.5f), -12.0f) && near(semitonesFor(0.0f), -60.0f) && near(semitonesFor(kFloorRatio), -60.0f), "semitones");

    // a whole run as the synth part steps it: per block the end ratio, the level reaches 0 in the block where the run ends
    {
        const uint32_t total = lengthFrames(lengthMs(8, 125.0f)); // 2 steps at 120 BPM
        uint32_t pos = 0;
        int blocks = 0;
        float lastGain = 1.0f, lastSt = 0.0f;
        while (pos < total)
        {
            const float r = ratio(kStop, pos + kBlockFrames, total);
            lastGain = gainFor(r);
            lastSt = semitonesFor(r);
            pos += kBlockFrames;
            blocks++;
        }
        check(blocks == int((total + kBlockFrames - 1) / kBlockFrames), "run length in blocks");
        check(near(lastGain, 0.0f) && near(lastSt, -60.0f), "run ends silent at the floor");
    }

    // grid labels: whole steps, quarters
    check(labelWhole(4) == 1 && labelQuarter(4) == 0 && labelWhole(6) == 1 && labelQuarter(6) == 50 && labelWhole(1) == 0 && labelQuarter(1) == 25 && labelWhole(64) == 16, "labels");
    check(labelWhole(68) == 1 && labelQuarter(68) == 0 && labelWhole(70) == 1 && labelQuarter(70) == 50 && labelWhole(65) == 0 && labelQuarter(65) == 25 && labelWhole(128) == 16, "start labels read like the stop labels");

    // the load migration of the dev projects: id 51 (the retired Tape Start) -> id 50 with the value + 64, nothing else is touched
    {
        auto mig = [](uint8_t t, uint8_t v, uint8_t& ot, uint8_t& ov) { ot = t; ov = v; migrateSlot(ot, ov); };
        uint8_t t, v;
        mig(51, 8, t, v);
        check(t == 50 && v == 72, "migration: 51 value 8 -> 50 value 72");
        mig(51, 1, t, v);
        check(t == 50 && v == 65, "migration: 51 value 1 -> 65");
        mig(51, 64, t, v);
        check(t == 50 && v == 128, "migration: 51 value 64 -> 128");
        mig(51, 0, t, v);
        check(t == 50 && v == 65, "migration: 51 value 0 played as 1 -> 65");
        mig(51, 65, t, v);
        check(t == 50 && v == 128, "migration: 51 value 65 played as 64 -> 128");
        mig(51, 255, t, v);
        check(t == 50 && v == 128, "migration: 51 value 255 played as 64 -> 128");
        mig(50, 8, t, v);
        check(t == 50 && v == 8, "migration: 50 value 8 stays (a Stop)");
        mig(50, 72, t, v);
        check(t == 50 && v == 72, "migration: 50 value 72 stays (a Start of the new encoding)");
        mig(50, 200, t, v);
        check(t == 50 && v == 200, "migration: 50 value 200 untouched (reserved, clamped on playback)");
        mig(49, 8, t, v);
        check(t == 49 && v == 8, "migration: other ids untouched");
        mig(52, 3, t, v);
        check(t == 52 && v == 3, "migration: Snap (52) untouched");
        mig(51, 8, t, v);
        uint8_t t2 = t, v2 = v;
        migrateSlot(t2, v2);
        check(t2 == t && v2 == v, "migration is idempotent");
        // every value of the retired id plays as before: the old clamp (0 -> 1, 65.. -> 64) of the Start equals the new Start run
        bool same = true;
        for (int old = 0; old < 256; old++)
        {
            uint8_t ty = 51, vv = uint8_t(old);
            migrateSlot(ty, vv);
            const uint8_t oldRun = old < 1 ? 1 : (old > 64 ? 64 : old);
            same = same && ty == 50 && isStart(vv) && runValue(vv) == oldRun;
        }
        check(same, "migration: all 256 old Start values keep direction and run length");
        // every old Stop value 1..64 means the same
        bool sameStop = true;
        for (int old = 1; old <= 64; old++)
        {
            sameStop = sameStop && !isStart(uint8_t(old)) && runValue(uint8_t(old)) == old;
        }
        check(sameStop, "old Stop values 1..64 mean the same");
    }

    printf("%d checks\n", checks);
    printf(fails ? "RESULT: RED (%d failing)\n" : "RESULT: GREEN\n", fails);
    return fails ? 1 : 0;
}
