// The Patch Vary rule of Tracker+ and Mini (Synth::varyPatch, P12): the MK66 synth-open/open_cold.cpp patchVary per parameter, written over
// a plain description of one parameter so the same lines run in the IMX core (over the stock Metadata::Param) and in the host smoke test
// (over the MK66 ParamDef, host_smoke_test.cpp vary parity: same seed, same values, same generator state as patchVary).
// Header only, no stock or open types. The MK66 lines in open_cold.cpp stay as they are.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace synthvary
{
// the knob curves: the open CurveKind kLinear, kExp, kSteep, kPower = the stock Curve kLinear, kExponential, kExponentialSteep, kPower
enum class Curve : uint8_t
{
    kLinear,
    kExp,
    kSteep,
    kPower
};

struct Param
{
    float lo = 0.0f;
    float hi = 0.0f;
    float before = 0.0f; // the value now (the default when unset)
    Curve curve = Curve::kLinear;
    bool discrete = false; // drawn anew from 50 % (the MK66 kEnum and kOnOff, the stock isEnum)
    bool whole = false;    // a continuous draw rounded to whole steps (the MK66 kInt, kSemi, kRatio)
    bool skip = false;     // never varied (the volume)
};

// the MK66 seed step of the patch editor (s_varyRng * 1664525 + 1013904223 + micros, never 0)
inline uint32_t seed(uint32_t state, uint32_t entropy)
{
    state = state * 1664525u + 1013904223u + entropy;
    return state == 0 ? 1u : state;
}

inline uint32_t next(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline float unit(uint32_t& s) // 0 .. 1
{
    return float(next(s) >> 8) * (1.0f / 16777216.0f);
}

inline float curveToValue(Curve c, float lo, float hi, float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    switch (c)
    {
        case Curve::kLinear: break;
        case Curve::kExp:
            if (lo > 0.0f)
            {
                return lo * std::pow(hi / lo, n);
            }
            n = n * n;
            break;
        case Curve::kSteep: n = n * n * n; break;
        case Curve::kPower: n = n * n; break;
    }
    return lo + (hi - lo) * n;
}

inline float valueToCurve(Curve c, float lo, float hi, float v)
{
    v = std::clamp(v, lo, hi);
    if (c == Curve::kExp && lo > 0.0f)
    {
        return std::log(v / lo) / std::log(hi / lo);
    }
    const float n = hi > lo ? (v - lo) / (hi - lo) : 0.0f;
    switch (c)
    {
        case Curve::kExp:
        case Curve::kPower: return std::sqrt(n);
        case Curve::kSteep: return std::cbrt(n);
        default: return n;
    }
}

// one parameter of the MK66 loop: true and the new value in v when it changes (the caller sets it at once, the next parameter reads the
// patch again). rng is the loop's generator (0 starts as 0x9E3779B9, the caller does that once), percent 1..100
inline bool draw(const Param& d, int percent, uint32_t& rng, float& v)
{
    if (d.skip || !(d.hi > d.lo))
    {
        return false;
    }
    const float amount = float(percent) * 0.01f;
    if (d.discrete)
    {
        if (percent < 50 || unit(rng) >= amount)
        {
            return false;
        }
        v = d.lo + std::floor(unit(rng) * (d.hi - d.lo + 1.0f));
    }
    else
    {
        float k;
        if (percent >= 100)
        {
            k = unit(rng);
        }
        else
        {
            k = std::clamp(valueToCurve(d.curve, d.lo, d.hi, d.before) + (unit(rng) * 2.0f - 1.0f) * amount, 0.0f, 1.0f);
        }
        v = curveToValue(d.curve, d.lo, d.hi, k);
        if (d.whole)
        {
            v = std::round(v);
        }
    }
    v = std::clamp(v, d.lo, d.hi);
    return v != d.before;
}
} // namespace synthvary
