// Console: a live stereo master insert plugin (console.plg), glue saturation. A gentle tanh-style curve with a little more
// drive in the lows: a low shelf (one pole split at 120 Hz) lifts the lows before the curve and its exact inverse follows
// it, so a signal too small to bend the curve passes unchanged (no tone change at low Drive) and what is loud enough to
// bend it is shaped, the lows first. Drive 0 is transparent (the curve's knee is 60 times above full scale).
//
// Parameters: Drive 0..100 % (curve gain 0.05 .. 3.5), Weight 0..100 % (extra drive in the lows: the shelf lifts them by
// 0 .. +12 dB before the curve), Output -12..+12 dB, Mix 0..100 % (the dry path is time aligned: the shelf and its inverse
// cancel and the curve has no delay).
//
// Copyright (c) 2026 sbialy (tracker-ff). MIT licence, see synth-open/plug/LICENSE. Own design from
// textbook techniques (rational tanh, pre / de-emphasis), no third party code.
//
// The curve is s(v) = v (27 + v^2) / (27 + 9 v^2) for |v| <= 3 and +-1 beyond (the rational tanh), applied as s(g u) / g with
// u the shelved input: the small signal gain is exactly 1 for every Drive. The shelf is u = x + k lp(x), lp one pole
// (lp += a (x - lp), a = 1 - exp(-2 pi 120 / fs)), k = 0 .. 3. Its inverse is y[n] = (e[n] - c e[n-1] + c y[n-1]) / (1 + k a)
// with e the curve output and c = 1 - a. Every setting moves linearly from the value the last block ended on to its new target
// over the block, per sample (a change never steps the curve, 1/g is one division per frame). No libm in the loop: powf runs
// in set_param, expf once in init.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c

#define SHELF_HZ 120.0f
#define G_MIN 0.05f
#define G_SPAN 3.45f   // g = G_MIN + G_SPAN * Drive / 100
#define K_MAX 3.0f     // shelf gain at DC 1 + k = x4 = +12 dB

enum { P_DRIVE, P_WEIGHT, P_OUT, P_MIX, P_COUNT };

typedef struct
{
    float a, c;                     // shelf pole (lp += a (x - lp)), c = 1 - a
    float v[P_COUNT];               // the parameter values as the host delivered them
    float g0, g1, k0, k1;           // curve gain and shelf gain: the block start values and the targets
    float og0, og1, mx0, mx1;       // output gain (linear) and mix: the same
    float lp[2], e1[2], y1[2];      // shelf low-pass, last curve output, last de-emphasis output (per channel)
} Inst;

static const plug_param kParams[P_COUNT] = {
    {"Drive", "%", 0.0f, 100.0f, 25.0f, 1.0f, 0, 0, 0},
    {"Weight", "%", 0.0f, 100.0f, 30.0f, 1.0f, 0, 0, 0},
    {"Output", "dB", -12.0f, 12.0f, 0.0f, 0.5f, 0, 0, 0},
    {"Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, 0},
};

static inline float absf(float x) { return __builtin_fabsf(x); }
static inline float flush(float x) { return absf(x) < 1.0e-20f ? 0.0f : x; }

// The targets of the stored values (powf here, in set_param, never in process)
static void retarget(Inst* s)
{
    s->g1 = G_MIN + G_SPAN * 0.01f * s->v[P_DRIVE];
    s->k1 = K_MAX * 0.01f * s->v[P_WEIGHT];
    s->og1 = plug_rt_host->powf(10.0f, s->v[P_OUT] * 0.05f);
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
    for (int c = 0; c < 2; c++) s->lp[c] = s->e1[c] = s->y1[c] = 0.0f;
    s->g0 = s->g1; s->k0 = s->k1; s->og0 = s->og1; s->mx0 = s->mx1;
}

static int32_t init(void* p, const plug_host* host)
{
    Inst* s = (Inst*)p;
    const float fs = host->sample_rate > 0.0f ? host->sample_rate : 44117.647f;
    s->a = 1.0f - host->expf(-6.2831853f * SHELF_HZ / fs);
    s->c = 1.0f - s->a;
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
    const float a = s->a, c = s->c;
    float lp0 = s->lp[0], lp1 = s->lp[1], e10 = s->e1[0], e11 = s->e1[1], y10 = s->y1[0], y11 = s->y1[1];
    const float inv = 1.0f / (float)frames;
    float og = s->og0, mx = s->mx0, g = s->g0, k = s->k0;
    const float dog = (s->og1 - og) * inv, dmx = (s->mx1 - mx) * inv, dg = (s->g1 - g) * inv, dk = (s->k1 - k) * inv;
    // r = 1 / (1 + k a) ramps linearly with k (k a <= 0.05: the straight line is within 0.1 % of the curve)
    float r = 1.0f / (1.0f + k * a);
    const float dr = (1.0f / (1.0f + s->k1 * a) - r) * inv;
    for (uint32_t i = 0; i < frames; i++)
    {
        og += dog;
        mx += dmx;
        g += dg;
        k += dk;
        r += dr;
        const float ig = 1.0f / g;
        // left
        float x = L[i];
        lp0 += a * (x - lp0);
        float v = g * (x + k * lp0);
        v = 0.5f * (absf(v + 3.0f) - absf(v - 3.0f));   // clamp to +-3, the curve is flat there
        float w = v * v;
        float e = v * (27.0f + w) / (27.0f + 9.0f * w) * ig;
        float y = (e - c * e10 + c * y10) * r;
        e10 = e;
        y10 = y;
        L[i] = x + mx * (y * og - x);
        // right
        x = R[i];
        lp1 += a * (x - lp1);
        v = g * (x + k * lp1);
        v = 0.5f * (absf(v + 3.0f) - absf(v - 3.0f));
        w = v * v;
        e = v * (27.0f + w) / (27.0f + 9.0f * w) * ig;
        y = (e - c * e11 + c * y11) * r;
        e11 = e;
        y11 = y;
        R[i] = x + mx * (y * og - x);
    }
    s->lp[0] = flush(lp0); s->lp[1] = flush(lp1);
    s->e1[0] = flush(e10); s->e1[1] = flush(e11);
    s->y1[0] = flush(y10); s->y1[1] = flush(y11);
    s->g0 = s->g1; s->k0 = s->k1; s->og0 = s->og1; s->mx0 = s->mx1;
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Console", "tracker-ff", P_COUNT, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
