// PLK ("Pluck"): clean-room Karplus-Strong plucked string for the open PolySynth shim. See pluck.h and README.md.
//
// Signal flow of one voice (all state in the 128 byte SynthVoice::state plus one int16 delay line from a shared pool):
//
//   exciter burst (once, into the empty line)  ->  delay line  ->  tuning allpass -> dispersion allpass x2 (Stretch)
//        ^                                            ^                                       |
//        |  Noise / Click / Saw / Mallet,             |                                       v
//        |  pick position comb, colour lowpass        +---- error feedback rounding <---- gain <---- loop lowpass
//
// The line is excited once at note on and then only recirculates. Everything per note is solved at note on (and when the
// pitch bend or a parameter changes) in Retune(), never per sample: the loop gain is set so the fundamental dies in the Damping time
// on every key, the loop lowpass cutoff follows sqrt(f0) so the upper partials decay about equally fast in seconds on every key,
// and the phase delays of the loop lowpass, the dispersion and the tuning allpass are computed at f0 and taken out of the
// integer delay, so the note is in tune up to the top. Notes below E2 run the loop at half the sample rate (the line then holds
// twice the period, the output is linearly interpolated), which brings E1 into a 1,088 byte line.
//
// No libm: the few transcendentals Retune needs (exp2, sin, cos, atan) are polynomials below, evaluated per note, not per sample.
#include "pluck/pluck.h"

#include <cstdint>
#include <cstring>

namespace ot {
namespace audio {
namespace pluck {
namespace {

constexpr int kLineLen = 544;   // int16 samples per line: 1,088 bytes. E2 (82.4 Hz) needs 535 at 44.1 kHz, E1 535 at half rate
constexpr int kLines = 8;       // kSynthMaxVoices: the shim never sounds more voices than that at once
constexpr int kHalfBelow = 40;  // notes below E2 (MIDI 40) run the loop at half rate
constexpr float kPi = 3.14159265f;
constexpr float kHalfPi = 1.57079633f;
constexpr float kLineUnit = 16384.0f;               // float 1.0 = 16384 in the line (one bit of headroom for the comb and the allpass overshoot)
constexpr float kSilence = 3.0f;                    // mean |x| of a block, line units (-75 dB): below it for 2 blocks the voice is done
constexpr float kRoundMagic = 12582912.0f;          // 1.5 * 2^23: adding it rounds a float to an integer in the low mantissa bits
constexpr float kTargetRms = 0.26f * kLineUnit;     // exciter level before the per exciter trim
constexpr float kTrim[4] = { 1.2f, 1.2f, 0.5f, 0.55f };   // Noise, Click, Saw, Mallet: equal loudness
constexpr float kNyquistGuard = 0.45f;

// ------------------------------------------------------------------ small math (per note, not per sample)

// Per note code is kept out of line and built for size: inlined copies of the polynomials would cost more flash than the call costs time
#if defined(__GNUC__) && !defined(__clang__)
#define PLUCK_COLD __attribute__((noinline, optimize("Os")))
#else
#define PLUCK_COLD __attribute__((noinline))
#endif

inline float fsqrt(float x)
{
#if defined(__arm__) && defined(__VFP_FP__)
    float r;
    asm("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
#else
    return __builtin_sqrtf(x);
#endif
}

// 2^x for x in [-100, 100], relative error about 1e-7
PLUCK_COLD float fexp2(float x)
{
    int i = int(x);
    if (float(i) > x) {
        --i;
    }
    const float f = x - float(i);
    const float p = 1.0f + f * (0.693147181f + f * (0.240226507f + f * (0.0555041087f + f * (0.00961812911f + f * (0.00133335581f + f * (0.000154035304f + f * 0.0000152527338f))))));
    if (i < -126) {
        i = -126;
    }
    if (i > 126) {
        i = 126;
    }
    const uint32_t bits = uint32_t(i + 127) << 23;
    float s;
    __builtin_memcpy(&s, &bits, sizeof(s));   // the firmware builds with -fno-builtin: plain memcpy would be a call
    return p * s;
}

inline float fexp(float x) { return fexp2(x * 1.44269504f); }

// sin and cos of w in [0, 1.6]
PLUCK_COLD void sincosf_(float w, float& s, float& c)
{
    const float w2 = w * w;
    s = w * (1.0f - w2 * (1.0f / 6.0f) * (1.0f - w2 * (1.0f / 20.0f) * (1.0f - w2 * (1.0f / 42.0f) * (1.0f - w2 * (1.0f / 72.0f) * (1.0f - w2 * (1.0f / 110.0f))))));
    c = 1.0f - w2 * 0.5f * (1.0f - w2 * (1.0f / 12.0f) * (1.0f - w2 * (1.0f / 30.0f) * (1.0f - w2 * (1.0f / 56.0f) * (1.0f - w2 * (1.0f / 90.0f)))));
}

// atan(z) for z in [0, 1] (Abramowitz and Stegun 4.4.49, error below 1e-5)
inline float atan01(float z)
{
    const float z2 = z * z;
    return z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
}

// atan2(y, x) for y >= 0 and any x
PLUCK_COLD float atan2Pos(float y, float x)
{
    if (y <= 0.0f) {
        return x >= 0.0f ? 0.0f : kPi;
    }
    if (x >= 0.0f) {
        return y <= x ? atan01(y / x) : kHalfPi - atan01(x / y);
    }
    const float ax = -x;
    return kPi - (y <= ax ? atan01(y / ax) : kHalfPi - atan01(ax / y));
}

// Phase delay in samples at w of the first order allpass (a + z^-1) / (1 + a z^-1), |a| < 1
PLUCK_COLD float apPhaseDelay(float a, float sw, float cw, float w)
{
    const float y2 = a * sw, x2 = 1.0f + a * cw;
    const float t2 = y2 >= 0.0f ? atan2Pos(y2, x2) : -atan2Pos(-y2, x2);
    return (atan2Pos(sw, a + cw) - t2) / w;
}

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// ------------------------------------------------------------------ voice state and line pool

struct VoiceState {
    float sT, s0, s1;     // tuning allpass state, two dispersion allpass states
    float lp, err, xPrev; // loop lowpass state, rounding error feedback, last loop output (half rate interpolation)
    float aT, aD, c, rho; // tuning allpass, dispersion allpass, lowpass pole and loop gain
    float vel;            // note velocity 0..1
    float pitch;          // the note in semitones, no Tune, Fine or bend
    float lastPitch;      // pitch including the bend that Retune last solved
    uint32_t serial;      // SynthParams::serial that Retune last saw
    uint16_t w, rd, len;  // write index, read index, integer delay in samples
    uint8_t line;         // pool line of this voice
    uint8_t half;         // loop runs at half rate
    uint8_t released;     // note off seen
    uint8_t disp;         // dispersion allpass cascade on
    uint8_t quiet;        // consecutive silent blocks
    uint8_t quietLimit;   // silent blocks that end the voice: a sparse burst (Click, Mallet) passes the pick-up once per period
};
static_assert(sizeof(VoiceState) <= kSynthVoiceStateBytes, "voice state too large");

// Plain data, zero initialised in .bss (8.7 KB). Kept out of the engine object so the object does not move into .data.
struct Pool {
    int16_t line[kLines][kLineLen];
    const SynthVoice* owner[kLines];
    uint32_t stamp[kLines];
    uint32_t clock;
};
#ifndef PLUCK_WORK_POOL
Pool gPool;
#else
Pool* gPoolAt;   // host memory, zeroed (setWork)
#define gPool (*gPoolAt)
#endif

// A line for this voice record: a stale line of the same record is dropped, then a free one, else the oldest is taken over
// (its voice finds itself without a line in its next render and ends)
int claim(const SynthVoice* v)
{
    int pick = -1;
    for (int i = 0; i < kLines; ++i) {
        if (gPool.owner[i] == v) {
            gPool.owner[i] = nullptr;
        }
    }
    for (int i = 0; i < kLines && pick < 0; ++i) {
        if (!gPool.owner[i]) {
            pick = i;
        }
    }
    if (pick < 0) {
        pick = 0;
        for (int i = 1; i < kLines; ++i) {
            if (gPool.stamp[i] < gPool.stamp[pick]) {
                pick = i;
            }
        }
    }
    gPool.owner[pick] = v;
    gPool.stamp[pick] = ++gPool.clock;
    return pick;
}

inline float P(const SynthParams& p, int i) { return p.end[i]; }

// The note with Tune, Fine and the step FX pitch bend (semitones)
inline float soundingPitch(const VoiceState& V, const SynthParams& p, float bend)
{
    return V.pitch + P(p, kTune) + P(p, kFine) * 0.01f + bend;
}

// ------------------------------------------------------------------ Retune: the coefficients of a voice

// Solves loop lowpass, loop gain, dispersion and tuning allpass for the voice's current pitch and parameters. Called at note on, on
// note off, when the bend moves and when a parameter changed: a few hundred cycles of polynomial math, never per sample.
PLUCK_COLD void retune(VoiceState& V, const SynthParams& p, float pitch)
{
    const float sr = kSampleRate * (V.half ? 0.5f : 1.0f);
    float f0 = 440.0f * fexp2((pitch - 69.0f) * (1.0f / 12.0f));
    f0 = clampf(f0, 20.0f, 6000.0f);
    const float w = 6.28318531f * f0 / sr;
    float sw, cw;
    sincosf_(w, sw, cw);

    // Velocity brightens (timbre) and lengthens (time) the note. Velocity 1 is the plain patch.
    const float bri = clampf(P(p, kBrightness) - 0.4f * (1.0f - V.vel), 0.0f, 1.0f);
    float t60;
    float fc = fsqrt(f0 * 4000.0f * fexp(5.52146f * bri));   // loop lowpass cutoff, Hz: follows sqrt(f0)
    if (V.released) {
        t60 = clampf(P(p, kRelease), 0.005f, 5.0f);
        fc *= 0.4f;                                           // a damped string is dull
    } else {
        t60 = 15.0f * fexp(-5.01063f * clampf(P(p, kDamping), 0.0f, 1.0f)) * (0.55f + 0.45f * V.vel);
    }
    fc = clampf(fc, 100.0f, kNyquistGuard * sr);

    // Decay compensation: the fundamental loses G = exp(-6.9078 / (f0 t60)) per pass in all. The loop lowpass may take 60 % of that loss
    // at most (a high note with a long Damping would otherwise need a loop gain above 1): the pole is capped to where the lowpass
    // loses (1 - G) * 0.6 at f0. Loss of y = (1 - c) x + c y' at w is about c w^2 / (2 (1 - c)^2), solved for c.
    const float gTarget = fexp(-6.90776f / (f0 * t60));
    float c = fexp(-6.28318531f * fc / sr);
    {
        const float k = 1.2f * (1.0f - gTarget) / (w * w);
        const float cMax = k < 1.0e-4f ? k : ((2.0f * k + 1.0f) - fsqrt(4.0f * k + 1.0f)) / (2.0f * k);
        if (c > cMax) {
            c = cMax;
        }
    }
    const float lpDelay = atan2Pos(c * sw, 1.0f - c * cw) / w;
    const float lpGain = (1.0f - c) / fsqrt(1.0f - 2.0f * c * cw + c * c);
    // The lowpass takes lpGain of the loss at f0, the loop gain the rest
    float rho = gTarget / lpGain;
    if (rho > 0.99995f) {
        rho = 0.99995f;
    }

    // Dispersion: two identical allpasses with a < 0 (the delay falls with frequency, so the partials come out sharp)
    const float stretch = clampf(P(p, kStretch), 0.0f, 1.0f);
    float aD = -0.8f * stretch * stretch;
    float dispDelay = 0.0f;
    const float total = sr / f0;
    bool disp = aD < -0.002f;
    if (disp) {
        // The dispersion must leave room for the line: cut it back on high notes
        for (int k = 0; k < 8; ++k) {
            dispDelay = 2.0f * apPhaseDelay(aD, sw, cw, w);
            if (total - lpDelay - dispDelay >= 3.0f) {
                break;
            }
            aD *= 0.6f;
        }
        disp = aD < -0.002f && total - lpDelay - dispDelay >= 3.0f;
        if (!disp) {
            aD = 0.0f;
            dispDelay = 0.0f;
        }
    }

    // What is left for the line and the tuning allpass: an integer len plus a delay between 0.5 and 1.5 samples
    float rest = total - lpDelay - dispDelay;
    rest = clampf(rest, 1.5f, float(kLineLen) + 0.45f);
    int len = int(rest - 0.5f);
    const float want = rest - float(len);
    float d = want;
    float eta = (1.0f - d) / (1.0f + d);
    for (int k = 0; k < 2; ++k) {                // the allpass delay is not flat: solve it at f0
        d = clampf(d + (want - apPhaseDelay(eta, sw, cw, w)), 0.3f, 1.7f);
        eta = (1.0f - d) / (1.0f + d);
    }

    V.c = c;
    V.rho = rho;
    V.aD = aD;
    V.aT = eta;
    V.disp = disp;
    {
        const int periodBlocks = ((len + 2) * (V.half ? 2 : 1)) / int(kBlock) + 2;
        V.quietLimit = uint8_t(periodBlocks < 255 ? periodBlocks : 255);
    }
    if (len != V.len) {
        V.len = uint16_t(len);
        V.rd = uint16_t(V.w >= len ? V.w - len : V.w + kLineLen - len);
    }
}

// ------------------------------------------------------------------ exciter

// The burst, one value per line position i of Lx. Pure function of i, so the pick position comb can read it twice.
PLUCK_COLD float excitation(int type, int i, int lx, uint32_t seed)
{
    switch (type) {
    case kClick: {
        static const float kShape[6] = { 0.35f, 1.0f, 0.7f, 0.25f, 0.06f, 0.0f };
        return i < 6 ? kShape[i] : 0.0f;
    }
    case kSaw:
        return 1.0f - 2.0f * float(i) / float(lx);
    case kMallet: {
        const int wd = lx / 5 < 6 ? 6 : lx / 5;
        if (i >= wd) {
            return 0.0f;
        }
        const float t = float(i) / float(wd);
        return 4.0f * t * (1.0f - t);
    }
    default:
        return float(int32_t(hash32(seed + uint32_t(i) * 0x9E3779B1U))) * (1.0f / 2147483648.0f);
    }
}

// Fills the empty line with the burst: comb for the pick position, colour lowpass, zero mean, fixed RMS (peak limited)
PLUCK_COLD void excite(VoiceState& V, int16_t* buf, int type, float pos, float kappa, uint32_t seed)
{
    std::memset(buf, 0, sizeof(int16_t) * kLineLen);
    const int lx = V.len;
    const int lag = int(pos * float(lx) + 0.5f);
    const bool comb = lag >= 1 && lag < lx;
    const bool colour = kappa < 0.95f;
    float s = 0.0f, sum = 0.0f, sumSq = 0.0f, peak = 0.0f;
    int idx = V.rd;
    for (int i = 0; i < lx; ++i) {
        float e = excitation(type, i, lx, seed);
        if (comb) {
            e -= excitation(type, i >= lag ? i - lag : i + lx - lag, lx, seed);
        }
        if (colour) {
            s += kappa * (e - s);
            e = s;
        }
        e *= 8192.0f;
        buf[idx] = int16_t(e + (e >= 0.0f ? 0.5f : -0.5f));
        sum += e;
        sumSq += e * e;
        const float ae = e < 0.0f ? -e : e;
        peak = ae > peak ? ae : peak;
        idx = idx + 1 == kLineLen ? 0 : idx + 1;
    }
    const float mean = sum / float(lx);
    const float var = sumSq / float(lx) - mean * mean;
    float scale = 1.0f;
    if (var > 1.0f) {
        scale = kTargetRms * kTrim[type & 3] / fsqrt(var);
        const float room = 0.9f * kLineUnit / (peak + (mean < 0.0f ? -mean : mean) + 1.0f);
        scale = scale < room ? scale : room;
    }
    idx = V.rd;
    for (int i = 0; i < lx; ++i) {
        const float e = (float(buf[idx]) - mean) * scale;
        buf[idx] = int16_t(e + (e >= 0.0f ? 0.5f : -0.5f));
        idx = idx + 1 == kLineLen ? 0 : idx + 1;
    }
}

// ------------------------------------------------------------------ the loop

inline int32_t roundSat(float v, float& err)
{
    const float t = v + kRoundMagic;
    uint32_t bits;
    __builtin_memcpy(&bits, &t, sizeof(bits));
    int32_t q = int32_t(bits) - 0x4B400000;
    err = v - (t - kRoundMagic);
    if (uint32_t(q + 32768) > 65535u) {
        q = q < 0 ? -32768 : 32767;
        err = 0.0f;
    }
    return q;
}

// One block. kDisp: the dispersion allpasses are in the loop. kHalf: the loop runs at half rate (64 steps, linear interpolation).
// Adds into L and R, returns the mean |output| of the block in line units.
template <bool kDisp, bool kHalf>
float runBlock(VoiceState& V, int16_t* buf, float g, float gInc, float* L, float* R)
{
    float sT = V.sT, s0 = V.s0, s1 = V.s1, lp = V.lp, err = V.err, xPrev = V.xPrev;
    const float aT = V.aT, aD = V.aD, c = V.c, rho = V.rho;
    int w = V.w, rd = V.rd;
    float acc = 0.0f;

    constexpr int kSteps = kHalf ? int(kBlock) / 2 : int(kBlock);
    for (int i = 0; i < kSteps; ++i) {
        const float x = float(buf[rd]);
        rd = rd + 1 == kLineLen ? 0 : rd + 1;
        float y = aT * x + sT;
        sT = x - aT * y;
        if (kDisp) {
            const float y1 = aD * y + s0;
            s0 = y - aD * y1;
            const float y2 = aD * y1 + s1;
            s1 = y1 - aD * y2;
            y = y2;
        }
        lp = y + c * (lp - y);
        const int32_t q = roundSat(lp * rho + err, err);
        buf[w] = int16_t(q);
        w = w + 1 == kLineLen ? 0 : w + 1;
        if (kHalf) {
            const float o0 = (xPrev + x) * 0.5f;
            xPrev = x;
            float o = o0 * g;
            L[2 * i] += o;
            R[2 * i] += o;
            g += gInc;
            o = x * g;
            L[2 * i + 1] += o;
            R[2 * i + 1] += o;
            g += gInc;
            acc += (o0 < 0.0f ? -o0 : o0) + (x < 0.0f ? -x : x);
        } else {
            const float o = x * g;
            L[i] += o;
            R[i] += o;
            g += gInc;
            acc += x < 0.0f ? -x : x;
        }
    }
    V.sT = sT; V.s0 = s0; V.s1 = s1; V.lp = lp; V.err = err; V.xPrev = xPrev;
    V.w = uint16_t(w);
    V.rd = uint16_t(rd);
    return acc * (1.0f / float(kBlock));
}

// ------------------------------------------------------------------ engine

class PluckEngine final : public ISynthEngine {
public:
    int32_t id() const override { return kEngineId; }
    const char* name() const override { return "PLK"; }

    PLUCK_COLD void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override
    {
        VoiceState& V = v.as<VoiceState>();
        std::memset(&V, 0, sizeof(V));
        V.line = uint8_t(claim(&v));
        V.vel = clampf(n.velocity, 0.0f, 1.0f);
        V.pitch = clampf(n.pitch, 12.0f, 120.0f);
        V.len = 0;
        V.w = 0;
        V.serial = p.serial;
        V.lastPitch = soundingPitch(V, p, v.bend);
        V.half = V.lastPitch < float(kHalfBelow);
        retune(V, p, V.lastPitch);

        const float sr = kSampleRate * (V.half ? 0.5f : 1.0f);
        const float bri = clampf(P(p, kBrightness) - 0.4f * (1.0f - V.vel), 0.0f, 1.0f);
        const float f0 = 440.0f * fexp2((V.lastPitch - 69.0f) * (1.0f / 12.0f));
        const float fcx = (400.0f + 2.0f * f0) * fexp(3.91202f * bri);   // dullest: the fundamental and a little more, on every key
        const float kappa = 1.0f - fexp(-6.28318531f * fcx / sr);
        int type = int(P(p, kExciter) + 0.5f);
        type = type < 0 ? 0 : (type >= kExciterCount ? kExciterCount - 1 : type);
        excite(V, gPool.line[V.line], type, 0.5f * clampf(P(p, kPickPos), 0.0f, 1.0f), kappa, hash32(++gPool.clock * 2654435761U));
        v.level = 1.0f;
    }

    PLUCK_COLD void noteOff(SynthVoice& v, const SynthParams& p) override
    {
        VoiceState& V = v.as<VoiceState>();
        V.released = 1;
        retune(V, p, V.lastPitch);
    }

    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override
    {
        VoiceState& V = v.as<VoiceState>();
        if (V.line >= kLines || gPool.owner[V.line] != &v) {
            return false;   // the line was taken over by a newer note
        }
        const float pitch = soundingPitch(V, p, v.bend);
        if (p.serial != V.serial || pitch != V.lastPitch) {
            V.serial = p.serial;
            V.lastPitch = pitch;
            retune(V, p, pitch);
        }
        int16_t* buf = gPool.line[V.line];
        const float scale = 1.0f / kLineUnit;
        float mean;
        if (V.half) {
            mean = V.disp ? runBlock<true, true>(V, buf, g0 * scale, gInc * scale, L, R)
                          : runBlock<false, true>(V, buf, g0 * scale, gInc * scale, L, R);
        } else {
            mean = V.disp ? runBlock<true, false>(V, buf, g0 * scale, gInc * scale, L, R)
                          : runBlock<false, false>(V, buf, g0 * scale, gInc * scale, L, R);
        }
        v.level = mean * scale * 4.0f;
        V.quiet = mean < kSilence ? uint8_t(V.quiet < 255 ? V.quiet + 1 : 255) : 0;
        if (V.quiet >= V.quietLimit || v.fading) {
            gPool.owner[V.line] = nullptr;
            return false;
        }
        return true;
    }
};

PluckEngine gPluck;

}  // namespace

ISynthEngine* engine() { return &gPluck; }

#ifdef PLUCK_WORK_POOL
uint32_t workBytes() { return sizeof(Pool); }
void setWork(void* work, uint32_t bytes)
{
    if (bytes >= sizeof(Pool)) {
        gPoolAt = static_cast<Pool*>(work);
    }
}
#endif

}  // namespace pluck
}  // namespace audio
}  // namespace ot
