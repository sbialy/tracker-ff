// Copied from opentracker-synths (https://github.com/opentracker-fw/opentracker-synths, modules/ttype/src/ttype.cpp, commit eb150a5f741b),
// Copyright (c) 2026 Marcin Spoczynski, MIT licence, see licenses/LICENSE-OpenTracker-synths-MIT.txt. Not modified, except this notice and the square bank scratch (TTYPE_WORK_POOL, see render; tracker-ff stack-fix).
// T-Type drum synth engine [OT] -- see ttype.h for the design notes.
//
// One block (render):
//   - the low drums (kick, snare, clap, toms, cowbell) run at half the
//     sample rate (kHalf frames: their bodies, noise bands and resonators
//     stay below ~8 kHz) into a mono buffer each, which is added to the
//     half-rate mid and side buses with its gains (level x hit gain x the
//     amplitude envelope; side: x the drum's place), ramped linearly across
//     the block; the buses are upsampled (linear) once at the end, the side
//     times Width;
//   - the rim (its resonators), the hats and the cymbal run at full rate (the
//     latter two off the bank of six squares
//     packed 16-bit phases, summed once per block into a stack buffer) and
//     are added straight into the stereo bus at their places x Width;
//   - the stereo bus is the slot bus, or a stack kit bus when Drive is on or
//     the kit master moves (a Volume FX, the steal fade): that one then goes
//     through the soft clip and the master into the slot bus;
//   - the send tap gets the kit output with each send's level-weighted mean.
// Decays are exponential from block to block and linear inside a block.
// Cost levers: half rate, parabolic sines of fixed-point phases, packed
// square banks, one-pole filters where a band edge will do, no division in a
// per-sample loop, at most kRingingMax drums at once.
#include "ttype.h"

#include <cstddef>

#include "audio/dsp.h"
#include "audio/va_dsp.h"

namespace ot {
namespace audio {
namespace ttype {

namespace {

using va::clampf;

constexpr uint32_t kHalf = kBlock / 2;           // half-rate frames per block
constexpr float kFs = kSampleRate;
constexpr float kFs2 = 0.5f * kSampleRate;       // the half rate
constexpr float kInvBlock = 1.0f / float(kBlock);
constexpr float kInvHalf = 1.0f / float(kHalf);
constexpr float kBlockT = float(kBlock) / kSampleRate;
constexpr float kSilent = 3.0e-4f;               // -70 dB: a drum is over
constexpr float kLog2_1000 = 9.9657843f;         // decay times are to -60 dB
constexpr float kLog2e = 1.44269504f;

// ---- block-rate math (kept out of line: flash) ---------------------------------
#define TT_BLOCK __attribute__((noinline))

TT_BLOCK float exp2f_(float x) { return va::fastExp2(x); }
// Level after n frames (at fs) of a decay to -60 dB in t seconds.
TT_BLOCK float decayN(float t, float n) { return exp2f_(-kLog2_1000 * n / (kFs * (t < 5.0e-4f ? 5.0e-4f : t))); }
inline float decayBlock(float t) { return decayN(t, float(kBlock)); }
// Factor of an exponential with time constant tau over n frames (at fs).
TT_BLOCK float tauN(float tau, float n) { return exp2f_(-kLog2e * n / (kFs * tau)); }
TT_BLOCK float octHz(float hz, float oct) { return hz * exp2f_(oct); }
// Phase increment per frame at rate fs (uint32: one cycle; 16-bit packed).
TT_BLOCK int32_t hzInc(float hz, float fs) { return int32_t(clampf(hz, 0.0f, 0.45f * fs) * (4294967296.0f / fs)); }
inline uint32_t hzInc16(float hz, float fs) { return uint32_t(clampf(hz, 1.0f, 0.45f * fs) * (65536.0f / fs)) & 0xFFFFu; }
// One-pole (TPT) gain G = g / (1 + g), g = tan(pi f / fs).
TT_BLOCK float onePoleG(float hz, float fs) {
    const float g = va::tanPade(va::kPi * clampf(hz, 10.0f, 0.45f * fs) / fs);
    return g / (1.0f + g);
}
// SVF coefficients (va::svfStep) at hz with damping, at rate fs.
TT_BLOCK va::SvfCoef svfAt(float hz, float damping, float fs) {
    return va::svfCoef(va::tanPade(va::kPi * clampf(hz, 10.0f, 0.45f * fs) / fs), damping);
}

// ---- per-sample building blocks ----------------------------------------------------

// int32 fixed point (31 fraction bits) -> float in [-1, 1).
inline float q31(uint32_t v) {
#if defined(__arm__) && defined(__ARM_FP)
    float f;
    __asm__("vmov %0, %1" : "=t"(f) : "r"(v));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(f));
    return f;
#else
    return float(int32_t(v)) * (1.0f / 2147483648.0f);
#endif
}
// Parabolic sine of a uint32 phase (third harmonic at -30 dB) and the same
// refined once (within 0.1 %: the pure bodies).
inline float sinP(uint32_t ph) {
    const float x = q31(ph);
    return x * (4.0f - 4.0f * __builtin_fabsf(x));
}
inline float sinR(uint32_t ph) {
    const float y = sinP(ph);
    return y + 0.225f * (y * __builtin_fabsf(y) - y);
}
inline uint32_t noiseStep(uint32_t s) { return s * 1664525u + 1013904223u; }
// Two 16-bit phase accumulators in one word.
inline uint32_t add16x2(uint32_t a, uint32_t b) {
#if defined(__arm__) && defined(__ARM_FEATURE_SIMD32)
    uint32_t r;
    __asm__("uadd16 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
#else
    return ((a & 0x7FFF7FFFu) + (b & 0x7FFF7FFFu)) ^ ((a ^ b) & 0x80008000u);
#endif
}
// Clamp to [-1, 1): on the M4 the saturating VCVT to Q31 and back.
inline float clamp1(float x) {
#if defined(__arm__) && defined(__ARM_FP)
    __asm__("vcvt.s32.f32 %0, %0, #31" : "+t"(x));
    __asm__("vcvt.f32.s32 %0, %0, #31" : "+t"(x));
    return x;
#else
    return clampf(x, -1.0f, 1.0f);
#endif
}
// Soft saturation of v = x x 2/3: slope 1 at 0, flat at |x| = 1.5 -> 1 (a
// cubic: cheaper than a tanh).
inline float satV(float v) {
    v = clamp1(v);
    return v * (1.5f - 0.5f * v * v);
}
// TPT one-pole: returns the low-pass, moves the state.
inline float lp1(float& s, float x, float G) {
    const float v = (x - s) * G;
    const float y = v + s;
    s = y + v;
    return y;
}

// ---- kit state (slot scratch) ---------------------------------------------------------

struct Head {   // common to every drum channel
    uint8_t on;       // 0 off, 1 ringing, 2 cut: fades out over this block, 3 hit from silence, not rendered yet
    uint8_t mode;     // Character at the hit
    uint8_t var;      // hat 0..3, cymbal 0 crash / 1 ride
    uint8_t bright;   // click / noise brightness of the hit (0.6 + 0.4 gain) x 255
    float vel;        // the hit's gain ((1 - Accent) + Accent x velocity)
    float lvl;        // amplitude envelope at the end of the last block (0..1)
};
struct Kick {
    Head h;
    uint32_t ph;
    float amp, sw, clk, z, hp;   // envelope, sweep, click envelope, click LP, Knock HP
};
struct Snare {
    Head h;
    uint32_t ph[2];
    float body, nz, sw;   // body / noise envelopes, sweep
    va::Svf f;
};
struct Clap {
    Head h;
    float be, te;            // burst / tail envelopes
    uint16_t cnt, left;      // half-rate frames to the next burst, bursts left
    va::Svf f;
};
struct Tom {
    Head h;
    uint32_t ph;
    float amp, sw, nz, z, semis;   // envelope, sweep, noise envelope, noise LP, note offset
};
struct Rim {
    Head h;
    va::Svf a, b;   // two struck resonators
};
struct Bell {
    Head h;
    uint32_t ph;   // two packed 16-bit square phases
    float fast, slow;
    va::Svf f;
};
struct Hat {
    Head h;
    float amp, h1, h2, lp;
};
struct Cym {
    Head h;
    float amp, ping, hp, lp;
};

struct Kit {
    SynthVoice* owner;
    uint32_t token;
    uint32_t seed;
    uint32_t bank[3];   // the six squares' packed phases
    float sendG[2];     // send-tap gains at the end of the last block (delay, reverb)
    float upM, upS;     // the half-rate buses' last samples (upsampling)
    Kick kick;
    Snare snare;
    Clap clap;
    Tom tom[3];
    Rim rim;
    Bell bell;
    Hat hat;
    Cym cym;
};
static_assert(sizeof(Kit) <= kSynthSlotScratchBytes, "T-Type kit state must fit the slot scratch");

struct Voice {
    uint32_t token;
    float vLast;   // the last hit's velocity
    float master;  // kit master (the framework's gain / vLast) at the end of the last block
    float ref;     // the master the last hit's first block found (the track fader's ratio)
    uint32_t hit;  // a hit since the last block: its block sets ref
};
static_assert(sizeof(Voice) <= kSynthVoiceStateBytes, "T-Type voice state too large");

float* gTapD[kSynthSlots] = {};
float* gTapR[kSynthSlots] = {};
float gTapGD[kSynthSlots] = {};
float gTapGR[kSynthSlots] = {};

inline float P(const SynthParams& p, int i) { return p.end[i]; }
inline float P0(const SynthParams& p, int i) { return p.start[i]; }
inline int charOf(const SynthParams& p, int i) { return va::enumOf(P(p, i), 2); }

// The channels' Heads by offset (Kit is standard-layout).
const uint16_t kHeadAt[kDrums] = {
    uint16_t(offsetof(Kit, kick)),   uint16_t(offsetof(Kit, snare)), uint16_t(offsetof(Kit, clap)),
    uint16_t(offsetof(Kit, tom)),    uint16_t(offsetof(Kit, tom) + sizeof(Tom)), uint16_t(offsetof(Kit, tom) + 2 * sizeof(Tom)),
    uint16_t(offsetof(Kit, rim)),    uint16_t(offsetof(Kit, bell)),  uint16_t(offsetof(Kit, hat)),
    uint16_t(offsetof(Kit, cym))};
Head& headOf(Kit& k, int d) { return *reinterpret_cast<Head*>(reinterpret_cast<uint8_t*>(&k) + kHeadAt[d]); }
const Head& headOf(const Kit& k, int d) { return headOf(const_cast<Kit&>(k), d); }

// Clear the drums, keep the ownership token and the noise seed.
void kitSilence(Kit& k) {
    static_assert(sizeof(Kit) % 4 == 0 && alignof(Kit) >= 4, "word-wise clear");
    const uint32_t t = k.token, s = k.seed;
    zeroWords(&k, sizeof(Kit));
    k.token = t;
    k.seed = s ? s : 0x2545F491u;
}
inline float brightOf(const Head& h) { return float(h.bright) * (1.0f / 255.0f); }

// ---- levels and mixing ------------------------------------------------------------------

const uint8_t kLevelParam[kDrums] = {kKickLevel, kSnareLevel, kClapLevel, kTomLoLevel, kTomMidLevel, kTomHiLevel,
                                     kRimLevel,  kBellLevel,  kHatClosed, kCymLevel};
// The drum's level at the block start / end (the hat's between Closed and Open).
float levelOf(const SynthParams& p, const Head& h, int d, bool start) {
    const float* v = start ? p.start : p.end;
    if (d != kHat) return v[kLevelParam[d]];
    return v[kHatClosed] + (v[kHatOpen] - v[kHatClosed]) * float(h.var) * (1.0f / 3.0f);
}

// One drum's block: its mono signal t and the gains it is added with (the
// envelope a0 -> a1 x level x hit gain; 0 at the end when it is cut).
struct Mix {
    float g0, g1;
};
// kOut: the kit's output trim (a drum at Level 1 peaks near -6 dBFS
// before the slot gain; eight at once stay below full scale at -9 dB).
constexpr float kOut = 0.5f;
TT_BLOCK Mix mixOf(const SynthParams& p, const Head& h, int d, float a0, float a1) {
    Mix m;
    m.g0 = levelOf(p, h, d, true) * h.vel * a0 * kOut;
    m.g1 = h.on == 2 ? 0.0f : levelOf(p, h, d, false) * h.vel * a1 * kOut;
    return m;
}
// Half rate: M += t g, S += t g x place.
TT_BLOCK void mixHalf(const float* t, float* M, float* S, const Mix& m, float place) {
    float g = m.g0;
    const float dg = (m.g1 - m.g0) * kInvHalf;
    if (place == 0.0f) {
        for (uint32_t i = 0; i < kHalf; ++i) {
            M[i] += t[i] * g;
            g += dg;
        }
        return;
    }
    float s = g * place;
    const float ds = dg * place;
    for (uint32_t i = 0; i < kHalf; ++i) {
        const float y = t[i];
        M[i] += y * g;
        S[i] += y * s;
        g += dg;
        s += ds;
    }
}
// Full rate: L += t g (1 - w), R += t g (1 + w), w = place x Width (ramped).
TT_BLOCK void mixFull(const float* t, float* L, float* R, const Mix& m, float w0, float w1) {
    float gl = m.g0 * (1.0f - w0), gr = m.g0 * (1.0f + w0);
    const float dl = (m.g1 * (1.0f - w1) - gl) * kInvBlock, dr = (m.g1 * (1.0f + w1) - gr) * kInvBlock;
    for (uint32_t i = 0; i < kBlock; ++i) {
        const float y = t[i];
        L[i] += y * gl;
        R[i] += y * gr;
        gl += dl;
        gr += dr;
    }
}

// ---- the six-square bank (hats, cymbal) ------------------------------------------------

// Engine::render's bank block (kBlock floats, 512 B) is scratch, written by bankBlock and read by the hat and the cymbal in the same
// call. On the stack it was part of the 2,856 B frame that made T-Type the second deepest synth in the audio interrupt (ref/
// stack-margin-1.2.0.md). With TTYPE_WORK_POOL (the card plugin, plug/synth/ttype_plug.cpp) it is host work memory given once through
// setWork before any audio call (SDRAM on the device, zeroed): only a ringing hat or cymbal touches it. The output does not change.
#ifdef TTYPE_WORK_POOL
float* gBankAt;
#endif

const float kMetalRatio[6] = {1.0f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f};

// out[i] = mean of the six squares (-1..1) at Hat Tune x the ratios.
TT_BLOCK void bankBlock(Kit& k, const SynthParams& p, float* out) {
    const float hz = P(p, kHatTune);
    uint32_t inc[3];
    for (int j = 0; j < 3; ++j)
        inc[j] = hzInc16(hz * kMetalRatio[2 * j], kFs) | (hzInc16(hz * kMetalRatio[2 * j + 1], kFs) << 16);
    uint32_t p0 = k.bank[0], p1 = k.bank[1], p2 = k.bank[2];
    for (uint32_t i = 0; i < kBlock; ++i) {
        p0 = add16x2(p0, inc[0]);
        p1 = add16x2(p1, inc[1]);
        p2 = add16x2(p2, inc[2]);
        const uint32_t t = ((p0 >> 15) & 0x10001u) + ((p1 >> 15) & 0x10001u) + ((p2 >> 15) & 0x10001u);
        out[i] = 1.0f - float(int32_t((t & 0xFFFFu) + (t >> 16))) * (1.0f / 3.0f);
    }
    k.bank[0] = p0;
    k.bank[1] = p1;
    k.bank[2] = p2;
}

// ---- drums ------------------------------------------------------------------------------
// Each xxxOn() starts the drum (its Head is set by the caller); each
// xxxRender() fills t (kHalf frames at half rate, or kBlock at full rate),
// returns its envelope over the block in a0 / a1 and whether it still
// sounds, and sets h.lvl. The block context:
struct Ctx {
    const SynthParams& p;
    float bend0, bend1;   // step-FX pitch (semitones) at the block start / end
    uint32_t& seed;
    const float* bank;
};
inline float bendOct(float b) { return b * (1.0f / 12.0f); }

// Kick: a sine with a pitch sweep (octaves x Sweep, time constant by
// Character) and an exponential decay, a click (a short low-passed noise
// burst, Attack), the kick's Drive (a soft clip; not Boom), Knock's thin body.
struct KickChar {
    float oct, tau, decayMul, decayMin, clickHz, hpMul;
    bool drive;
};
const KickChar kKickChars[3] = {
    {2.0f, 0.040f, 1.0f, 0.05f, 6000.0f, 0.0f, true},    // Punch
    {0.5f, 0.012f, 1.0f, 0.2f, 1500.0f, 0.0f, false},    // Boom: pure, long
    {1.0f, 0.020f, 0.5f, 0.05f, 3500.0f, 1.2f, true},    // Knock: short, thin body
};
void kickOn(Kick& d) {
    if (!d.h.on) d.ph = 0;   // a fresh kick starts at the zero crossing
    d.amp = 1.0f;
    d.sw = 1.0f;
    d.clk = 1.0f;
}
TT_BLOCK bool kickRender(Kick& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const KickChar& C = kKickChars[d.h.mode];
    const float tune = P(p, kKickTune), oct = C.oct * 2.0f * P(p, kKickSweep);
    const float sw0 = d.sw, sw1 = sw0 * tauN(C.tau, float(kBlock));
    d.sw = sw1 < 1.0e-5f ? 0.0f : sw1;
    const int32_t i0 = hzInc(octHz(tune, oct * sw0 + bendOct(c.bend0)), kFs2);
    const int32_t i1 = hzInc(octHz(tune, oct * sw1 + bendOct(c.bend1)), kFs2);
    float dt = P(p, kKickDecay) * C.decayMul;
    dt = dt < C.decayMin ? C.decayMin : dt;
    a0 = d.amp;
    a1 = a0 * decayBlock(dt);
    d.amp = a1;
    const float drv = C.drive ? P(p, kKickDrive) : 0.0f;
    const float pre = (1.0f + 3.0f * drv) * (2.0f / 3.0f), norm = 1.0f / (1.0f + 0.3f * drv);
    const float hpG = C.hpMul > 0.0f ? onePoleG(tune * C.hpMul, kFs2) : 0.0f;
    // The click rides on the body's envelope: its own level divides it back.
    const bool click = d.clk > 0.0f && P(p, kKickAttack) > 0.0f;
    const float clkA = click ? onePoleG(C.clickHz, kFs2) : 0.0f, clkK = tauN(0.002f, 2.0f);
    const float clkL = 2.0f * P(p, kKickAttack) * brightOf(d.h) / (a0 > 0.05f ? a0 : 0.05f);
    uint32_t ph = d.ph, s = c.seed;
    int32_t inc = i0;
    const int32_t di = (i1 - i0) / int32_t(kHalf);
    float z = d.z, hp = d.hp, clk = click ? d.clk : 0.0f;
    const uint32_t useDrv = drv > 0.0f, useClk = click;   // integer flags: cheap tests in the loop
    for (uint32_t i = 0; i < kHalf; ++i) {
        ph += uint32_t(inc);
        inc += di;
        float x = sinR(ph);
        x -= lp1(hp, x, hpG);   // Knock: TPT one-pole high-pass (hpG 0: off)
        if (useDrv) x = satV(x * pre) * norm;
        if (useClk) {
            s = noiseStep(s);
            x += clk * clkL * lp1(z, q31(s), clkA);
            clk *= clkK;
        }
        t[i] = x;
    }
    d.ph = ph;
    d.z = va::flush(z);
    d.hp = va::flush(hp);
    d.clk = clk < 1.0e-4f ? 0.0f : clk;
    c.seed = s;
    d.h.lvl = a1;
    return a1 > kSilent;
}

// Snare: two sines (f, f x ratio) with a short body decay and a small sweep,
// plus noise through a state-variable filter (Tone: 1..5 kHz; Character:
// high + band / band / high), Snappy the noise level, Decay the noise's.
struct SnareChar {
    float bodyMul, oct, ratio, g1, g2, fcMul, damp, cBp, cHp, nLvl;
};
const SnareChar kSnareChars[3] = {
    {0.45f, 0.4f, 1.5f, 0.6f, 0.4f, 1.0f, 0.5f, 0.6f, 0.6f, 1.0f},    // Crack: bright, a sweep
    {0.8f, 0.0f, 1.5f, 0.7f, 0.45f, 0.8f, 0.7f, 1.0f, 0.0f, 0.8f},    // Soft: more body, band noise
    {0.3f, 0.2f, 1.6f, 0.4f, 0.2f, 1.5f, 0.6f, 0.0f, 1.0f, 1.1f},     // Thin: high noise, little body
};
void snareOn(Snare& d) {
    if (!d.h.on) d.ph[0] = d.ph[1] = 0;
    d.body = 1.0f;
    d.nz = 1.0f;
    d.sw = 1.0f;
}
TT_BLOCK bool snareRender(Snare& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const SnareChar& C = kSnareChars[d.h.mode];
    const float tune = P(p, kSnareTune), decay = P(p, kSnareDecay);
    const float sw0 = d.sw, sw1 = sw0 * tauN(0.008f, float(kBlock));
    d.sw = sw1 < 1.0e-5f ? 0.0f : sw1;
    const float hz0 = octHz(tune, C.oct * sw0 + bendOct(c.bend0)), hz1 = octHz(tune, C.oct * sw1 + bendOct(c.bend1));
    const int32_t i0 = hzInc(hz0, kFs2), i1 = hzInc(hz1, kFs2);
    const int32_t di = (i1 - i0) / int32_t(kHalf);
    const int32_t j0 = hzInc(hz0 * C.ratio, kFs2), dj = (hzInc(hz1 * C.ratio, kFs2) - j0) / int32_t(kHalf);
    const float b0 = d.body, b1 = b0 * decayBlock(decay * C.bodyMul);
    const float n0 = d.nz, n1 = n0 * decayBlock(decay);
    d.body = b1 < kSilent ? 0.0f : b1;
    d.nz = n1;
    // The mix carries the larger envelope; body and noise ride on it as ratios.
    const float nl = 1.2f * P(p, kSnareSnappy) * C.nLvl * brightOf(d.h);
    a0 = n0 > b0 ? n0 : b0;
    a1 = n1 > b1 ? n1 : b1;
    const float inv0 = a0 > 1.0e-9f ? 1.0f / a0 : 0.0f, inv1 = a1 > 1.0e-9f ? 1.0f / a1 : 0.0f;
    const float rb0 = b0 * inv0, rb1 = b1 * inv1;
    float rn = n0 * inv0 * nl;
    const float drn = (n1 * inv1 * nl - rn) * kInvHalf;
    float k1 = C.g1 * rb0, k2 = C.g2 * rb0;
    const float dk1 = C.g1 * (rb1 - rb0) * kInvHalf, dk2 = C.g2 * (rb1 - rb0) * kInvHalf;
    const va::SvfCoef f = svfAt(1000.0f * exp2f_(2.3f * P(p, kSnareTone)) * C.fcMul, C.damp, kFs2);
    uint32_t pa = d.ph[0], pb = d.ph[1], s = c.seed;
    int32_t ia = i0, ib = j0;
    const float cb = C.cBp, ch = C.cHp;
    va::Svf sv = d.f;
    for (uint32_t i = 0; i < kHalf; ++i) {
        pa += uint32_t(ia);
        pb += uint32_t(ib);
        ia += di;
        ib += dj;
        s = noiseStep(s);
        float lp, bp, hp;
        va::svfStep(sv, f, q31(s), lp, bp, hp);
        t[i] = k1 * sinP(pa) + k2 * sinP(pb) + rn * (cb * bp + ch * hp);
        k1 += dk1;
        k2 += dk2;
        rn += drn;
    }
    d.ph[0] = pa;
    d.ph[1] = pb;
    d.f.s1 = va::flush(sv.s1);
    d.f.s2 = va::flush(sv.s2);
    c.seed = s;
    d.h.lvl = a1;
    return a1 > kSilent;
}

// Clap: noise through a band-pass (Tone: 0.9 .. 2.5 kHz) under a train of
// short bursts (Wide 4, Dry 3, Tight 2; ~11 / 8 / 6 ms apart), then a tail
// (Tail x the level, decaying in Decay; Tight: none).
struct ClapChar {
    uint16_t spacing;   // half-rate frames between bursts
    uint8_t bursts;
    float tail, fcMul;
};
const ClapChar kClapChars[3] = {
    {243, 4, 1.0f, 1.0f},    // Wide
    {177, 3, 0.8f, 1.15f},   // Dry
    {133, 2, 0.0f, 1.3f},    // Tight: no tail
};
void clapOn(Clap& d) {
    const ClapChar& C = kClapChars[d.h.mode];
    d.be = 1.0f;
    d.te = 0.0f;
    d.cnt = C.spacing;
    d.left = uint16_t(C.bursts - 1);
}
TT_BLOCK bool clapRender(Clap& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const ClapChar& C = kClapChars[d.h.mode];
    const float tl = C.tail * P(p, kClapTail);
    const float kb = tauN(0.0028f, 2.0f), kt = decayN(P(p, kClapDecay), 2.0f);
    const va::SvfCoef f = svfAt(900.0f * exp2f_(1.5f * P(p, kClapTone)) * C.fcMul, 0.35f, kFs2);
    const float lv = 1.6f * brightOf(d.h) / (1.0f + 0.5f * tl);   // the last burst stacks on the tail
    float be = d.be, te = d.te;
    uint32_t cnt = d.cnt, left = d.left, s = c.seed;
    va::Svf sv = d.f;
    for (uint32_t i = 0; i < kHalf; ++i) {
        if (--cnt == 0) {   // the next burst; the last one starts the tail
            cnt = C.spacing;
            if (left) {
                be = 1.0f;
                if (--left == 0) te = tl;
            }
        }
        s = noiseStep(s);
        float lp, bp, hp;
        va::svfStep(sv, f, q31(s), lp, bp, hp);
        t[i] = (be + te) * lv * bp;
        be *= kb;
        te *= kt;
    }
    d.be = be < 1.0e-6f ? 0.0f : be;
    d.te = te < 1.0e-6f ? 0.0f : te;
    d.cnt = uint16_t(cnt);
    d.left = uint16_t(left);
    d.f.s1 = va::flush(sv.s1);
    d.f.s2 = va::flush(sv.s2);
    c.seed = s;
    a0 = a1 = 1.0f;   // the envelope is in the loop
    d.h.lvl = be > te ? be : te;
    return left > 0 || d.h.lvl > kSilent;
}

// Toms: a sine with a pitch sweep (Sweep / Round / Short) and a decay, the
// note's semitones up the group, a noise attack (Tone: its level).
struct TomChar {
    float oct, tau, decayMul, noise;
};
const TomChar kTomChars[3] = {
    {0.8f, 0.050f, 1.0f, 1.0f},    // Sweep
    {0.3f, 0.030f, 1.3f, 0.5f},    // Round: pure, long
    {0.5f, 0.015f, 0.5f, 1.2f},    // Short
};
void tomOn(Tom& d, float semis) {
    if (!d.h.on) d.ph = 0;
    d.amp = 1.0f;
    d.sw = 1.0f;
    d.nz = 1.0f;
    d.semis = semis;
}
TT_BLOCK bool tomRender(Tom& d, int which, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const TomChar& C = kTomChars[d.h.mode];
    const float tune = P(p, kTomLoTune + which);
    const float sw0 = d.sw, sw1 = sw0 * tauN(C.tau, float(kBlock));
    d.sw = sw1 < 1.0e-5f ? 0.0f : sw1;
    const float st = d.semis * (1.0f / 12.0f);
    const int32_t i0 = hzInc(octHz(tune, C.oct * sw0 + st + bendOct(c.bend0)), kFs2);
    const int32_t i1 = hzInc(octHz(tune, C.oct * sw1 + st + bendOct(c.bend1)), kFs2);
    a0 = d.amp;
    a1 = a0 * decayBlock(P(p, kTomLoDecay + which) * C.decayMul);
    d.amp = a1;
    const int32_t di = (i1 - i0) / int32_t(kHalf);
    uint32_t ph = d.ph, s = c.seed;
    int32_t inc = i0;
    // The noise attack (~8 ms) while it lasts, on the body's envelope.
    const float nl = 0.8f * P(p, kTomTone) * C.noise * brightOf(d.h);
    const bool noise = d.nz > 1.0e-4f && nl > 0.0f;
    const float kn = tauN(0.008f, 2.0f), ga = noise ? onePoleG(3000.0f, kFs2) : 0.0f;
    float nz = noise ? d.nz * nl / (a0 > 0.05f ? a0 : 0.05f) : 0.0f, z = d.z;
    for (uint32_t i = 0; i < kHalf; ++i) {
        ph += uint32_t(inc);
        inc += di;
        float x = sinR(ph);
        if (noise) {
            s = noiseStep(s);
            x += nz * lp1(z, q31(s), ga);
            nz *= kn;
        }
        t[i] = x;
    }
    c.seed = s;
    d.z = va::flush(z);
    d.nz = noise ? d.nz * tauN(0.008f, float(kBlock)) : 0.0f;
    d.ph = ph;
    d.h.lvl = a1;
    return a1 > kSilent;
}

// Rim (full rate: its resonators reach 6.4 kHz, whose half-rate image would
// sing): two struck resonators (state-variable band-passes, Q ~ 8) ringing
// from an impulse; Stick / Pop / Tick set their tunings and balance.
struct RimChar {
    float fa, fb, damp, gb;
};
const RimChar kRimChars[3] = {
    {1.0f, 0.55f, 0.0625f, 0.7f},   // Stick
    {0.7f, 1.6f, 0.08f, 0.6f},      // Pop
    {1.3f, 1.0f, 0.1f, 0.0f},       // Tick: one, short
};
void rimOn(Rim& d) {
    d.a.s1 = 1.0f;   // the impulse: the band-passes' integrators struck
    d.a.s2 = 0.0f;
    d.b.s1 = 1.0f;
    d.b.s2 = 0.0f;
}
TT_BLOCK bool rimRender(Rim& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const RimChar& C = kRimChars[d.h.mode];
    const float hz = P(p, kRimTune);
    const va::SvfCoef fa = svfAt(hz * C.fa, C.damp, kFs), fb = svfAt(hz * C.fb, C.damp, kFs);
    // The ring decays by itself (e^(-2 pi damping f t)); the level follows the slower resonator.
    const float slow = hz * (C.gb > 0.0f && C.fb < C.fa ? C.fb : C.fa);
    const float lv1 = d.h.lvl * exp2f_(-kLog2e * 2.0f * va::kPi * C.damp * slow * kBlockT);
    const float gb = C.gb;
    va::Svf A = d.a, B = d.b;
    if (gb > 0.0f) {
        for (uint32_t i = 0; i < kBlock; ++i) {
            float lp, bpa, bpb, hp;
            va::svfStep(A, fa, 0.0f, lp, bpa, hp);
            va::svfStep(B, fb, 0.0f, lp, bpb, hp);
            t[i] = 0.6f * (bpa + gb * bpb);
        }
    } else {   // Tick: one resonator
        for (uint32_t i = 0; i < kBlock; ++i) {
            float lp, bpa, hp;
            va::svfStep(A, fa, 0.0f, lp, bpa, hp);
            t[i] = 0.6f * bpa;
        }
    }
    d.a.s1 = va::flush(A.s1);
    d.a.s2 = va::flush(A.s2);
    d.b.s1 = va::flush(B.s1);
    d.b.s2 = va::flush(B.s2);
    a0 = a1 = 1.0f;   // the ring is its own envelope
    d.h.lvl = lv1;
    return lv1 > kSilent;
}

// Cowbell: two squares (Tune, x 1.48) through a band-pass; a fast and a slow
// decay (Decay) summed. Clang / Bright / Low set the band and the balance.
struct BellChar {
    float tuneMul, fcMul, damp, fast;
};
const BellChar kBellChars[3] = {
    {1.0f, 2.0f, 0.15f, 0.5f},     // Clang
    {1.0f, 3.0f, 0.25f, 0.65f},    // Bright
    {0.8f, 1.6f, 0.2f, 0.4f},      // Low
};
void bellOn(Bell& d) {
    const BellChar& C = kBellChars[d.h.mode];
    d.fast = C.fast;
    d.slow = 1.0f - C.fast;
}
TT_BLOCK bool bellRender(Bell& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const BellChar& C = kBellChars[d.h.mode];
    const float hz = P(p, kBellTune) * C.tuneMul;
    const float f0 = d.fast, s0 = d.slow, f1 = f0 * decayBlock(0.05f), s1 = s0 * decayBlock(P(p, kBellDecay));
    d.fast = f1 < 1.0e-6f ? 0.0f : f1;
    d.slow = s1;
    a0 = f0 + s0;
    a1 = f1 + s1;
    const uint32_t inc = hzInc16(hz, kFs2) | (hzInc16(hz * 1.48f, kFs2) << 16);
    const va::SvfCoef f = svfAt(hz * C.fcMul, C.damp, kFs2);
    uint32_t ph = d.ph;
    va::Svf sv = d.f;
    for (uint32_t i = 0; i < kHalf; ++i) {
        ph = add16x2(ph, inc);
        const uint32_t q = (ph >> 15) & 0x10001u;
        float lp, bp, hp;
        va::svfStep(sv, f, 1.0f - float(int32_t((q & 0xFFFFu) + (q >> 16))), lp, bp, hp);
        t[i] = bp;
    }
    d.ph = ph;
    d.f.s1 = va::flush(sv.s1);
    d.f.s2 = va::flush(sv.s2);
    d.h.lvl = a1;
    return a1 > kSilent;
}

// Hat (full rate): the bank (Noisy: plus noise) through two high-passes at
// Tone (3.5 .. 10 kHz, following the bank's pitch a little) and a one-pole
// low-pass (the band's top); decay and level between Closed and Open by note.
struct HatChar {
    float noise, hpMul, lpHz, decayMul;
};
const HatChar kHatChars[3] = {
    {0.7f, 1.0f, 9000.0f, 1.0f},     // Noisy
    {0.0f, 1.0f, 9000.0f, 1.0f},     // Metal
    {0.3f, 1.4f, 12000.0f, 0.6f},    // Thin
};
// Geometric between them: closed x (open / closed)^(var / 3).
TT_BLOCK float hatDecay(const SynthParams& p, int var) {
    const float c = P(p, kHatClosedDecay), o = P(p, kHatOpenDecay);
    if (var <= 0) return c;
    if (var >= 3) return o;
    float r = o / c;
    uint32_t b;
    __builtin_memcpy(&b, &r, 4);
    const float l2 = float(int32_t((b >> 23) & 255u) - 127);
    b = (b & 0x7FFFFFu) | 0x3F800000u;
    float m;
    __builtin_memcpy(&m, &b, 4);
    m -= 1.0f;
    return c * exp2f_(float(var) * (1.0f / 3.0f) * (l2 + m * (1.3465553f - 0.34655535f * m)));
}
void hatOn(Hat& d) { d.amp = 1.0f; }
TT_BLOCK bool hatRender(Hat& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const HatChar& C = kHatChars[d.h.mode];
    a0 = d.amp;
    a1 = a0 * decayBlock(hatDecay(p, d.h.var) * C.decayMul);
    d.amp = a1;
    const float tr = clampf(0.5f + 0.5f * P(p, kHatTune) * (1.0f / 205.0f), 0.6f, 1.8f);
    const float gh = onePoleG(3500.0f * exp2f_(1.5f * P(p, kHatTone)) * C.hpMul * tr, kFs);
    const float gl = onePoleG(C.lpHz * tr, kFs);
    const float nz = C.noise * brightOf(d.h);
    float h1 = d.h1, h2 = d.h2, lp = d.lp;
    uint32_t s = c.seed;
    const float* bank = c.bank;
    for (uint32_t i = 0; i < kBlock; ++i) {
        s = noiseStep(s);
        const float x = bank[i] + nz * q31(s);
        const float y1 = x - lp1(h1, x, gh);   // two TPT one-pole high-passes, then the low-pass
        const float y2 = y1 - lp1(h2, y1, gh);
        t[i] = 3.2f * lp1(lp, y2, gl);
    }
    d.h1 = va::flush(h1);
    d.h2 = va::flush(h2);
    d.lp = va::flush(lp);
    c.seed = s;
    d.h.lvl = a1;
    return a1 > kSilent;
}

// Cymbal (full rate): the bank (Crash: plus noise) through a high-pass and a
// low-pass (Tune scales both, Tone opens them), a body (Decay) and a short
// bright ping. Ride (70): brighter, less noise, more ping. Crash / Ring / Thin.
struct CymChar {
    float noise, hpMul, decayMul, ping;
};
const CymChar kCymChars[3] = {
    {0.6f, 1.0f, 1.0f, 0.6f},     // Crash
    {0.1f, 0.8f, 1.3f, 1.0f},     // Ring
    {0.3f, 1.6f, 0.6f, 0.5f},     // Thin
};
void cymOn(Cym& d) {
    d.amp = 1.0f;
    d.ping = 1.0f;
}
TT_BLOCK bool cymRender(Cym& d, Ctx& c, float* t, float& a0, float& a1) {
    const SynthParams& p = c.p;
    const CymChar& C = kCymChars[d.h.mode];
    const bool ride = d.h.var == 1;
    const float tune = P(p, kCymTune), tone = P(p, kCymTone);
    a0 = d.amp;
    a1 = a0 * decayBlock(P(p, kCymDecay) * C.decayMul);
    const float r0 = d.ping, r1 = r0 * decayBlock(0.08f);
    d.amp = a1;
    d.ping = r1 < 1.0e-5f ? 0.0f : r1;
    const float gh = onePoleG(2500.0f * tune * exp2f_(tone) * C.hpMul * (ride ? 1.5f : 1.0f), kFs);
    const float gl = onePoleG(7000.0f * tune * exp2f_(0.7f * tone) * (ride ? 1.6f : 1.0f), kFs);
    const float nz = C.noise * (ride ? 0.3f : 1.0f) * brightOf(d.h);
    const float pk = C.ping * (ride ? 1.3f : 1.0f);
    // The ping rides on the body's envelope as a ratio.
    float pr = a0 > 1.0e-6f ? pk * r0 / a0 : 0.0f;
    const float dpr = ((a1 > 1.0e-6f ? pk * r1 / a1 : 0.0f) - pr) * kInvBlock;
    float hp = d.hp, lp = d.lp;
    uint32_t s = c.seed;
    const float* bank = c.bank;
    for (uint32_t i = 0; i < kBlock; ++i) {
        s = noiseStep(s);
        const float x = bank[i] + nz * q31(s);
        const float y = x - lp1(hp, x, gh);
        t[i] = 1.6f * (lp1(lp, y, gl) + pr * y);
        pr += dpr;
    }
    d.hp = va::flush(hp);
    d.lp = va::flush(lp);
    c.seed = s;
    d.h.lvl = a1;
    return a1 > kSilent;
}

// ---- per-drum sends (send tap) --------------------------------------------------------
// As PERC: the kit output (L + R) / 2 feeds the send buses with each send's
// level-weighted mean over the ringing drums (from the envelopes at the
// block end, ramped across the block). A drum alone sends exactly its own
// amount. The buses get the slot gain x Global Mixer level.
int soundOf(int d, int var) {
    static const uint8_t kSound[kDrums] = {kSKick, kSSnare, kSClap, kSTomLo, kSTomMid,
                                           kSTomHi, kSRim, kSBell, kSHat, kSCrash};
    return d == kCymbal && var == 1 ? int(kSRide) : int(kSound[d]);
}
bool kitSends(const SynthParams& p) {
    for (int i = kSends; i < kAccent; ++i)
        if (P(p, i) > 0.0f) return true;
    return false;
}
// The kit's output stage, one pass over the block: the half-rate mid / side
// buses upsampled (linear, the side x Width), plus the full-rate drums' stereo
// bus, through Drive (kDrive) and the kit master into the slot bus, and the
// send tap (tap) from the same samples.
struct OutStage {
    const float* hm;
    const float* hs;
    const float* acc[2];
    float* L;
    float* R;
    float* tapD;   // never nullptr with kTap (a dummy of kBlock floats instead)
    float* tapR;
    float pm, ps;          // the half-rate buses' previous samples (in / out)
    float w, dw;           // Width ramp
    float pre, dpre;       // Drive pre-gain (x 2/3 for satV) ramp
    float g, dg;           // the gain after Drive (make-up x the kit master) ramp
    float d, dd, e, de;    // send-tap gains (x 1/2: (L + R) / 2) ramps
    uint32_t tap;          // the send tap is on
};
template <bool kDrive>
struct OutFrame {
    float w, pre, g, d, e;
    float dw, dpre, dg, dd, de;
    uint32_t tap;
    inline void put(float a0, float a1, float m, float sd, float& L, float& R, float& D, float& E) {
        const float sw = sd * w;
        float l = a0 + m - sw, r = a1 + m + sw;
        if (kDrive) {
            l = satV(l * pre);
            r = satV(r * pre);
            pre += dpre;
        }
        l *= g;
        r *= g;
        L += l;
        R += r;
        if (tap) {
            const float x = l + r;
            D += x * d;
            E += x * e;
            d += dd;
            e += de;
        }
        w += dw;
        g += dg;
    }
};
template <bool kDrive>
TT_BLOCK void outStage(OutStage& o) {
    const float* __restrict hm = o.hm;
    const float* __restrict hs = o.hs;
    const float* __restrict a0 = o.acc[0];
    const float* __restrict a1 = o.acc[1];
    float* __restrict L = o.L;
    float* __restrict R = o.R;
    float* __restrict D = o.tapD;
    float* __restrict E = o.tapR;
    OutFrame<kDrive> f{o.w, o.pre, o.g, o.d, o.e, o.dw, o.dpre, o.dg, o.dd, o.de, o.tap};
    float pm = o.pm, ps = o.ps;
    for (uint32_t j = 0; j < kHalf; ++j) {
        const float m1 = hm[j], s1 = hs[j];
        const uint32_t i = 2 * j;
        f.put(a0[i], a1[i], 0.5f * (pm + m1), 0.5f * (ps + s1), L[i], R[i], D[i], E[i]);
        f.put(a0[i + 1], a1[i + 1], m1, s1, L[i + 1], R[i + 1], D[i + 1], E[i + 1]);
        pm = m1;
        ps = s1;
    }
    o.pm = pm;
    o.ps = ps;
}

// ---- engine ------------------------------------------------------------------------------

inline Kit& kitOf(SynthVoice& v) { return v.slotScratch->as<Kit>(); }

// ---- cost governor (kKitBudget) ---------------------------------------------------------
// Steady M4 instruction cost per block of each ringing drum (its loop and its
// mix), of the output stage while anything rings, of Drive, of the send tap
// and of the square bank (while a hat or the cymbal rings), measured on the
// M4 bench (the ttype bench components) and rounded up. A hit, or a
// parameter move that changes the cost (Drive, a send), over kKitBudget less
// 1k (the hit block's own work) or over kRingingMax drums fades the quietest
// other drums out over one block. Keep the table in step with the kernels
// (ttype.unit <= kKitBudget).
const uint16_t kDrumCost[kDrums] = {3100, 3800, 2700, 2700, 2700, 2700, 3800, 2300, 5200, 5200};
constexpr int32_t kCostBase = 4400, kCostBank = 2000, kCostDrive = 1400, kCostTap = 1900;
constexpr int32_t kBudget = kKitBudget - 1000;

inline bool sounds(const Head& h) { return h.on == 1 || h.on == 3; }
int32_t costOf(const Kit& k, const SynthParams& p, int* ringing) {
    int32_t c = 0;
    int n = 0;
    for (int j = 0; j < kDrums; ++j)
        if (sounds(headOf(k, j))) {
            ++n;
            c += kDrumCost[j];
        }
    if (ringing) *ringing = n;
    if (!n) return 0;
    c += kCostBase;
    if (sounds(k.hat.h) || sounds(k.cym.h)) c += kCostBank;
    if (P(p, kDrive) > 0.0f) c += kCostDrive;
    if (kitSends(p)) c += kCostTap;
    return c;
}
// Cut the quietest others while the kit is over the cap or the budget (keep:
// the new hit, -1 none).
void enforceCap(Kit& k, const SynthParams& p, int keep) {
    for (;;) {
        int n = 0;
        if (costOf(k, p, &n) <= kBudget && n <= kRingingMax) return;
        int q = -1;
        float ql = 0.0f;
        for (int j = 0; j < kDrums; ++j) {
            const Head& h = headOf(k, j);
            const float l = h.lvl * h.vel * levelOf(p, h, j, false);
            if (sounds(h) && j != keep && (q < 0 || l < ql)) {
                q = j;
                ql = l;
            }
        }
        if (q < 0) return;
        Head& c = headOf(k, q);
        c.on = c.on == 3 ? 0 : 2;   // never sounded: gone at once; else a fade over this block
        if (!c.on) c.lvl = 0.0f;
    }
}

const uint8_t kCharParam[kDrums] = {kKickChar, kSnareChar, kClapChar, kTomChar, kTomChar, kTomChar,
                                    kRimChar,  kBellChar,  kHatChar,  kCymChar};

// Start drum d (the caller then sets its Head).
void drumOn(Kit& k, Drum d, float semis) {
    switch (d) {
        case kKick: kickOn(k.kick); break;
        case kSnare: snareOn(k.snare); break;
        case kClap: clapOn(k.clap); break;
        case kTomLo:
        case kTomMid:
        case kTomHi: tomOn(k.tom[d - kTomLo], semis); break;
        case kRim: rimOn(k.rim); break;
        case kCowbell: bellOn(k.bell); break;
        case kHat: hatOn(k.hat); break;
        default: cymOn(k.cym); break;
    }
}
bool drumRender(Kit& k, int d, Ctx& c, float* t, float& a0, float& a1) {
    switch (d) {
        case kKick: return kickRender(k.kick, c, t, a0, a1);
        case kSnare: return snareRender(k.snare, c, t, a0, a1);
        case kClap: return clapRender(k.clap, c, t, a0, a1);
        case kTomLo:
        case kTomMid:
        case kTomHi: return tomRender(k.tom[d - kTomLo], d - kTomLo, c, t, a0, a1);
        case kRim: return rimRender(k.rim, c, t, a0, a1);
        case kCowbell: return bellRender(k.bell, c, t, a0, a1);
        case kHat: return hatRender(k.hat, c, t, a0, a1);
        default: return cymRender(k.cym, c, t, a0, a1);
    }
}

class Engine final : public ISynthEngine {
public:
    int32_t id() const override { return kEngineId; }
    const char* name() const override { return "TTYPE"; }

    void slotReset(SynthSlotScratch& slot) override { kitSilence(slot.as<Kit>()); }

    void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override {
        Kit& k = kitOf(v);
        Voice& tv = v.as<Voice>();
        if (!n.reused || tv.token != k.token || k.owner != &v) {
            // A new voice takes the kit over; its drums keep ringing when
            // another, still-owning record of this slot holds them (then the
            // old voice's master and fader ratio come along).
            const bool keep = k.owner && k.owner != &v && k.owner->slotScratch == v.slotScratch &&
                              k.owner->as<Voice>().token == k.token;
            float master = 1.0f, ref = 1.0f;
            if (keep) {
                master = k.owner->as<Voice>().master;
                ref = k.owner->as<Voice>().ref;
            } else {
                kitSilence(k);
            }
            k.owner = &v;
            tv.token = ++k.token;
            tv.master = master;
            tv.ref = ref;
        }
        // A kit master left by a Volume FX (master / the fader ratio) stays with
        // the drums it scaled; the kit goes on at the fader ratio.
        const float fold = tv.ref > 1.0e-6f ? tv.master / tv.ref : 1.0f;
        if (fold != 1.0f) {
            for (int j = 0; j < kDrums; ++j) {
                Head& hj = headOf(k, j);
                hj.vel = hj.vel * fold > 1.0f ? 1.0f : hj.vel * fold;
            }
        }
        tv.master = tv.ref;
        tv.hit = 1;
        tv.vLast = n.velocity;
        int var = 0;
        float semis = 0.0f;
        const Drum d = drumForNote(n.note, &var, &semis);
        if (d == kNone) return;
        Head& h = headOf(k, d);
        h.var = uint8_t(var);
        h.mode = uint8_t(charOf(p, kCharParam[d]));
        drumOn(k, d, semis);
        const float a = clampf(P(p, kAccent), 0.0f, 1.0f), vel = clampf(n.velocity, 0.0f, 1.0f);
        const float g = (1.0f - a) + a * vel;
        h.vel = g;
        h.bright = uint8_t((0.6f + 0.4f * g) * 255.0f + 0.5f);
        h.on = h.on == 1 || h.on == 2 ? 1 : 3;
        h.lvl = 1.0f;
        enforceCap(k, p, d);
    }

    void noteOff(SynthVoice& v, const SynthParams& p) override {
        (void)v;
        (void)p;   // one-shot drums: the gate does not matter
    }

    bool setSendTap(int slot, float* delay, float* reverb, float gainD, float gainR) override {
        ttype::setSendTap(slot, delay, reverb, gainD, gainR);
        return true;
    }

    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override {
        Kit& k = kitOf(v);
        Voice& tv = v.as<Voice>();
        if (k.owner != &v || tv.token != k.token) return false;   // handed over
        if (p.moving) enforceCap(k, p, -1);   // a macro moved Drive or a send
        // Kit master: the framework's gain relative to the last hit's velocity
        // (each hit carries its own), so a Volume FX on an empty step scales
        // the ringing kit; the steal / cut fade ramps it to 0.
        const float gEnd = g0 + gInc * float(kBlock);
        const bool fading = v.fading;
        const float m0 = tv.master;
        float m1 = fading ? 0.0f : (tv.vLast > 1.0e-6f ? clampf(gEnd / tv.vLast, 0.0f, 2.0f) : 1.0f);
        if (__builtin_fabsf(m1 - 1.0f) < 1.0e-4f) m1 = 1.0f;   // ramp rounding: the plain path
        tv.master = m1;
        if (tv.hit && !fading) tv.ref = m1;   // the fader ratio a hit plays at
        tv.hit = 0;
        const float dr0 = P0(p, kDrive), dr1 = P(p, kDrive);
        const bool drive = dr0 > 0.0f || dr1 > 0.0f;
        alignas(16) float acc[2][kBlock];
#ifdef TTYPE_WORK_POOL
        float* const bank = gBankAt;   // host memory (setWork), see gBankAt
#else
        alignas(16) float bank[kBlock];
#endif
        alignas(16) float t[kBlock];
        alignas(16) float hm[kHalf], hs[kHalf];
        zeroWords(acc, sizeof(acc));
        zeroWords(hm, sizeof(hm));
        zeroWords(hs, sizeof(hs));
        if (k.hat.h.on || k.cym.h.on) bankBlock(k, p, bank);
        Ctx c{p, v.bendStart, v.bend, k.seed, bank};
        const int tapSlot = v.slot < kSynthSlots ? v.slot : 0;
        float* const tapD = gTapD[tapSlot];
        float* const tapR = gTapR[tapSlot];
        const bool tapped = (tapD || tapR) && kitSends(p);
        float wSum = 0.0f, wD = 0.0f, wR = 0.0f, level = 0.0f;
        bool any = false;
        for (int d = 0; d < kDrums; ++d) {
            Head& h = headOf(k, d);
            if (!h.on) continue;
            float a0 = 1.0f, a1 = 1.0f;
            bool on = drumRender(k, d, c, t, a0, a1);
            const Mix m = mixOf(p, h, d, a0, a1);
            const float pl = placeOf(Drum(d), h.var);
            if (d < kHat && d != kRim) mixHalf(t, hm, hs, m, pl);   // the half-rate drums
            else mixFull(t, acc[0], acc[1], m, pl * P0(p, kWidth), pl * P(p, kWidth));
            if (h.on == 2) on = false;   // cut: faded out in this block
            h.on = on ? 1 : 0;
            if (!on) h.lvl = 0.0f;
            const float lv = h.lvl * h.vel;
            if (lv > level) level = lv;
            any = any || on;
            if (tapped) {   // send weights: the drum's level at the block end
                const int s = soundOf(d, h.var);
                const float w = lv * levelOf(p, h, d, false);
                wSum += w;
                wD += w * P(p, sendParam(s, false));
                wR += w * P(p, sendParam(s, true));
            }
        }
        // The output stage: Drive, the kit master, the send tap.
        OutStage o;
        o.hm = hm;
        o.hs = hs;
        o.acc[0] = acc[0];
        o.acc[1] = acc[1];
        o.L = L;
        o.R = R;
        o.pm = k.upM;
        o.ps = k.upS;
        o.w = P0(p, kWidth);
        o.dw = (P(p, kWidth) - o.w) * kInvBlock;
        o.pre = (1.0f + 6.0f * dr0) * (2.0f / 3.0f);
        o.dpre = 4.0f * (dr1 - dr0) * kInvBlock;
        o.g = drive ? m0 / (1.0f + 1.2f * dr0) : m0;
        o.dg = ((drive ? m1 / (1.0f + 1.2f * dr1) : m1) - o.g) * kInvBlock;
        o.tapD = o.tapR = t;   // the drum buffer is free now: the side without a tap writes there (gain 0)
        o.d = o.dd = o.e = o.de = 0.0f;
        o.tap = tapped;
        if (tapped) {
            // From silence (a send of 0) the new amount applies at once: the
            // drum that brings it starts in this block.
            const float inv = wSum > 1.0e-9f ? 1.0f / wSum : 0.0f;
            const float gD = tapD ? wD * inv * gTapGD[tapSlot] : 0.0f, gR = tapR ? wR * inv * gTapGR[tapSlot] : 0.0f;
            const float d0 = k.sendG[0] > 0.0f ? k.sendG[0] : gD, e0 = k.sendG[1] > 0.0f ? k.sendG[1] : gR;
            o.d = 0.5f * d0;
            o.dd = 0.5f * (gD - d0) * kInvBlock;
            o.e = 0.5f * e0;
            o.de = 0.5f * (gR - e0) * kInvBlock;
            if (tapD) o.tapD = tapD;
            if (tapR) o.tapR = tapR;
            k.sendG[0] = gD;
            k.sendG[1] = gR;
        } else {
            k.sendG[0] = k.sendG[1] = 0.0f;
        }
        if (drive) outStage<true>(o);
        else outStage<false>(o);
        k.upM = o.pm;
        k.upS = o.ps;
        v.level = level > 1.0f ? 1.0f : level;
        if (fading || !any) {   // the kit is silent (or cut by a steal): free it
            kitSilence(k);
            return false;
        }
        return true;
    }
};

Engine gEngine;

}  // namespace

Drum drumForNote(uint8_t note, int* variant, float* semis) {
    if (variant) *variant = 0;
    if (semis) *semis = 0.0f;
    if (note < 48) {
        if (semis) *semis = float(int(note) - 48);
        return kTomLo;
    }
    if (note < 60) {
        if (semis) *semis = float((note - 48) % 4);
        return Drum(kTomLo + (note - 48) / 4);
    }
    if (note >= 72) note = uint8_t(60 + (note - 72) % 12);
    switch (note) {
        case 60: return kKick;
        case 61: return kRim;
        case 62: return kSnare;
        case 63:
        case 71: return kClap;
        case 64:
        case 65:
        case 66:
        case 67:
            if (variant) *variant = note - 64;
            return kHat;
        case 68: return kCowbell;
        default:
            if (variant) *variant = note - 69;
            return kCymbal;
    }
}

float placeOf(Drum d, int variant) {
    static const float kPlace[kDrums] = {0.0f, 0.0f, 0.15f, -0.4f, 0.0f, 0.4f, -0.25f, 0.3f, 0.35f, -0.3f};
    if (d == kCymbal && variant == 1) return 0.45f;
    return d >= 0 && d < kDrums ? kPlace[d] : 0.0f;
}

void setSendTap(int slot, float* delay, float* reverb, float gainD, float gainR) {
    if (slot < 0 || slot >= kSynthSlots) return;
    gTapD[slot] = delay;
    gTapR[slot] = reverb;
    gTapGD[slot] = delay ? gainD : 0.0f;
    gTapGR[slot] = reverb ? gainR : 0.0f;
}

int slotStateBytes() { return int(sizeof(Kit)); }

int32_t kitCost(const SynthSlotScratch& slot, const SynthParams& p) { return costOf(slot.as<Kit>(), p, nullptr); }

int ringing(const SynthSlotScratch& slot) {
    const Kit& k = slot.as<Kit>();
    int n = 0;
    for (int d = 0; d < kDrums; ++d) n += sounds(headOf(k, d));
    return n;
}
bool ringingDrum(const SynthSlotScratch& slot, Drum d) {
    return d >= 0 && d < kDrums && sounds(headOf(slot.as<Kit>(), d));
}

ISynthEngine* engine() { return &gEngine; }

#ifdef TTYPE_WORK_POOL
uint32_t workBytes() { return kBlock * sizeof(float); }
void setWork(void* work, uint32_t bytes) {
    if (bytes >= kBlock * sizeof(float)) gBankAt = static_cast<float*>(work);
}
#endif

}  // namespace ttype
}  // namespace audio
}  // namespace ot
