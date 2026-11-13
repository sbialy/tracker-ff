// Host test of two small pieces of the insert host (no firmware needed):
//  - the load gate of a project load (synth-open/plug_defer.h): rest after the load, one insert per pass, stagger, wrap safe clock
//  - the named end values of a numeric parameter (synth-open/plug/plug_abi.h, PLUG_PARAM_NAME_MIN / MAX)
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_defer_test.cpp -o /tmp/plug_defer_test && /tmp/plug_defer_test
#include "plug/plug_abi.h"
#include "plug_defer.h"

#include <cstdio>
#include <cstring>

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

// One main loop pass of plug::tick() for two pending non-manual slots, as plug_host.cpp does it: returns the bitmask of loaded slots
unsigned pass(plug::OpenGate& g, uint32_t now, bool pending[2])
{
    unsigned loaded = 0;
    bool openLoaded = false;
    for (int k = 0; k < 2; k++)
    {
        if (!pending[k] || openLoaded || !g.ready(now)) continue;
        pending[k] = false;
        g.took(now);
        openLoaded = true;
        loaded |= 1u << k;
    }
    return loaded;
}

void gate(uint32_t t0, const char* tag)
{
    char w[96];
    plug::OpenGate g;
    bool pend[2] = {true, true};
    g.arm(t0);
    snprintf(w, sizeof w, "%s: nothing loads in the pass that finished the load", tag);
    check(pass(g, t0, pend) == 0, w);
    snprintf(w, sizeof w, "%s: nothing loads before the rest is over", tag);
    check(pass(g, t0 + plug::kOpenRestMs - 1, pend) == 0, w);
    snprintf(w, sizeof w, "%s: insert 1 loads first, alone, when the rest is over", tag);
    check(pass(g, t0 + plug::kOpenRestMs, pend) == 1u, w);
    snprintf(w, sizeof w, "%s: insert 2 waits in the next passes", tag);
    check(pass(g, t0 + plug::kOpenRestMs + 1, pend) == 0, w);
    snprintf(w, sizeof w, "%s: insert 2 loads after the stagger, alone", tag);
    check(pass(g, t0 + plug::kOpenRestMs + plug::kStaggerMs, pend) == 2u, w);
    snprintf(w, sizeof w, "%s: nothing is left", tag);
    check(!pend[0] && !pend[1], w);
}

// an enum-less numeric parameter with named ends
const char* const kNames[] = {"Auto", "Max"};
}   // namespace

int main()
{
    gate(5000, "boot clock");
    gate(0xFFFFFF00u, "clock wrap");   // now + rest crosses 2^32

    // plug_param_value_name: min only (Glue Makeup), max only, both, none, enum
    plug_param a = {"Makeup", "dB", -0.5f, 24.0f, -0.5f, 0.5f, 0, PLUG_PARAM_NAME_MIN, kNames};
    check(plug_param_name_count(&a) == 1, "min only: one name");
    check(!strcmp(plug_param_value_name(&a, -0.5f) ? plug_param_value_name(&a, -0.5f) : "", "Auto"), "min only: the bottom shows Auto");
    check(plug_param_value_name(&a, 0.0f) == nullptr, "min only: 0 dB is a number");
    check(plug_param_value_name(&a, 24.0f) == nullptr, "min only: the top is a number");
    plug_param b = {"X", "", 0.0f, 10.0f, 5.0f, 1.0f, 0, PLUG_PARAM_NAME_MAX, kNames};
    check(plug_param_name_count(&b) == 1, "max only: one name");
    check(!strcmp(plug_param_value_name(&b, 10.0f), "Auto"), "max only: the top shows the first name");
    check(plug_param_value_name(&b, 0.0f) == nullptr, "max only: the bottom is a number");
    plug_param c = {"X", "", 0.0f, 10.0f, 5.0f, 1.0f, 0, PLUG_PARAM_NAME_MIN | PLUG_PARAM_NAME_MAX, kNames};
    check(plug_param_name_count(&c) == 2, "both: two names");
    check(!strcmp(plug_param_value_name(&c, 0.0f), "Auto") && !strcmp(plug_param_value_name(&c, 10.0f), "Max"), "both: min Auto, max Max");
    check(plug_param_value_name(&c, 5.0f) == nullptr, "both: the middle is a number");
    plug_param d = {"X", "", 0.0f, 10.0f, 5.0f, 1.0f, 0, 0, nullptr};
    check(plug_param_name_count(&d) == 0 && plug_param_value_name(&d, 0.0f) == nullptr, "flags 0 (every older plugin): never a name");
    plug_param e = {"X", "", 0.0f, 1.0f, 0.0f, 1.0f, 2, PLUG_PARAM_NAME_MIN, kNames};
    check(plug_param_name_count(&e) == 0 && plug_param_value_name(&e, 0.0f) == nullptr, "an enum ignores the flags");
    plug_param f = {"X", "", 0.0f, 10.0f, 5.0f, 1.0f, 0, PLUG_PARAM_NAME_MIN, nullptr};
    check(plug_param_value_name(&f, 0.0f) == nullptr, "flag without names: the number (no null read)");

    printf(g_fail ? "FAILED %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
