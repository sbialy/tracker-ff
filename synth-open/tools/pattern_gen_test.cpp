// Host test of the pattern idea generator (core/source/mtPatternGen.cpp, tracker-ff). NOT firmware.
//   c++ -std=c++20 -O1 -Wall -I synth-open/tools/kern_host -I core/source synth-open/tools/pattern_gen_test.cpp core/source/mtPatternGen.cpp \
//       core/source/scales.cpp -o pattern_gen_test
// Checks: the same inputs give the same steps and another seed gives another pattern, the anchors (kick downbeat, snare backbeat,
// bass root) hold, the densities order the hit counts, bass and lead notes sit in the scale and in their register for every scale and
// every key, drums never carry a pitch, volumes are in 1..100, odd lengths and one step work, roleOfTrack and the names.
#include "mtPatternGen.h"
#include "scales.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace patternGen;

namespace
{
int g_fail = 0, g_checks = 0;
void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("FAIL %s\n", what);
    }
}

std::vector<Cell> gen(uint8_t role, uint16_t steps, uint8_t density, uint32_t seed, uint8_t root = 24, uint8_t scale = 0, uint8_t low = 48)
{
    std::vector<Cell> c(steps + 1);
    generate(role, steps, density, root, scale, low, seed, c.data());
    c.resize(steps);
    return c;
}
int hits(const std::vector<Cell>& c)
{
    int n = 0;
    for (const Cell& x : c)
        n += x.hit;
    return n;
}
bool same(const std::vector<Cell>& a, const std::vector<Cell>& b)
{
    return a.size() == b.size() && memcmp(a.data(), b.data(), a.size() * sizeof(Cell)) == 0;
}
} // namespace

int main()
{
    // determinism and seed
    for (uint8_t r = 0; r < kPlainRoles; r++)
    {
        check(same(gen(r, 16, 64, 1234), gen(r, 16, 64, 1234)), "same seed same steps");
        int differ = 0;
        for (uint32_t s = 1; s <= 20; s++)
            differ += !same(gen(r, 32, 64, s), gen(r, 32, 64, s + 100));
        check(differ >= 18, "other seeds give other patterns");
    }
    check(hits(gen(roleKick, 16, 64, 0)) > 0, "seed 0 still plays");

    // anchors for 2000 seeds
    int kickDown = 0, snareBack = 0, bassRoot = 0, snareOff = 0, kickDownOn = 0;
    for (uint32_t s = 1; s <= 2000; s++)
    {
        auto k = gen(roleKick, 32, 64, s);
        kickDown += k[0].hit && k[16].hit;
        auto sn = gen(roleSnare, 32, 64, s);
        snareBack += sn[4].hit && sn[12].hit && sn[20].hit && sn[28].hit;
        snareOff += sn[0].hit || sn[8].hit || sn[16].hit;
        auto b = gen(roleBass, 32, 64, s, 24 + (s % 12), 0, 36);
        bassRoot += b[0].hit && b[0].note % 12 == (24 + (s % 12)) % 12;
        kickDownOn += gen(roleKick, 16, 7, s)[0].hit;
    }
    check(kickDown == 2000, "kick downbeat is an anchor");
    check(snareBack == 2000, "snare backbeat is an anchor");
    check(snareOff == 0, "snare never lands on the 1, 3 of the bar");
    check(bassRoot == 2000, "bass plays the root on the downbeat");
    check(kickDownOn == 0, "below density 8 the anchors go too");
    check(gen(roleKick, 16, 8, 5)[0].hit, "density 8 keeps the anchor");

    // density orders the hit count
    for (uint8_t r = 0; r < kPlainRoles; r++)
    {
        long lo = 0, mid = 0, hi = 0;
        for (uint32_t s = 1; s <= 300; s++)
        {
            lo += hits(gen(r, 64, 16, s));
            mid += hits(gen(r, 64, 64, s));
            hi += hits(gen(r, 64, 128, s));
        }
        printf("role %-5s hits per 64 steps at density 16 / 64 / 128: %.1f / %.1f / %.1f\n", roleName(r), lo / 300.0, mid / 300.0, hi / 300.0);
        check(lo < mid && mid < hi, "density thins and thickens");
    }
    // sanity of the designed densities per role at 64 (hits per 16 steps)
    {
        auto avg = [](uint8_t r) { long n = 0; for (uint32_t s = 1; s <= 500; s++) n += hits(gen(r, 16, 64, s)); return n / 500.0; };
        check(avg(roleKick) >= 3.0 && avg(roleKick) <= 6.0, "kick 3 to 6 hits a bar");
        check(avg(roleSnare) >= 2.0 && avg(roleSnare) <= 4.0, "snare 2 to 4 hits a bar");
        check(avg(roleHat) >= 8.0 && avg(roleHat) <= 12.0, "hat 8 to 12 hits a bar");
        check(avg(roleBass) >= 4.0 && avg(roleBass) <= 8.0, "bass 4 to 8 hits a bar");
        check(avg(roleLead) >= 3.0 && avg(roleLead) <= 7.0, "lead 3 to 7 hits a bar");
    }

    // notes: drums carry no pitch, bass and lead stay in scale and register for every key and scale
    bool drumsClean = true, volOk = true, noHitClean = true;
    for (uint8_t r = 0; r < kPlainRoles; r++)
        for (uint32_t s = 1; s <= 50; s++)
            for (const Cell& c : gen(r, 40, 64 + s, s))
            {
                if (r < roleBass && c.hit && c.note != 0)
                    drumsClean = false;
                if (c.hit && (c.volume < 1 || c.volume > 100))
                    volOk = false;
                if (!c.hit && (c.note != -1 || c.volume != -1))
                    noHitClean = false;
            }
    check(drumsClean, "drum hits keep the note of the step (note 0)");
    check(volOk, "volumes are 1 to 100");
    check(noHitClean, "no hit: note -1, volume -1");

    int maxLeap = 0;
    int bad = 0, outOfRange = 0, total = 0, lowBad = 0, leadWalk = 0, leadSteps = 0;
    for (uint8_t scale = 0; scale <= MAX_SCALE; scale++)
        for (uint8_t root = 0; root < 12; root++)
            for (uint8_t role = roleBass; role <= roleLead; role++)
                for (uint32_t s = 1; s <= 6; s++)
                {
                    const uint8_t low = role == roleBass ? (s & 1 ? 36 : 48) : 60;
                    auto c = gen(role, 64, 100, s * 7 + scale, uint8_t(24 + root), scale, low);
                    int prev = -1;
                    for (const Cell& x : c)
                    {
                        if (!x.hit)
                            continue;
                        total++;
                        if (!isInScale(uint8_t(x.note), uint8_t(24 + root), scale))
                            bad++;
                        const int top = low + 11 + (role == roleLead ? 8 : 1) + 11;
                        if (x.note < low || x.note > top || x.note > 127)
                            outOfRange++;
                        if (role == roleBass && x.note < low)
                            lowBad++;
                        if (role == roleLead && prev >= 0)
                        {
                            leadSteps++;
                            { const int d = x.note > prev ? x.note - prev : prev - x.note; leadWalk += d <= 16; if (d > maxLeap) maxLeap = d; }
                        }
                        prev = x.note;
                    }
                }
    printf("pitched notes checked: %d\n", total);
    check(total > 10000, "pitched notes were produced");
    check(bad == 0, "bass and lead notes are in the scale");
    check(outOfRange == 0, "bass and lead notes are in their register");
    check(lowBad == 0, "bass is not below its floor");
    printf("largest lead leap: %d semitones\n", maxLeap);
    check(leadSteps > 0 && leadWalk == leadSteps, "lead leaps stay within 16 semitones (sparse scales have wide gaps)");

    // lead uses more than one pitch, bass uses root mostly
    {
        int root = 0, other = 0;
        for (uint32_t s = 1; s <= 200; s++)
            for (const Cell& c : gen(roleBass, 64, 64, s, 24, 0, 36))
                if (c.hit)
                    (c.note % 12 == 0 ? root : other)++;
        check(root > other, "bass leans on the root");
        std::vector<int> seen(128, 0);
        for (uint32_t s = 1; s <= 50; s++)
            for (const Cell& c : gen(roleLead, 64, 64, s, 24, 2, 60))
                if (c.hit)
                    seen[c.note] = 1;
        int kinds = 0;
        for (int v : seen)
            kinds += v;
        check(kinds >= 6, "lead uses many pitches of the scale");
    }

    // lengths: one step, odd lengths, the longest selection
    for (uint16_t n : { 1, 2, 3, 15, 17, 33, 127, 128 })
        for (uint8_t r = 0; r < kPlainRoles; r++)
        {
            std::vector<Cell> c(n + 8);
            for (Cell& x : c) { x.note = 99; x.volume = 99; x.hit = true; }
            generate(r, n, 64, 24, 0, 48, 77, c.data());
            bool tail = true;
            for (size_t i = n; i < c.size(); i++)
                tail &= c[i].note == 99 && c[i].volume == 99;
            check(tail, "generate writes only steps cells");
            bool all = true;
            for (size_t i = 0; i < n; i++)
                all &= c[i].note != 99 || !c[i].hit;
            check(all, "generate writes every cell");
        }

    // out of range arguments
    check(hits(gen(200, 16, 64, 3)) > 0, "role out of range falls back to a plain role");
    check(hits(gen(roleBass, 16, 64, 3, 255, 200, 255)) > 0, "key, scale and floor out of range are clamped");
    for (const Cell& c : gen(roleBass, 16, 128, 9, 255, 200, 255))
        check(!c.hit || c.note <= 127, "a high floor stays inside 127");

    // roles of tracks and names
    check(roleOfTrack(roleKick, 3) == roleKick && roleOfTrack(roleLead, 0) == roleLead, "a plain role stays");
    uint8_t order[7];
    for (uint8_t t = 0; t < 7; t++)
        order[t] = roleOfTrack(roleAuto, t);
    check(order[0] == roleKick && order[1] == roleSnare && order[2] == roleHat && order[3] == roleBass && order[4] == roleLead &&
            order[5] == roleKick && order[6] == roleSnare,
          "auto walks kick snare hat bass lead and again");
    check(!strcmp(roleName(0), "Kick") && !strcmp(roleName(1), "Snare") && !strcmp(roleName(2), "Hat") && !strcmp(roleName(3), "Bass") &&
            !strcmp(roleName(4), "Lead") && !strcmp(roleName(5), "Auto") && !strcmp(roleName(77), "Kick"),
          "role names");

    printf("%d checks, %d failed\n", g_checks, g_fail);
    printf(g_fail ? "RESULT: RED\n" : "RESULT: GREEN\n");
    return g_fail ? 1 : 0;
}
