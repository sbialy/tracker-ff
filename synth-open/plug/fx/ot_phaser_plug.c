// Phaser: a live stereo master insert plugin (phaser.plg), Classic (4 stages) and 80s (8 stages).
//
// Per channel: the input plus Feedback times the last wet sample runs through a chain of first order all-pass stages that
// share one corner frequency, the chain output mixed with the dry input cancels where the chain turns the phase by 180
// degrees (the notches). An LFO sweeps the corner on a log scale between the mode's lowest and highest frequency (Depth: how
// far up), Spread offsets the right channel's LFO by up to half a cycle.
//   Classic: 4 stages, 140 Hz .. 2.4 kHz, a sweep that lingers low and rises quickly (two notches, the warm small pedal
//            sound), Feedback up to 0.6.
//   80s:     8 stages, 90 Hz .. 4.2 kHz, a smooth (raised cosine) sweep, four notches, Feedback up to 0.8 (the deep rack sound).
// The corner is computed every 64 frames and ramped in between. Depth, Spread, Feedback and Mix ramp across the block, a Mode
// change crossfades the 4 and the 8 stage outputs over one block (no click).
//
// Copyright (c) 2026 Marcin Spoczynski (OpenTracker). MIT licence, text in licenses/LICENSE-OpenTracker-fx-MIT.txt.
// Port of the PHSR module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx, modules/phaser/src/phaser.cpp,
// commit 7899366, tree 23d58b2), with the Pade tan and the polynomial 2^x of OpenTracker 3.x audio/va_dsp.h
// (https://github.com/sandlbn/polyend-tracker-re, branch v3_fixes, commit 789d55a4c5e8, Apache-2.0, text in
// licenses/LICENSE-OpenTracker-Apache-2.0.txt, copy in synth-open/ot/audio/va_dsp.h).
//
// Modified: rewritten in C and as a stereo master insert (upstream is a synth slot insert on the IEffect interface), the
// settings are the plugin's parameters instead of an EffectSettings block, the rate increment is built in set_param, the
// stage arithmetic is multiply then add (no fused multiply add: 2 cycles on the Cortex-M4F against 3), the sample rate comes
// from the host, Mix 0 passes the bus untouched (the wet fades out over a block, then the chain sleeps and restarts empty
// with the wet fading in). All state lives in the instance.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c

#define STAGES 8
#define STEP 64u   // frames per corner update

enum { P_MODE, P_RATE, P_DEPTH, P_FB, P_SPREAD, P_MIX, P_COUNT };

typedef struct { int stages; float lo, span, fbMax; } Voice;   // corner range lo .. lo * 2^span
static const Voice kVoice[2] = {{4, 140.0f, 4.1000f, 0.6f},    // 140 Hz .. 2.4 kHz
                                {8, 90.0f, 5.5443f, 0.8f}};    // 90 Hz .. 4.2 kHz

typedef struct
{
    // settings, as delivered by set_param
    float depthT, fbT, spreadT, gT;
    uint32_t inc;              // LFO phase increment per frame (cycles x 2^32)
    uint8_t mode;
    // what the last block ended on (all zero = fresh)
    uint32_t phase;            // LFO phase (one cycle = 2^32)
    float gain;                // wet amount
    float fb, depth, spread;
    float tap;                 // 0: the 4 stage output, 1: the 8 stage output
    float a[2];                // all-pass coefficient per channel
    uint8_t mode1;             // the mode the last block ran + 1 (0: fresh)
    uint8_t upper;             // stages 5..8 ran in the last block
    float fs, pifs;            // sample rate and pi / fs
    // stage memory per channel: p[0] the last input of the chain, p[k] the last output of stage k, and the last wet sample
    float p[2][STAGES + 1];
    float last[2];
} Inst;

static const char* const kModeNames[2] = {"Classic", "80s"};
static const plug_param kParams[P_COUNT] = {
    {"Mode", "", 0.0f, 1.0f, 0.0f, 1.0f, 2, 0, kModeNames},
    {"Rate", "", 0.0f, 100.0f, 40.0f, 1.0f, 0, 0, 0},       // 0.05 .. 6.4 Hz on a log scale
    {"Depth", "%", 0.0f, 100.0f, 80.0f, 1.0f, 0, 0, 0},
    {"Feedback", "%", 0.0f, 100.0f, 30.0f, 1.0f, 0, 0, 0},
    {"Spread", "%", 0.0f, 100.0f, 25.0f, 1.0f, 0, 0, 0},
    {"Mix", "%", 0.0f, 100.0f, 50.0f, 1.0f, 0, 0, 0},
};

static inline float absf(float x) { return __builtin_fabsf(x); }
static inline float flush(float x) { return absf(x) < 1.0e-15f ? 0.0f : x; }
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// 2^x, a degree 5 polynomial on the fraction (va_dsp.h fastExp2)
static inline float fast_exp2(float x)
{
    x = clampf(x, -60.0f, 60.0f);
    const int i = (int)(x + (x >= 0.0f ? 0.5f : -0.5f));
    const float f = x - (float)i;
    const float p = 1.0f + f * (0.693147181f + f * (0.240226507f + f * (0.0555041087f + f * (0.00961812911f + f * 0.00133335581f))));
    union { uint32_t u; float f; } b;
    b.u = (uint32_t)(i + 127) << 23;
    return p * b.f;
}

// tan(x) for 0 <= x < pi / 2 (Pade 7/6, < 0.1 % up to 1.4)
static inline float tan_pade(float x)
{
    const float x2 = x * x;
    return x * (135135.0f - x2 * (17325.0f - x2 * (378.0f - x2))) / (135135.0f - x2 * (62370.0f - x2 * (3150.0f - 28.0f * x2)));
}

// The all-pass coefficient of a corner frequency (Hz)
static inline float coef(const Inst* s, float hz)
{
    const float lim = 0.45f * s->fs;
    const float t = tan_pade(s->pifs * (hz < lim ? hz : lim));
    return (t - 1.0f) / (t + 1.0f);
}

// The LFO at a phase as the sweep position 0..1: a triangle, shaped per mode (Classic: t^1.5, lingering low, 80s: a smoothstep)
static inline float sweep(uint32_t ph, int mode)
{
    const float f = (float)ph * (1.0f / 4294967296.0f);
    const float t = f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f;
    return mode == 0 ? t * __builtin_sqrtf(t) : t * t * (3.0f - 2.0f * t);
}

// One all-pass stage, one multiply: y = p0 + a (x - p1), the stage's last input becomes p0
#define STAGE(v, a, p0, p1) do { const float x_ = (v); (v) = (p0) + (a) * (x_ - (p1)); (p0) = x_; } while (0)

// STEP frames of one channel with steady settings (the hot path), the stage memory stays in registers.
// upper: the 8 stage output, else the 4 stage one.
static inline __attribute__((always_inline)) void chain_impl(float* x, float* pm, float* last, float a, float da, float g,
                                                             float fb, const int upper)
{
    float p0 = pm[0], p1 = pm[1], p2 = pm[2], p3 = pm[3], p4 = pm[4];
    float p5 = 0, p6 = 0, p7 = 0, p8 = 0;
    if (upper) { p5 = pm[5]; p6 = pm[6]; p7 = pm[7]; p8 = pm[8]; }
    float y = *last;
    const float dry = 1.0f - g, wet = g * (1.0f - 0.4f * fb);
    for (uint32_t i = 0; i < STEP; ++i)
    {
        a += da;
        const float in = x[i];
        float v = in + fb * y;
        STAGE(v, a, p0, p1);
        STAGE(v, a, p1, p2);
        STAGE(v, a, p2, p3);
        STAGE(v, a, p3, p4);
        if (upper)
        {
            STAGE(v, a, p4, p5);
            STAGE(v, a, p5, p6);
            STAGE(v, a, p6, p7);
            STAGE(v, a, p7, p8);
            p8 = v;
        }
        else
            p4 = v;
        y = v;
        x[i] = wet * v + dry * in;
    }
    *last = y;
    pm[0] = p0; pm[1] = p1; pm[2] = p2; pm[3] = p3; pm[4] = p4;
    if (upper) { pm[5] = p5; pm[6] = p6; pm[7] = p7; pm[8] = p8; }
}
static __attribute__((noinline)) void chain4(float* x, float* pm, float* last, float a, float da, float g, float fb)
{
    chain_impl(x, pm, last, a, da, g, fb, 0);
}
static __attribute__((noinline)) void chain8(float* x, float* pm, float* last, float a, float da, float g, float fb)
{
    chain_impl(x, pm, last, a, da, g, fb, 1);
}

// STEP frames while settings ramp (one block after a change): the wet, Feedback and the 4 / 8 stage crossfade move per frame.
static inline __attribute__((always_inline)) void moving_impl(float* x, float* pm, float* last, float a, float da, float g,
                                                              float dg, float fb, float dfb, float tap, float dtap,
                                                              const int upper)
{
    float p0 = pm[0], p1 = pm[1], p2 = pm[2], p3 = pm[3], p4 = pm[4];
    float p5 = 0, p6 = 0, p7 = 0, p8 = 0;
    if (upper) { p5 = pm[5]; p6 = pm[6]; p7 = pm[7]; p8 = pm[8]; }
    float y = *last;
    for (uint32_t i = 0; i < STEP; ++i)
    {
        a += da;
        g += dg;
        fb += dfb;
        tap += dtap;
        const float in = x[i];
        float v = in + fb * y;
        STAGE(v, a, p0, p1);
        STAGE(v, a, p1, p2);
        STAGE(v, a, p2, p3);
        STAGE(v, a, p3, p4);
        if (upper)
        {
            const float v4 = v;
            STAGE(v, a, p4, p5);
            STAGE(v, a, p5, p6);
            STAGE(v, a, p6, p7);
            STAGE(v, a, p7, p8);
            p8 = v;
            v = v4 + tap * (v - v4);
        }
        else
            p4 = v;
        y = v;
        x[i] = in + g * (v * (1.0f - 0.4f * fb) - in);
    }
    *last = y;
    pm[0] = p0; pm[1] = p1; pm[2] = p2; pm[3] = p3; pm[4] = p4;
    if (upper) { pm[5] = p5; pm[6] = p6; pm[7] = p7; pm[8] = p8; }
}
static __attribute__((noinline)) void moving4(float* x, float* pm, float* last, float a, float da, float g, float dg, float fb,
                                               float dfb, float tap, float dtap)
{
    moving_impl(x, pm, last, a, da, g, dg, fb, dfb, tap, dtap, 0);
}
static __attribute__((noinline)) void moving8(float* x, float* pm, float* last, float a, float da, float g, float dg, float fb,
                                               float dfb, float tap, float dtap)
{
    moving_impl(x, pm, last, a, da, g, dg, fb, dfb, tap, dtap, 1);
}

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    const float pct = 0.01f * clampf(value, 0.0f, 100.0f);
    switch (index)
    {
        case P_MODE: s->mode = value >= 0.5f ? 1 : 0; break;
        case P_RATE:
        {
            // 0.05 .. 6.4 Hz: 0.05 * 2^(7.32 Rate / 100)
            const float hz = 0.05f * fast_exp2(clampf(value, 0.0f, 100.0f) * (7.321928f / 100.0f));
            s->inc = (uint32_t)(hz / s->fs * 4294967296.0f);
            break;
        }
        case P_DEPTH: s->depthT = pct; break;
        case P_FB: s->fbT = pct; break;      // times the mode's maximum in process
        case P_SPREAD: s->spreadT = pct; break;
        case P_MIX: s->gT = pct; break;
        default: break;
    }
}

static void reset(void* p)
{
    Inst* s = (Inst*)p;
    for (int c = 0; c < 2; c++)
    {
        for (int k = 0; k <= STAGES; k++) s->p[c][k] = 0.0f;
        s->last[c] = 0.0f;
        s->a[c] = 0.0f;
    }
    s->phase = 0;
    s->gain = s->fb = s->depth = s->spread = s->tap = 0.0f;
    s->mode1 = 0;
    s->upper = 0;
}

static int32_t init(void* p, const plug_host* host)
{
    Inst* s = (Inst*)p;
    s->fs = host->sample_rate > 0.0f ? host->sample_rate : 44117.647f;
    s->pifs = 3.14159265f / s->fs;
    for (uint32_t i = 0; i < P_COUNT; i++) set_param(p, i, kParams[i].def);
    reset(p);
    return 0;
}

static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2 || frames < STEP) return;
    const int mode = s->mode;
    const Voice* vc = &kVoice[mode];
    const float depth = s->depthT, fbT = s->fbT * vc->fbMax, spread = s->spreadT, gT = s->gT;
    const float tapT = vc->stages == STAGES ? 1.0f : 0.0f;
    const int fresh = s->mode1 == 0;
    if (gT <= 0.0f && s->gain <= 0.0f)   // Mix 0 and faded out: the bus passes untouched, the chain restarts empty later
    {
        if (!fresh) reset(p);
        return;
    }
    if (fresh)   // start at the settings, the wet fading in
    {
        s->depth = depth;
        s->spread = spread;
        s->fb = fbT;
        s->tap = tapT;
        s->gain = 0.0f;
        for (int c = 0; c < 2; c++)
        {
            for (int k = 0; k <= STAGES; k++) s->p[c][k] = 0.0f;
            s->last[c] = 0.0f;
        }
    }
    // Stages 5..8 run while the 8 stage output is heard, restarting they start empty.
    const int upper = tapT > 0.0f || s->tap > 0.0f;
    if (upper && !s->upper)
        for (int c = 0; c < 2; ++c)
            for (int k = 5; k <= STAGES; ++k) s->p[c][k] = s->p[c][4];

    const uint32_t steps = frames / STEP;
    const float inv = 1.0f / (float)frames;
    const float dg = (gT - s->gain) * inv, dfb = (fbT - s->fb) * inv, dtap = (tapT - s->tap) * inv;
    float g = s->gain, fb = s->fb, tap = s->tap;
    // Steady (no ramp, the 4 or the 8 stage output alone): the cheaper loop.
    const int moving = dg != 0.0f || dfb != 0.0f || dtap != 0.0f || (tap != 0.0f && tap != 1.0f);
    float* const io[2] = {ch[0], ch[1]};
    for (uint32_t j = 0; j < steps; ++j)
    {
        const float f = (float)(j + 1u) / (float)steps;
        const float d = s->depth + (depth - s->depth) * f;
        const float sp = s->spread + (spread - s->spread) * f;
        const uint32_t ph = s->phase + s->inc * (STEP * (j + 1u));
        const uint32_t off = (uint32_t)(sp * 2147483648.0f);
        float a1[2];
        a1[0] = coef(s, vc->lo * fast_exp2(d * sweep(ph, mode) * vc->span));
        a1[1] = off ? coef(s, vc->lo * fast_exp2(d * sweep(ph + off, mode) * vc->span)) : a1[0];
        if (fresh && j == 0) s->a[0] = a1[0], s->a[1] = a1[1];
        const uint32_t i0 = j * STEP;
        for (int c = 0; c < 2; ++c)
        {
            float* const x = io[c] + i0;
            const float da = (a1[c] - s->a[c]) * (1.0f / (float)STEP);
            if (moving)
            {
                if (upper) moving8(x, s->p[c], &s->last[c], s->a[c], da, g, dg, fb, dfb, tap, dtap);
                else moving4(x, s->p[c], &s->last[c], s->a[c], da, g, dg, fb, dfb, tap, dtap);
            }
            else if (upper) chain8(x, s->p[c], &s->last[c], s->a[c], da, g, fb);
            else chain4(x, s->p[c], &s->last[c], s->a[c], da, g, fb);
            s->a[c] = a1[c];
        }
        g += dg * (float)STEP;
        fb += dfb * (float)STEP;
        tap += dtap * (float)STEP;
    }
    for (int c = 0; c < 2; ++c)
    {
        s->last[c] = flush(s->last[c]);
        for (int k = 0; k <= STAGES; ++k) s->p[c][k] = flush(s->p[c][k]);
    }
    s->phase += s->inc * frames;
    s->depth = depth;
    s->spread = spread;
    s->gain = gT;
    s->fb = fbT;
    s->tap = tapT;
    s->upper = upper ? 1 : 0;
    s->mode1 = (uint8_t)(mode + 1);
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Phaser", "OpenTracker (M. Spoczynski)", P_COUNT, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
