// Host test of the Strum ramp depth (synth-open/plug/strum_ramp.h, strum_depth.h). NOT firmware.
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/strum_depth_test.cpp -o /tmp/sdt && /tmp/sdt
//   c++ -std=c++20 -O1 -Wall -I synth-open -DSTRUM_RAMP=0 synth-open/tools/strum_depth_test.cpp -o /tmp/sdt0 && /tmp/sdt0
// run_strum_depth_test.sh builds both and compares the DIGEST lines: the ramp build at depth 0 must print the digest of the
// STRUM_RAMP=0 build (every function returns the plain value: bit identical to the old behaviour), and at depth 100 the digest of the
// ramp as it was before the depth existed (the formulas of b1a46f2 below, copied verbatim).
#include "strum_depth.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace legacy   // synth-open/plug/strum_ramp.h at b1a46f2 (the ramp before the depth), STRUM_RAMP=1
{
inline constexpr int fallPct(int order, int units)
{
    const unsigned u = unsigned(units > strumramp::kFullUnits ? strumramp::kFullUnits : units);
    const int p = 100 - int(unsigned(strumramp::kFallPct) * unsigned(order) * u / unsigned(strumramp::kFullUnits));
    return p < strumramp::kFloorPct ? strumramp::kFloorPct : p;
}
inline constexpr int strokePct(int strum) { return strum < 0 ? strumramp::kUpStrokePct : 100; }
inline constexpr int velocity(int v, int order, int strum)
{
    if (v <= 0) return v;
    const int units = strum < 0 ? -strum : strum;
    int r = v * fallPct(order, units) / 100;
    r = r * strokePct(strum) / 100;
    return r < 1 ? 1 : r;
}
inline constexpr float gain(int order, int units)
{
    const int p = fallPct(order, units);
    return p >= 100 ? 1.0f : float(p) * 0.01f;
}
}   // namespace legacy

namespace
{
int g_fail = 0, g_checks = 0;
void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}
uint64_t fnv(uint64_t h, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        h ^= (v >> (8 * i)) & 0xffu;
        h *= 1099511628211ull;
    }
    return h;
}
uint32_t bitsOf(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
// every function over every input: velocity 0..127 and -1, order 0..3, strum -63..63 without 0, units 1..63, at depth `pct`
template <class V, class F, class S, class G> uint64_t digest(V vel, F fall, S stroke, G gain)
{
    uint64_t h = 1469598103934665603ull;
    for (int strum = -63; strum <= 63; strum++)
    {
        if (!strum) continue;
        const int units = strum < 0 ? -strum : strum;
        h = fnv(h, uint32_t(stroke(strum)));
        for (int order = 0; order < 4; order++)
        {
            h = fnv(h, uint32_t(fall(order, units)));
            h = fnv(h, bitsOf(gain(order, units)));
            for (int v = -1; v <= 127; v++) h = fnv(h, uint32_t(vel(v, order, strum)));
        }
    }
    return h;
}
}   // namespace

int main()
{
    using namespace strumramp;
    const uint64_t d0 = digest([](int v, int o, int s) { return velocity(v, o, s, 0); }, [](int o, int u) { return fallPct(o, u, 0); },
                               [](int s) { return strokePct(s, 0); }, [](int o, int u) { return gain(o, u, 0); });
    const uint64_t d100 = digest([](int v, int o, int s) { return velocity(v, o, s, 100); }, [](int o, int u) { return fallPct(o, u, 100); },
                                 [](int s) { return strokePct(s, 100); }, [](int o, int u) { return gain(o, u, 100); });
    const uint64_t dOld = digest(legacy::velocity, legacy::fallPct, legacy::strokePct, legacy::gain);
    const uint64_t dOld3 = digest([](int v, int o, int s) { return velocity(v, o, s); }, [](int o, int u) { return fallPct(o, u); },
                                  [](int s) { return strokePct(s); }, [](int o, int u) { return gain(o, u); });
    (void)dOld3;
    printf("DIGEST0 %016llx\nDIGEST100 %016llx\nDIGESTLEGACY %016llx\n", (unsigned long long)d0, (unsigned long long)d100, (unsigned long long)dOld);

#if STRUM_RAMP
    check(d100 == dOld, "depth 100 is the ramp of b1a46f2 for every velocity, order and strum (velocity, fall, stroke, gain bits)");
    check(dOld3 == dOld, "the 100 % overloads (plugin halves built for a host before the depth) are the old ramp too");
    {
        bool plain = true, one = true;
        for (int strum = -63; strum <= 63; strum++)
        {
            if (!strum) continue;
            const int units = strum < 0 ? -strum : strum;
            for (int order = 0; order < 4; order++)
            {
                plain = plain && fallPct(order, units, 0) == 100 && strokePct(strum, 0) == 100;
                one = one && gain(order, units, 0) == 1.0f;
                for (int v = -1; v <= 127; v++) plain = plain && velocity(v, order, strum, 0) == v;
            }
        }
        check(plain, "depth 0: velocity untouched, fall 100, stroke 100, for every input");
        check(one, "depth 0: gain is exactly 1.0f");
    }
    {
        bool mono = true;
        for (int strum : {-63, -40, -16, -4, -1, 1, 4, 16, 40, 63})
        {
            const int units = strum < 0 ? -strum : strum;
            for (int order = 0; order < 4; order++)
                for (int pct = 1; pct <= 100; pct++)
                {
                    mono = mono && fallPct(order, units, pct) <= fallPct(order, units, pct - 1) && strokePct(strum, pct) <= strokePct(strum, pct - 1);
                    mono = mono && fallPct(order, units, pct) >= kFloorPct && fallPct(order, units, pct) <= 100;
                    for (int v : {1, 2, 64, 100, 127}) mono = mono && velocity(v, order, strum, pct) <= velocity(v, order, strum, pct - 1) && velocity(v, order, strum, pct) >= 1;
                }
        }
        check(mono, "depth 1..100: the fall, the upstroke drop and the velocity only deepen with the depth, floor 50, velocity >= 1");
        check(fallPct(3, 16, 50) == 90 && fallPct(3, 16, 100) == 79 && strokePct(-5, 50) == 94 && strokePct(-5, 100) == 88 && strokePct(5, 50) == 100,
              "depth 50 is half the fall (90 vs 79 at order 3, 16 units) and half the upstroke drop (94 vs 88)");
        check(fallPct(3, 16, 250) == fallPct(3, 16, 100) && fallPct(3, 16, -5) == 100, "a depth outside 0..100 counts as 100 or 0");
        check(velocity(0, 2, 20, 100) == 0 && velocity(100, 0, 20, 100) == 100 && velocity(100, 0, -20, 100) == 88, "velocity 0 stays 0, the first note keeps it, the upstroke 88 at depth 100");
    }
#else
    check(d0 == d100, "STRUM_RAMP=0: every depth is the plain value");
#endif
    {
        g_depth.ramp_pct = 77;
        check(depthPct() == 77 && g_depth.struct_size == sizeof(plug_ext_strum) && sizeof(plug_ext_strum) == 8, "the depth object: struct_size 8, ramp_pct live");
        setDepth(250);
        check(depthPct() == 100, "setDepth clamps to 100");
        setDepth(-3);
        check(depthPct() == 0, "setDepth clamps to 0");
        check(PLUG_HAS(&g_depth, plug_ext_strum, ramp_pct) && PLUG_EXT_STRUM == 7, "PLUG_HAS sees ramp_pct, PLUG_EXT_STRUM is 7");
    }
    printf("strum_depth_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
