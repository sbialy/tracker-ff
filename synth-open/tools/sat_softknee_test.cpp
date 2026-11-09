// Host test of the Soft Knee mixer saturation curve (synth-open/sat_softknee.h, SaturationType::kSoftKnee in saturate()).
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/sat_softknee_test.cpp -o /tmp/sat_softknee_test && /tmp/sat_softknee_test
//
// Every int32 input in [-2^24, 2^24] (far above any mixer sum: 8 tracks plus 2 returns at full scale are under 2^19) and the
// extremes up to INT32_MIN and INT32_MAX: passthrough below the knee, continuous (steps of at most 1 between neighbouring
// inputs), monotonic, odd, never outside +-32767, slope 1 at the knee.
#include "sat_softknee.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}
double db(int y)
{
    return 20.0 * std::log10(double(y) / 32767.0);
}
}   // namespace

int main()
{
    using softknee::apply;
    using softknee::kKnee;
    constexpr int32_t kSpan = 1 << 24;

    bool pass = true, mono = true, cont = true, odd = true, bound = true;
    int maxStep = 0;
    int32_t prevY = apply(-kSpan);
    for (int32_t x = -kSpan; x <= kSpan; x++)
    {
        const int32_t y = apply(x);
        const int32_t a = x < 0 ? -x : x;
        if (a <= kKnee && y != x) pass = false;
        if (y > 32767 || y < -32767) bound = false;
        if (apply(-x) != -y) odd = false;
        if (x > -kSpan)
        {
            if (y < prevY) mono = false;
            const int step = int(y - prevY);
            if (step > maxStep) maxStep = step;
            if (step > 1) cont = false;
        }
        prevY = y;
    }
    check(pass, "bit exact passthrough for |x| <= 23197 (-3.0 dBFS)");
    check(cont, "continuous: neighbouring inputs differ by at most 1 at the output");
    check(mono, "monotonic over [-2^24, 2^24]");
    check(odd, "odd: f(-x) = -f(x)");
    check(bound, "never outside +-32767 over [-2^24, 2^24]");

    const int32_t ext[] = {INT32_MIN, INT32_MIN + 1, -(1 << 30), (1 << 30), INT32_MAX - 1, INT32_MAX};
    bool extOk = true;
    int32_t last = -32768;
    for (int32_t x : ext)
    {
        const int32_t y = apply(x);
        if (y < -32767 || y > 32767 || y < last) extOk = false;
        last = y;
    }
    check(extOk, "extremes up to INT32_MIN and INT32_MAX: in range, monotonic, no overflow");
    check(apply(INT32_MAX) == 32767 && apply(INT32_MIN) == -32767, "the ceiling: +-32767 for the largest inputs");

    // slope 1 at the knee: the first 64 steps above it each rise by 1
    bool slope = true;
    for (int32_t x = kKnee; x < kKnee + 64; x++)
        if (apply(x + 1) - apply(x) != 1) slope = false;
    check(slope, "slope 1 just above the knee (no kink)");
    check(apply(32767) < 32767 && apply(32767) > kKnee, "full scale in stays below full scale out (soft, not a clip)");

    printf("      curve: 0 dBFS in -> %d (%.2f dBFS), +6 dB -> %d (%.2f), +12 dB -> %d (%.2f), +24 dB -> %d (%.2f)\n",
           apply(32767), db(apply(32767)), apply(65534), db(apply(65534)), apply(131068), db(apply(131068)),
           apply(524272), db(apply(524272)));
    printf("%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
