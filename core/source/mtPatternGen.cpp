#include "mtPatternGen.h"

#include "scales.h"

namespace patternGen
{
namespace
{
constexpr uint8_t kAnchor = 255; // weight: always a hit while density >= kAnchorMinDensity
constexpr uint8_t kAnchorMinDensity = 8;
constexpr uint8_t kBar = 16; // the tables are one bar of 16th steps, longer selections repeat the bar positions

// Chance in percent of a hit on each of the 16 steps at density 64, per rhythm role (kick, snare, hat, bass, lead).
const uint8_t kWeight[kPlainRoles][kBar] = {
    { kAnchor, 6, 12, 10, 60, 5, 30, 10, 75, 6, 40, 10, 60, 5, 28, 18 },         // kick
    { 0, 0, 6, 0, kAnchor, 0, 6, 10, 0, 5, 12, 0, kAnchor, 0, 14, 18 },          // snare
    { 90, 25, 70, 30, 90, 25, 70, 35, 90, 25, 70, 30, 90, 25, 70, 40 },          // closed hat
    { kAnchor, 10, 20, 40, 25, 10, 45, 30, 50, 10, 25, 45, 25, 10, 40, 35 },     // bass
    { 60, 0, 25, 0, 40, 0, 30, 15, 50, 0, 25, 0, 35, 0, 30, 20 },                // lead
};

const char* const kRoleNames[roleCount] = { "Kick", "Snare", "Hat", "Bass", "Lead", "Auto" };

struct Rng
{
    uint32_t s;
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n)
    {
        return (next() >> 8) % n;
    }
};

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// 0..100 volume of a hit: the downbeat or backbeat full, the rest softer, a little spread on the soft ones
int8_t volumeOf(uint8_t role, uint8_t pos, bool anchor, Rng& rng)
{
    int base;
    switch (role)
    {
        case roleKick: base = anchor ? 100 : 85; break;
        case roleSnare: base = anchor ? 100 : 50; break;
        case roleHat: base = (pos % 4 == 0) ? 90 : ((pos % 2 == 0) ? 72 : 52); break;
        case roleBass: base = pos == 0 ? 100 : 82; break;
        default: base = 70 + ((pos % 4 == 0) ? 15 : 0); break;
    }
    if (!anchor)
        base += int(rng.below(9)) - 4;
    return int8_t(clampInt(base, 1, 100));
}
} // namespace

const char* roleName(uint8_t role)
{
    return kRoleNames[role < roleCount ? role : 0];
}

uint8_t roleOfTrack(uint8_t role, uint8_t trackInSelection)
{
    if (role >= roleAuto)
        return uint8_t(trackInSelection % kPlainRoles);
    return role;
}

void generate(uint8_t role,
              uint16_t steps,
              uint8_t density,
              uint8_t root,
              uint8_t scale,
              uint8_t low,
              uint32_t seed,
              Cell* cells)
{
    if (role >= kPlainRoles)
        role = roleKick;
    Rng rng{ seed ? seed : 0x9E3779B9u };
    const bool pitched = role == roleBass || role == roleLead;

    // the notes of the scale from the root at or above low, an octave and a half for a lead and one octave for a bass
    uint8_t list[26];
    uint8_t n = 0;
    uint8_t base = 0;
    if (pitched)
    {
        low = low > 96 ? 96 : low;
        base = uint8_t((low / 12) * 12 + root % 12);
        if (base < low)
            base += 12;
        const uint8_t top = role == roleBass ? 12 : 19;
        for (uint8_t d = 0; d <= top; d++)
            if (isInScale(uint8_t(base + d), root, scale))
                list[n++] = uint8_t(base + d);
    }
    int idx = 0; // the lead's position in list

    for (uint16_t i = 0; i < steps; i++)
    {
        const uint8_t pos = uint8_t(i % kBar);
        const uint8_t w = kWeight[role][pos];
        const bool anchor = w == kAnchor;
        bool hit;
        if (anchor)
            hit = density >= kAnchorMinDensity;
        else
        {
            const int eff = clampInt(int(w) * density / 64, 0, 100);
            hit = int(rng.below(100)) < eff;
        }

        Cell& c = cells[i];
        c.hit = hit;
        c.note = -1;
        c.volume = -1;
        if (!hit)
            continue;
        c.volume = volumeOf(role, pos, anchor, rng);
        c.note = 0;
        if (!pitched)
            continue;

        if (role == roleBass)
        {
            const uint32_t r = rng.below(100);
            if (pos == 0 || r < 50)
                c.note = int8_t(list[0]);
            else if (r < 70)
            { // the fifth, or the scale note nearest to it
                int best = 0;
                for (uint8_t k = 1; k < n; k++)
                {
                    const int dk = int(list[k]) - (base + 7);
                    const int db = int(list[best]) - (base + 7);
                    if ((dk < 0 ? -dk : dk) < (db < 0 ? -db : db))
                        best = k;
                }
                c.note = int8_t(list[best]);
            }
            else if (r < 82)
                c.note = int8_t(base + 12);
            else
                c.note = int8_t(list[1 + rng.below(n - 1 > 0 ? n - 1 : 1)]);
        }
        else
        { // lead: a walk over the scale, the start of every bar comes back to a chord tone (degree 1, 3 or 5)
            if (pos == 0)
            { // one of the three chord tones (list index 0, 2, 4 of each octave of seven) nearest to where the line is
                uint8_t near[3];
                uint8_t found = 0;
                for (uint8_t pick = 0; pick < 3; pick++)
                {
                    int best = -1;
                    for (int k = 0; k < int(n); k++)
                    {
                        const uint8_t deg = uint8_t(k % 7);
                        if (deg != 0 && deg != 2 && deg != 4)
                            continue;
                        bool taken = false;
                        for (uint8_t q = 0; q < found; q++)
                            taken |= near[q] == k;
                        const int dk = k > idx ? k - idx : idx - k;
                        const int db = best < 0 ? 1000 : (best > idx ? best - idx : idx - best);
                        if (!taken && dk < db)
                            best = k;
                    }
                    if (best >= 0)
                        near[found++] = uint8_t(best);
                }
                idx = found ? near[rng.below(found)] : 0;
            }
            else
            {
                const uint32_t r = rng.below(100);
                const int dlt = r < 10 ? -2 : r < 35 ? -1 : r < 50 ? 0 : r < 75 ? 1 : r < 90 ? 2 : r < 95 ? 3 : -3;
                idx += dlt;
            }
            if (idx < 0)
                idx = -idx;
            if (idx > int(n) - 1)
                idx = 2 * (int(n) - 1) - idx;
            idx = clampInt(idx, 0, int(n) - 1);
            c.note = int8_t(list[idx]);
        }
    }
}
} // namespace patternGen
