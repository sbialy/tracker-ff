// Copied from opentracker-synths (https://github.com/opentracker-fw/opentracker-synths, modules/eight/src/eight.cpp, commit eb150a5f741b),
// Copyright (c) 2026 Marcin Spoczynski, MIT licence, see licenses/LICENSE-OpenTracker-synths-MIT.txt. Not modified, except this notice and the voicePass scratch (EIGHT_WORK_POOL, see Link; tracker-ff stack-fix).
// 8-Type engine (see eight.h).
//
// The oscillators run as passes over a chunk (<= 128 frames), each into its
// bus, in an order where an oscillator that syncs or rings to the one before
// it (oscillator 1's is oscillator 3) runs after it and reads its events
// (wraps, square flips; if all three follow, oscillator 1 runs free). Saw and
// pulse are band-limited (va::vcoFrame, chip::pulseFrame with its edges
// clamped: a PW step stays bounded; the duty ramps from the last block) and
// sync through va::vcoSync (the reset
// band-limited); the triangle is naive (its 1 / n^2 harmonics alias little:
// chip-like) and, like the combined waves and the noise, resets naively. A
// combined wave is the other wave times the band-limited pulse as a gate
// 0..1, less its known mean (the gated wave's DC at the duty), so no DC
// blocker is needed. Noise is a 23-bit shift register stepped ~16 times per
// oscillator cycle (its top byte, held). Each oscillator goes to the filter
// or around it, through Grit (the bus rounded to the steps of a DAC spanning
// +-2) or Clean. The filter is the TPT state-variable filter (va::svfStep):
// any OR of its low / band / high-pass outputs (weights ramped at a mode
// change), k from the resonance step (2.0 .. 0.1: no self-oscillation), the
// input scaled by min(1, 2 k) (the top steps thin the pass band: the peak
// stays bounded); Dirt a cubic clip on its input, a resonance bite from the
// band level (block rate) and a soft clip on its output. The ADSR (16-step
// tables; linear attack from the current level, exponential decay / release
// to 1 / 4096), the filter envelope, glide, the PWM LFO (one per slot),
// levels and the cutoff run at block rate and are ramped across the block;
// the CHIP tricks split it at their ticks.
#include "eight.h"

#include <cstring>

#include "audio/chip_dsp.h"
#include "audio/va_dsp.h"

#if defined(__GNUC__) && !defined(__clang__)
#define EIGHT_PER_BLOCK __attribute__((optimize("Os")))
#else
#define EIGHT_PER_BLOCK
#endif

namespace ot {
namespace audio {
namespace eight {

namespace {

using namespace ot::audio::va;

constexpr float kTrim = 1.24f;      // voice -> slot bus (the ACD Init level)
constexpr float kMix = 0.5f;        // oscillator Level 100 % -> the buses (three at full: +-1.5)
constexpr float kEnvOct = 4.0f;     // Env Amt 100 % = +-4 octaves
constexpr float kFloor = 1.0f / 4096.0f;
constexpr float kNoiseRate = 16.0f; // register shifts per oscillator cycle
constexpr float kBiteMax = 0.5f;    // Dirt 100 %: k x (1 - 0.5 min(|band|, 1)) (not below the top step)

const float kAttS[16] = {0.002f,  0.008f,  0.01310f, 0.02146f, 0.03515f, 0.05757f, 0.09429f, 0.15444f,
                        0.25296f, 0.41432f, 0.67862f, 1.11152f, 1.82057f, 2.98194f, 4.88415f, 8.0f};
const float kDecS[16] = {0.006f,  0.024f,  0.03931f, 0.06439f, 0.10546f, 0.17272f, 0.28290f, 0.46336f,
                        0.75893f, 1.24304f, 2.03597f, 3.33467f, 5.46186f, 8.94591f, 14.6528f, 24.0f};
// Mode -> the low / band / high-pass weights.
const uint8_t kModeMask[7] = {1, 2, 4, 3, 5, 6, 7};

enum Route : uint8_t { kFiltGrit = 0, kFiltClean, kDirGrit, kDirClean };

struct OscCfg {
    uint8_t shape, ring, sync, route;
    float ratio;         // 2^((Semi + Fine) / 12)
    float pw, pwm;       // 0..1
    float pw0, pwLast;   // PW at the block start (the last block's), and that last value
    float lv0, lv1;      // level at the block start / end
};

struct SlotState {
    uint32_t serial;
    uint8_t valid, hasLast, glideMode, grit;
    uint8_t lvSet, mode, dirt, pad;
    OscCfg o[3];
    float cutoff, k0, k1, bite;   // curved Hz; k at the block start / end; Dirt's bite
    float drive, makeUp;          // Dirt's input gain and its make-up
    float key, envOct, envDecC;
    float w0[3], w1[3];           // mode weights at the block start / end
    float attRate, decC, relC, sustain;   // attack per frame; per-block factors; 0..1
    float gq, giq;                // Grit: 2^(bits - 1) and its inverse
    float glideC, lastPitch;
    float lfoPh, lfo0, lfo1;      // the slot's PWM LFO (triangle): phase, value at the block start / end
    chip::TricksCfg tc;
};
static_assert(sizeof(SlotState) <= kSynthSlotScratchBytes, "8-Type slot state");

struct Osc {
    float p, z, z2;      // phase; held frame; the pulse gate's held frame (combined) / the noise clock
    uint8_t hi, hi2, pad[2];
};
struct VoiceState {
    Osc o[3];
    Svf f;
    float h;             // the SVF's h at the last frame (tracked)
    float amp, fenv;     // ADSR level, filter envelope
    float pitch, target;
    float lastG, lastAmp, lastK;
    uint32_t lfsr;
    chip::Tricks tr;
    uint8_t stage, started, glide, gset, kset;
};
static_assert(sizeof(VoiceState) <= kSynthVoiceStateBytes, "8-Type voice state");

inline float dutyOf(float pw, float pwm, float lfo) { return 0.5f - 0.48f * clampf(pw + pwm * 0.5f * (1.0f + lfo), 0.0f, 1.0f); }

EIGHT_PER_BLOCK void refresh(SlotState& S, const SynthParams& p) {
    const float* E = p.end;
    for (int k = 0; k < 3; ++k) {
        OscCfg& c = S.o[k];
        c.shape = uint8_t(enumOf(E[osc(k, kWave)], kOff));
        c.pw = clampf(E[osc(k, kPw)], 0.0f, 1.0f);
        c.pwm = clampf(E[osc(k, kPwmDepth)], 0.0f, 1.0f);
        c.ratio = fastExp2((clampf(E[osc(k, kSemi)], -24.0f, 24.0f) + 0.01f * clampf(E[osc(k, kFine)], -100.0f, 100.0f)) * (1.0f / 12.0f));
        c.ring = E[osc(k, kRing)] >= 0.5f;
        c.sync = E[osc(k, kSync)] >= 0.5f;
        c.route = uint8_t((E[osc(k, kFilt)] >= 0.5f ? 0 : 2) + (E[osc(k, kClean)] >= 0.5f ? 1 : 0));
    }
    S.cutoff = curveHz(clampf(E[kCutoff], 30.0f, 12000.0f), enumOf(E[kCurve], kDark));
    S.mode = uint8_t(enumOf(E[kMode], kAll));
    const float d = clampf(E[kDirt], 0.0f, 1.0f);
    S.dirt = d > 0.0f;
    S.drive = 1.0f + 2.0f * d;
    S.makeUp = 1.0f / (1.0f + d);
    S.bite = kBiteMax * d;
    S.key = clampf(E[kKeyTrack], 0.0f, 1.0f) * (1.0f / 12.0f);
    S.envOct = kEnvOct * clampf(E[kEnvAmt], -1.0f, 1.0f);
    S.envDecC = blockCoef(clampf(E[kEnvDecay], 0.01f, 5.0f));
    S.attRate = kInvFs / attackSeconds(enumOf(E[kAttack], 15));
    S.decC = blockCoef(decaySeconds(enumOf(E[kDecay], 15)));
    S.relC = blockCoef(decaySeconds(enumOf(E[kRelease], 15)));
    S.sustain = float(enumOf(E[kSustain], 15)) * (1.0f / 15.0f);
    S.grit = uint8_t(enumOf(E[kGrit], 12));
    if (S.grit < 4) S.grit = 4;
    S.gq = float(1 << (S.grit - 2));
    S.giq = 1.0f / S.gq;
    chip::setTricks(S.tc, enumOf(E[kArpMode], chip::kArpCustom), int(E[kArpNote2] + 0.5f), int(E[kArpNote3] + 0.5f),
                    enumOf(E[kArpRate], chip::kDiv128), E[kVibRate], E[kVibDepth], E[kVibDelay], E[kSlideStart], E[kSlideTime]);
    S.glideMode = uint8_t(enumOf(E[kGlideMode], kLegatoOverlap));
    const float gt = clampf(E[kGlideTime], 0.0f, 3.0f);
    S.glideC = gt < 0.0005f ? 0.0f : fastExp2(-4.3219281f * kBlockT / gt);
    S.serial = p.serial;
    S.valid = 1;
}

constexpr uint32_t kChunk = 128;   // frames per pass at most (the scratch: 3 buses 1.5 KB, 2 Links 1.5 KB)

// The ramps of one chunk.
struct Ramp {
    Lin dt[3], du[3], lv[3];
    Lin g, k, w[3], a;   // the cutoff's g, k, the mode weights, the amplitude x the gain
};

// A master's events over a chunk for the oscillator after it: its wraps
// (frame, how far past it) and its square's flips (frame), each list ended by
// a frame no chunk reaches; its square at the chunk start.
// At most one wrap and one flip a frame (a sync reset counts as a wrap).
struct Link {
    uint8_t wAt[kChunk + 1], fAt[kChunk + 1];
    uint8_t sq0, pad[1];
    float wX[kChunk];
};
constexpr uint8_t kNever = 255;

// voicePass needs two Links (772 B each) and up to three buses (512 B each) of scratch. On the stack they made voicePass the deepest
// frame of the audio interrupt of any engine (3,728 B, the ISR chain 5,848 B against 8,424 B of stack room, ref/stack-margin-1.2.0.md).
// The buses are packed into three slots (an oscillator has one bus, three oscillators) instead of four, the Links move to host
// work memory when built as a plugin (EIGHT_WORK_POOL, plug/synth/eight_plug.cpp: setWork before any audio call, SDRAM on the
// device, zeroed, 8 aligned). Only a pass that follows or feeds a sync or ring reads a Link (a byte a frame), so SDRAM costs those passes only.
// The scratch is used by one voicePass at a time (the audio interrupt, or the offline export in the main loop while the interrupt
// returns at once), no pass depends on what an earlier call left in it, so the output does not change.
#ifdef EIGHT_WORK_POOL
Link* gLinkAt;   // host memory, 2 Links, zeroed (setWork)
#endif

// One chunk of one oscillator added into its bus (x its ramped level).
// Linked: it reads its master's events (in: sync at its wraps when sync,
// the triangle's sign from its square when ring; an empty Link otherwise)
// and records its own (out) for the next one. Saw and pulse sync
// band-limited (va::vcoSync); the triangle, the combined waves and the noise
// reset naively.
template <int kSh, bool kLinked>
__attribute__((always_inline)) inline void oscPassT(float* bus, uint32_t n, Osc& o, uint32_t& lfsr, Lin dt, Lin du, Lin lv,
                                                    const Link* in, bool sync, bool ring, Link* out) {
    float p = o.p, z = o.z, z2 = o.z2;
    bool hi = o.hi, hi2 = o.hi2;
    static const uint8_t kNoFlips[1] = {kNever};
    uint32_t rw = 0, rf = 0, nw = 0, nf = 0;
    float sign = 1.0f;
    bool sq = false;
    const uint8_t* flips = kNoFlips;
    if (kLinked) {
        if (ring) {
            sign = in->sq0 ? 1.0f : -1.0f;
            flips = in->fAt;
        }
        sq = p < 0.5f;
        out->sq0 = sq;
    }
    for (uint32_t i = 0; i < n; ++i) {
        const float d = dt.v, duty = du.v;
        float mx = -1.0f;
        if (kLinked) {
            if (in->wAt[rw] == i) mx = in->wX[rw++];
            if (flips[rf] == i) {
                sign = -sign;
                ++rf;
            }
        }
        float y;
        if (kSh == kSaw || kSh == kPulse) {
            if (kLinked && sync) {
                y = vcoSync<kSh == kSaw ? kShSaw : kShPulse>(p, z, d, duty, mx);
                hi = p < duty;
            } else {
                y = kSh == kSaw ? vcoFrame<kShSaw>(p, z, hi, d, duty) : chip::pulseFrame(p, z, hi, d, duty);
            }
        } else {
            if (kLinked && sync && mx >= 0.0f) p = mx * d - d;   // naive reset
            if (kSh == kNoise || kSh == kOff) {
                p += d;
                if (p >= 1.0f) p -= 1.0f;
                y = 0.0f;
                if (kSh == kNoise) {
                    z2 += kNoiseRate * d;
                    const int k = int(z2);
                    if (k) {
                        z2 -= float(k);
                        chip::lfsrSteps(lfsr, 23, 5, k > 18 ? 18 : k);
                        z = float(int((lfsr >> 15) & 0xFFu) - 128) * (1.0f / 128.0f);
                    }
                    y = z;
                }
            } else {
                const float pc = p < 0.0f ? 0.0f : p;   // (a naive reset may leave it below 0)
                if (kSh == kTri || kSh == kTriPulse) {
                    // The triangle naive (its harmonics fall as 1 / n^2: chip-like), at the
                    // last frame's phase (in step with the band-limited waves, one frame late).
                    y = 4.0f * __builtin_fabsf(pc - 0.5f) - 1.0f;
                    p += d;
                    if (p >= 1.0f) p -= 1.0f;
                } else if (kSh == kTriSaw) {
                    y = 0.5f * vcoFrame<kShSaw>(p, z, hi, d, 0.5f) + 0.5f * (4.0f * __builtin_fabsf(pc - 0.5f) - 1.0f);
                } else {
                    y = vcoFrame<kShSaw>(p, z, hi, d, 0.5f);
                }
                if (kLinked && kSh != kSawPulse) y *= sign;   // ring (the sign stays +1 without it)
                if (kSh == kTriPulse || kSh == kSawPulse) {
                    float q = pc;
                    const float gate = 0.5f * chip::pulseFrame(q, z2, hi2, d, duty) + duty;
                    // Less the gated wave's mean: tri d - 2 d^2, saw d^2 - d.
                    y = y * gate - (kSh == kTriPulse ? duty - 2.0f * duty * duty : duty * duty - duty);
                }
            }
        }
        if (kLinked) {
            if (p < d) {
                out->wAt[nw] = uint8_t(i);
                out->wX[nw++] = p / d;
            }
            const bool s = p < 0.5f;
            if (s != sq) {
                out->fAt[nf++] = uint8_t(i);
                sq = s;
            }
        }
        bus[i] += lv.v * y;
        dt.v += dt.d;
        du.v += du.d;
        lv.v += lv.d;
    }
    if (kLinked) {
        out->wAt[nw] = kNever;
        out->fAt[nf] = kNever;
    }
    o.p = p;
    o.z = z;
    o.z2 = z2;
    o.hi = hi;
    o.hi2 = hi2;
}
using OscPass = void (*)(float*, uint32_t, Osc&, uint32_t&, Lin, Lin, Lin, const Link*, bool, bool, Link*);
template <int kSh>
__attribute__((noinline)) void oscPlain(float* bus, uint32_t n, Osc& o, uint32_t& lfsr, Lin dt, Lin du, Lin lv, const Link*, bool,
                                        bool, Link*) {
    oscPassT<kSh, false>(bus, n, o, lfsr, dt, du, lv, nullptr, false, false, nullptr);
}
template <int kSh>
__attribute__((noinline)) void oscLinked(float* bus, uint32_t n, Osc& o, uint32_t& lfsr, Lin dt, Lin du, Lin lv, const Link* in,
                                         bool sync, bool ring, Link* out) {
    oscPassT<kSh, true>(bus, n, o, lfsr, dt, du, lv, in, sync, ring, out);
}
const OscPass kOscPlain[kWaveCount] = {oscPlain<kTri>,      oscPlain<kSaw>,      oscPlain<kPulse>,  oscPlain<kNoise>,
                                       oscPlain<kTriPulse>, oscPlain<kSawPulse>, oscPlain<kTriSaw>, oscPlain<kOff>};
const OscPass kOscLinked[kWaveCount] = {oscLinked<kTri>,      oscLinked<kSaw>,      oscLinked<kPulse>,  oscLinked<kNoise>,
                                        oscLinked<kTriPulse>, oscLinked<kSawPulse>, oscLinked<kTriSaw>, oscLinked<kOff>};

// Grit: the bus rounded to the step of a 2^bits-level DAC spanning +-2 (q = 2^(bits - 2)), in place
// (three oscillators at full level stay inside it).
void gritPass(float* x, uint32_t n, float q, float iq) {
    constexpr float kRound = 12582912.0f;   // 1.5 x 2^23: (v + kRound) - kRound rounds v to an integer
    for (uint32_t i = 0; i < n; ++i) x[i] = ((x[i] * q + kRound) - kRound) * iq;
}
void addPass(float* x, const float* y, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) x[i] += y[i];
}

// One chunk of the filter input x (and the dry bus) through the filter and
// the VCA into L / R, from frame `at` of the block's ramps. The input is
// scaled by min(1, 2 k) (the top resonance steps thin the pass band, so the
// peak stays near 2x); with Dirt the output goes through a cubic soft clip
// (its bite lowers k further, at block rate). kXfade: the mode weights move (a mode change).
template <bool kDirt, bool kXfade>
__attribute__((noinline)) void filterPass(float* L, float* R, uint32_t at, uint32_t n, const float* x, const float* dry,
                                          VoiceState& V, const SlotState& S, const Ramp& r) {
    Svf f = V.f;
    float h = V.h;
    const float fa = float(at);
    float g = r.g.v + r.g.d * fa, k = r.k.v + r.k.d * fa, a = r.a.v;
    float w0 = r.w[0].v + r.w[0].d * fa, w1 = r.w[1].v + r.w[1].d * fa, w2 = r.w[2].v + r.w[2].d * fa;
    const float dg = r.g.d, dk = r.k.d, d0 = r.w[0].d, d1 = r.w[1].d, d2 = r.w[2].d, da = r.a.d;
    const float drive = S.drive, makeUp = S.makeUp;
    const float kc = k + k, in = kc < 1.0f ? kc : 1.0f;
    const float dIn = kc < 1.0f ? dk + dk : 0.0f;
    float gin = in;
    for (uint32_t i = 0; i < n; ++i) {
        float u = x[i] * gin;
        if (kDirt) u = clip3(clampf(u * drive, -3.0f, 3.0f)) * makeUp;
        h = svfHNext(h, g, k);
        float lp, bp, hp;
        svfStep(f, SvfCoef{g, k, h}, u, lp, bp, hp);
        float y = w0 * lp + w1 * bp + w2 * hp;
        if (kDirt) y = clip3(clampf(y, -3.0f, 3.0f));
        if (dry) y += dry[i];
        y *= a;
        L[i] += y;
        R[i] += y;
        g += dg;
        k += dk;
        gin += dIn;
        if (kXfade) {
            w0 += d0;
            w1 += d1;
            w2 += d2;
        }
        a += da;
    }
    f.s1 = flush(f.s1);
    f.s2 = flush(f.s2);
    V.f = f;
    V.h = h;
}

// One chunk of the voice: the oscillators in an order where each one's
// master (the one before it, oscillator 1's being oscillator 3) has run when
// it syncs or rings to it; if all three do, oscillator 1's sync / ring is left
// out. Then Grit on the buses, the filter pass.
void voicePass(float* L, float* R, uint32_t at, uint32_t n, VoiceState& V, const SlotState& S, const Ramp& r) {
    const float fa = float(at);
    auto from = [fa](const Lin& l) { return Lin{l.v + l.d * fa, l.d}; };
    float buf[3][kChunk];   // the buses in the order they are first used (bus[route] points at its slot)
    int nbuf = 0;
#ifdef EIGHT_WORK_POOL
    Link* const link = gLinkAt;
#else
    Link link[2];
#endif
    link[0].wAt[0] = link[0].fAt[0] = link[1].wAt[0] = link[1].fAt[0] = kNever;
    link[0].sq0 = link[1].sq0 = 1;
    float* bus[4] = {nullptr, nullptr, nullptr, nullptr};
    bool dep[3];
    for (int k = 0; k < 3; ++k) dep[k] = S.o[k].sync || (S.o[k].ring && S.o[k].shape != kSaw && S.o[k].shape != kPulse &&
                                                          S.o[k].shape != kSawPulse && S.o[k].shape != kNoise);
    int start = 0;
    while (start < 3 && dep[start]) ++start;
    if (start == 3) start = 0;   // a cycle: oscillator 1 runs free
    for (int j = 0; j < 3; ++j) {
        const int k = (start + j) % 3, next = (k + 1) % 3;
        const OscCfg& c = S.o[k];
        const bool heard = c.lv0 > 0.0f || c.lv1 > 0.0f;
        const bool feeds = j < 2 && dep[next];   // the next one in the order syncs / rings to this one
        const bool mine = j > 0 && dep[k];       // this one syncs / rings to the one before
        if (!heard && !feeds) {
            // Nobody hears it or follows it: the phase runs on.
            Osc& o = V.o[k];
            o.p += float(n) * r.dt[k].v + 0.5f * float(n * (n - 1)) * r.dt[k].d;
            o.p -= float(int(o.p));
            continue;
        }
        float* b = bus[c.route];
        if (!b) {
            b = bus[c.route] = buf[nbuf++];
            std::memset(b, 0, sizeof(float) * n);
        }
        // The Link of the one before (j - 1) is link[(j + 1) & 1]; this one writes link[j & 1]
        // (when it feeds nobody, a Link nobody reads after it).
        Link* in = &link[(j + 1) & 1];
        Link* out = &link[j & 1];
        if (!mine) {
            in->wAt[0] = in->fAt[0] = kNever;   // an empty Link: no sync, ring sign +1
            in->sq0 = 1;
        }
        (mine || feeds ? kOscLinked : kOscPlain)[c.shape](b, n, V.o[k], V.lfsr, r.dt[k], from(r.du[k]), from(r.lv[k]), in, mine && c.sync,
                                                         mine && c.ring, out);
    }
    // Grit on the two Grit buses, then the filter's input and the dry bus.
    const bool grit = S.grit < 12;
    if (grit && bus[kFiltGrit]) gritPass(bus[kFiltGrit], n, S.gq, S.giq);
    if (grit && bus[kDirGrit]) gritPass(bus[kDirGrit], n, S.gq, S.giq);
    float* x = bus[kFiltGrit];
    if (!x) x = bus[kFiltClean];
    else if (bus[kFiltClean]) addPass(x, bus[kFiltClean], n);
    if (!x) {
        x = buf[nbuf];   // (no filter bus, so at most the two dry buses are in use: a slot is free)
        std::memset(x, 0, sizeof(float) * n);   // nothing into the filter: its tail rings out
    }
    float* dry = bus[kDirGrit];
    if (!dry) dry = bus[kDirClean];
    else if (bus[kDirClean]) addPass(dry, bus[kDirClean], n);
    const bool xf = r.w[0].d != 0.0f || r.w[1].d != 0.0f || r.w[2].d != 0.0f;
    if (S.dirt) xf ? filterPass<true, true>(L, R, at, n, x, dry, V, S, r) : filterPass<true, false>(L, R, at, n, x, dry, V, S, r);
    else xf ? filterPass<false, true>(L, R, at, n, x, dry, V, S, r) : filterPass<false, false>(L, R, at, n, x, dry, V, S, r);
}

inline void glideTo(float& x, float t, float c) {
    if (x == t) return;
    x = t + (x - t) * c;
    if (__builtin_fabsf(x - t) < 0.01f) x = t;
}

class EightEngine final : public ISynthEngine {
public:
    int32_t id() const override { return kEngineId; }
    const char* name() const override { return "8TYPE"; }

    void slotReset(SynthSlotScratch& sc) override { std::memset(sc.bytes, 0, sizeof(sc.bytes)); }
    void setTempo(float bpm) override { bpm_ = bpm; }

    EIGHT_PER_BLOCK void prepare(SynthSlotScratch& sc, const SynthParams& p) override {
        if (p.count < kParamCount) return;
        SlotState& S = sc.as<SlotState>();
        const float* E = p.end;
        if (!S.valid || S.serial != p.serial || p.moving) refresh(S, p);
        // Levels, the resonance and the mode weights glide from the last block's values.
        const float k = resonanceK(enumOf(E[kResonance], 15));
        S.k0 = S.lvSet ? S.k1 : k;
        S.k1 = k;
        const uint8_t m = kModeMask[S.mode];
        for (int j = 0; j < 3; ++j) {
            const float w = float((m >> j) & 1);
            S.w0[j] = S.lvSet ? S.w1[j] : w;
            S.w1[j] = w;
        }
        for (int j = 0; j < 3; ++j) {
            OscCfg& c = S.o[j];
            const float l = c.shape == kOff ? 0.0f : kMix * clampf(E[osc(j, kLevel)], 0.0f, 1.0f);
            c.lv0 = S.lvSet ? c.lv1 : l;
            c.lv1 = l;
            c.pw0 = S.lvSet ? c.pwLast : c.pw;   // a PW step ramps over the block
            c.pwLast = c.pw;
        }
        // The PWM LFO (a triangle per slot).
        float ph = S.lfoPh + clampf(E[kPwmRate], 0.05f, 20.0f) * kBlockT;
        if (ph >= 1.0f) ph -= float(int(ph));
        S.lfoPh = ph;
        const float l1 = ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);
        S.lfo0 = S.lvSet ? S.lfo1 : l1;
        S.lfo1 = l1;
        S.lvSet = 1;
    }

    EIGHT_PER_BLOCK void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override {
        if (p.count < kParamCount) return;
        SlotState& S = v.slotScratch->as<SlotState>();
        VoiceState& V = v.as<VoiceState>();
        if (!S.valid || S.serial != p.serial) refresh(S, p);
        const bool held = n.reused && V.started;
        const int mode = S.glideMode;
        const bool legato = held && (mode == kLegato || mode == kLegatoOverlap) && n.overlap && V.stage != kRel;
        const bool alwaysGlide = mode == kAlways || mode == kLegato;
        V.target = n.pitch;
        V.glide = S.glideC > 0.0f && (held || S.hasLast) && !n.fxGlide && (alwaysGlide || n.overlap);
        if (!V.glide) V.pitch = n.pitch;
        else if (!held) V.pitch = S.lastPitch;
        S.lastPitch = n.pitch;
        S.hasLast = 1;
        if (!legato) {
            V.stage = kAtt;   // from the current level
            V.fenv = 1.0f;
            chip::tricksStart(V.tr, S.tc, bpm_);
        }
        if (!n.reused) {
            V.lfsr = 0x7FFFFFu ^ (uint32_t(n.note) * 2654435761u >> 9);
            V.h = 0.0f;
            V.gset = 0;
            V.kset = 0;
        }
        V.started = 1;
        v.level = n.reused ? V.amp : 1.0f;
    }

    void noteOff(SynthVoice& v, const SynthParams&) override {
        VoiceState& V = v.as<VoiceState>();
        if (V.stage != kIdle) V.stage = kRel;
    }

    EIGHT_PER_BLOCK bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override {
        VoiceState& V = v.as<VoiceState>();
        if (!V.started || p.count < kParamCount) {
            v.level = 0.0f;
            return false;
        }
        const SlotState& S = v.slotScratch->as<SlotState>();
        // The ADSR: a linear attack (it may end inside the block: the ramp
        // then splits there), the decay to Sustain, the release to the floor.
        const float a0 = V.amp;
        uint32_t split = kBlock;
        bool done = false;
        switch (V.stage) {
            case kAtt: {
                const float a = a0 + S.attRate * float(kBlock);
                if (a >= 1.0f) {
                    split = uint32_t((1.0f - a0) / S.attRate);
                    if (split > kBlock) split = kBlock;
                    V.amp = 1.0f;
                    V.stage = kDec;
                } else {
                    V.amp = a;
                }
                break;
            }
            case kDec:
                V.amp = S.sustain + (V.amp - S.sustain) * S.decC;
                if (__builtin_fabsf(V.amp - S.sustain) < 1.0e-5f) V.amp = S.sustain;
                if (V.amp < kFloor) done = true;
                break;
            case kRel:
                V.amp *= S.relC;
                if (V.amp < kFloor) done = true;
                break;
            default: done = true; break;
        }
        // Glide, the filter envelope, the cutoff at the block end.
        const float pit0 = V.pitch;
        glideTo(V.pitch, V.target, V.glide ? S.glideC : 0.0f);
        V.fenv *= S.envDecC;
        if (V.fenv < 1.0e-6f) V.fenv = 0.0f;
        const float g1 = svfG(S.cutoff, S.key * (V.pitch + v.bend - 60.0f) + S.envOct * V.fenv);
        const float gs = V.gset ? V.lastG : g1;
        if (!V.gset) V.h = svfH(gs, S.k1);
        V.lastG = g1;
        V.gset = 1;
        // The output: the ADSR (ramped from the last block's end) x trim, the framework's gain.
        const float aEnd = done ? 0.0f : V.amp * kTrim;
        const float aStart = V.lastAmp;
        V.lastAmp = aEnd;
        const float base0 = pit0 + v.bendStart, base1 = V.pitch + v.bend;
        constexpr float kInvB = 1.0f / float(kBlock);
        const bool ticking = chip::tricksTick(S.tc);
        chip::tricksRate(V.tr, S.tc, bpm_);
        // The block's ramps (each chunk starts them at its frame).
        Ramp r;
        for (int k = 0; k < 3; ++k) {
            const OscCfg& c = S.o[k];
            r.du[k] = lin(dutyOf(c.pw0, c.pwm, S.lfo0), dutyOf(c.pw, c.pwm, S.lfo1));
            r.lv[k] = lin(c.lv0, c.lv1);
        }
        r.g = lin(gs, g1);
        // Dirt's bite: the resonance rises with the band-pass level of the last
        // block (k x (1 - bite min(|band|, 1)), not below the top step), at block rate.
        float kEnd = S.k1;
        if (S.dirt) {
            const float b = __builtin_fabsf(V.f.s1);
            kEnd *= 1.0f - S.bite * (b < 1.0f ? b : 1.0f);
            if (kEnd < 0.1f) kEnd = 0.1f;
        }
        const float kStart = V.kset ? V.lastK : kEnd;
        V.lastK = kEnd;
        V.kset = 1;
        r.k = lin(kStart, kEnd);
        for (int j = 0; j < 3; ++j) r.w[j] = lin(S.w0[j], S.w1[j]);
        const float gEnd = g0 + gInc * float(kBlock);
        const float sd = split < kBlock ? (split ? 1.0f / float(split) : 0.0f) : kInvB;
        uint32_t at = 0;
        while (at < kBlock) {
            uint32_t m = ticking ? V.tr.tick.chunk(kBlock - at) : kBlock - at;
            if (m > kChunk) m = kChunk;
            if (at < split && at + m > split) m = split - at;   // the attack's end
            const float o0 = chip::tricksOffset(V.tr, S.tc, 0.0f), o1 = chip::tricksOffset(V.tr, S.tc, float(m) * kInvFs);
            const float f0 = float(at) * kInvB, f1 = float(at + m) * kInvB, inv = 1.0f / float(m);
            const float n0 = base0 + (base1 - base0) * f0 + o0, n1 = base0 + (base1 - base0) * f1 + o1;
            const float i0 = 440.0f * kInvFs * fastExp2((n0 - 69.0f) * (1.0f / 12.0f)), i1 = 440.0f * kInvFs * fastExp2((n1 - 69.0f) * (1.0f / 12.0f));
            for (int k = 0; k < 3; ++k) {
                const float da = clampf(i0 * S.o[k].ratio, 1.0e-7f, kMaxDt), db = clampf(i1 * S.o[k].ratio, 1.0e-7f, kMaxDt);
                r.dt[k] = {da, (db - da) * inv};
            }
            // The amplitude: to full at the attack's end (`split`), then held; else the block's
            // ramp; x the framework's gain (one ramp of the product).
            const float e0 = at < split ? aStart + (aEnd - aStart) * float(at) * sd : aEnd;
            const float e1 = at < split ? aStart + (aEnd - aStart) * float(at + m) * sd : aEnd;
            const float q0 = g0 + (gEnd - g0) * f0, q1 = g0 + (gEnd - g0) * f1;
            r.a = {e0 * q0, (e1 * q1 - e0 * q0) * inv};
            voicePass(L + at, R + at, at, m, V, S, r);
            const bool t = ticking && V.tr.tick.advance(m);
            chip::tricksAdvance(V.tr, S.tc, m, t);
            at += m;
        }
        v.level = V.amp;
        if (done) {
            V.started = 0;
            V.stage = kIdle;
            V.amp = 0.0f;
            V.lastAmp = 0.0f;
            return false;
        }
        return true;
    }

private:
    float bpm_ = 120.0f;
};

EightEngine gEight;

}  // namespace

float attackSeconds(int i) { return kAttS[i < 0 ? 0 : (i > 15 ? 15 : i)]; }
float decaySeconds(int i) { return kDecS[i < 0 ? 0 : (i > 15 ? 15 : i)]; }
float curveHz(float cutoff, int curve) {
    if (curve != kDark) return cutoff;
    // Dark: 30 Hz x (cutoff / 30 Hz)^0.8 (a flatter mapping: 12 kHz -> 3.6 kHz).
    return 30.0f * fastExp2(0.8f * chip::fastLog2(cutoff * (1.0f / 30.0f)));
}
float resonanceK(int step) { return 2.0f - float(step < 0 ? 0 : (step > 15 ? 15 : step)) * (1.9f / 15.0f); }

ISynthEngine* engine() { return &gEight; }

#ifdef EIGHT_WORK_POOL
uint32_t workBytes() { return 2 * sizeof(Link); }
void setWork(void* work, uint32_t bytes) {
    if (bytes >= 2 * sizeof(Link)) gLinkAt = static_cast<Link*>(work);
}
#endif

}  // namespace eight
}  // namespace audio
}  // namespace ot
