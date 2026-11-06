// Tube: a live stereo master insert plugin (tube.plg), warm asymmetric saturation. A triode style stage: the signal rides on
// a bias point of a soft curve, so one half-wave compresses sooner than the other and the added harmonics are mostly even
// (2nd, 4th: the warm, fat kind), then DC is blocked, a high cut (Tone) takes the top off and a dry / wet Mix blends it.
//
// Parameters: Drive 0..100 % (curve gain 0.1 .. 3), Warmth 0..100 % (bias, 0 = symmetric curve, odd harmonics only, 100 =
// far off centre), Tone 0..100 (one pole high cut 2 kHz .. 20 kHz, 100 = off), Mix 0..100 %.
//
// Copyright (c) 2026 sbialy (tracker-ff). MIT licence, see synth-open/plug/LICENSE. Own design from textbook techniques
// (biased waveshaper, DC blocker), no third party code.
//
// The curve is the rational tanh s(u) = u (27 + u^2) / (27 + 9 u^2) for |u| <= 3, +-1 beyond. The stage is
// y = (s(g x + b) - s(b)) / (g s'(b)): s(b) removes the rest offset and 1 / (g s'(b)) makes the small signal gain exactly 1
// for every Drive and Warmth (s'(b) = (9 - b^2)^2 / (9 (3 + b^2)^2), exact for the rational form). What the loud signal adds
// beyond the first order is the shape: s'' (b) gives the 2nd harmonic, s''' the 3rd, and the ratio is about 6 b / (g A)
// for a swing A, so quiet and moderate material stays even harmonic dominated. The swing's mean shifts by the asymmetry:
// a DC blocker (1 pole high pass at 6 Hz) removes it. g, b, Tone and Mix move linearly from the value the last block
// ended on to the new target over the block, per sample, and s(b) and the normalisation follow them (no step in the curve).
// No libm in process: powf and expf run in set_param.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c

#define G_MIN 0.1f
#define G_SPAN 2.9f      // g = G_MIN + G_SPAN * Drive / 100
#define B_MAX 1.0f       // bias at Warmth 100
#define TONE_LO 2000.0f  // Hz at Tone 0, x10 at Tone 99
#define DC_HZ 6.0f

enum { P_DRIVE, P_WARMTH, P_TONE, P_MIX, P_COUNT };

typedef struct
{
    float rdc;                    // DC blocker pole
    float fs;
    float v[P_COUNT];             // the values as delivered
    float g0, g1, b0, b1;         // curve gain and bias: block start and target
    float t0, t1, mx0, mx1;       // Tone coefficient (1 = off) and mix
    float x1[2], dc[2], lp[2];    // DC blocker input and output memory, tone low-pass
} Inst;

static const plug_param kParams[P_COUNT] = {
    {"Drive", "%", 0.0f, 100.0f, 25.0f, 1.0f, 0, 0, 0},
    {"Warmth", "%", 0.0f, 100.0f, 30.0f, 1.0f, 0, 0, 0},
    {"Tone", "", 0.0f, 100.0f, 80.0f, 1.0f, 0, 0, 0},
    {"Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, 0},
};

static inline float absf(float x) { return __builtin_fabsf(x); }
static inline float flush(float x) { return absf(x) < 1.0e-20f ? 0.0f : x; }

// s(u) with the clamp to +-3
static inline float curve(float u)
{
    u = 0.5f * (absf(u + 3.0f) - absf(u - 3.0f));
    const float w = u * u;
    return u * (27.0f + w) / (27.0f + 9.0f * w);
}

static void retarget(Inst* s)
{
    s->g1 = G_MIN + G_SPAN * 0.01f * s->v[P_DRIVE];
    s->b1 = B_MAX * 0.01f * s->v[P_WARMTH];
    const float t = s->v[P_TONE];
    if (t >= 100.0f) s->t1 = 1.0f;   // off: lp follows the input exactly
    else
    {
        const float hz = TONE_LO * plug_rt_host->powf(10.0f, t * (1.0f / 99.0f));
        s->t1 = 1.0f - plug_rt_host->expf(-6.2831853f * hz / s->fs);
    }
    s->mx1 = 0.01f * s->v[P_MIX];
}

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    if (index >= P_COUNT) return;
    s->v[index] = value;
    retarget(s);
}

static void reset(void* p)
{
    Inst* s = (Inst*)p;
    for (int c = 0; c < 2; c++) s->x1[c] = s->dc[c] = s->lp[c] = 0.0f;
    s->g0 = s->g1; s->b0 = s->b1; s->t0 = s->t1; s->mx0 = s->mx1;
}

static int32_t init(void* p, const plug_host* host)
{
    Inst* s = (Inst*)p;
    s->fs = host->sample_rate > 0.0f ? host->sample_rate : 44117.647f;
    s->rdc = 1.0f - 6.2831853f * DC_HZ / s->fs;
    for (uint32_t i = 0; i < P_COUNT; i++) s->v[i] = kParams[i].def;
    retarget(s);
    reset(p);
    return 0;
}

static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2 || frames == 0) return;
    float* const L = ch[0];
    float* const R = ch[1];
    const float rdc = s->rdc;
    float x10 = s->x1[0], x11 = s->x1[1], d0 = s->dc[0], d1 = s->dc[1], lp0 = s->lp[0], lp1 = s->lp[1];
    const float inv = 1.0f / (float)frames;
    float t = s->t0, mx = s->mx0, g = s->g0, b = s->b0;
    const float dt = (s->t1 - t) * inv, dmx = (s->mx1 - mx) * inv, dg = (s->g1 - g) * inv, db = (s->b1 - b) * inv;
    for (uint32_t i = 0; i < frames; i++)
    {
        t += dt;
        mx += dmx;
        g += dg;
        b += db;
        // s(b) and 1 / (g s'(b)) = 9 (3 + b^2)^2 / (g (9 - b^2)^2) follow the ramped g and b (2 divisions per frame)
        const float b2 = b * b;
        const float d3 = 3.0f + b2, q = 9.0f - b2;
        const float nrm = 9.0f * d3 * d3 / (g * q * q);
        const float s0 = b * (27.0f + b2) / (27.0f + 9.0f * b2);
        float x = L[i];
        float y = (curve(g * x + b) - s0) * nrm;
        d0 = y - x10 + rdc * d0;   // DC blocker
        x10 = y;
        lp0 += t * (d0 - lp0);
        L[i] = x + mx * (lp0 - x);
        x = R[i];
        y = (curve(g * x + b) - s0) * nrm;
        d1 = y - x11 + rdc * d1;
        x11 = y;
        lp1 += t * (d1 - lp1);
        R[i] = x + mx * (lp1 - x);
    }
    s->x1[0] = flush(x10); s->x1[1] = flush(x11);
    s->dc[0] = flush(d0); s->dc[1] = flush(d1);
    s->lp[0] = flush(lp0); s->lp[1] = flush(lp1);
    s->g0 = s->g1; s->b0 = s->b1; s->t0 = s->t1; s->mx0 = s->mx1;
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Tube", "tracker-ff", P_COUNT, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
