// Lo-Fi: a live stereo master insert plugin (lofi.plg), the sound of a small chip's output stage. A DAC of Bits bits
// (4..16, 16 = none) with an optional Ladder character (uneven top steps: the code's top six bits carry weight errors), a
// sample and hold at Rate (32 kHz .. 4 kHz, no interpolation: the aliasing is the point), a Speaker shape (TV: high-pass
// 80 Hz, low-pass 6 kHz, +2 dB at 3 kHz. Handheld: 300 Hz, 4.5 kHz, +4 dB at 1.5 kHz. Monitor: 40 Hz, 12 kHz) and a dry /
// wet Mix. The TV and Handheld shapes are not level neutral: the host's int16 conversion clips what passes full scale.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the LOFI module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx, modules/lofi/src/lofi.cpp,
// commit 6e5a7da), MIT licence, text in licenses/LICENSE-OpenTracker-fx-MIT.txt (shipped next to lofi.plg).
// The ladder error weights are the table kLadderErr of OpenTracker's chip_dsp.h (Apache-2.0,
// https://github.com/sandlbn/polyend-tracker-re, branch v3_fixes, commit 789d55a4c5e8), text in
// licenses/LICENSE-OpenTracker-Apache-2.0.txt. The speaker filters follow lofi.cpp.
//
// Modified: from the offline .ptfx port (editor-fx-sdk/effects/ot_lofi.c on branch ptfx-ot): back to stereo (one filter
// state per channel, the sample and hold clock shared), float in and out on the plugin ABI (plug_abi.h), all state in the
// host's instance memory, the sample rate from the host (44117.6 Hz on the MK66). No block-rate crossfade on a Rate or
// Speaker change and no wet ramp (as in the .ptfx port). Filter coefficients use the host's tanf and powf.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c
float tanf(float);
float powf(float, float);
float sqrtf(float);

static const char* const kRateNames[8] = {"Off", "32 kHz", "22 kHz", "16 kHz", "11 kHz", "8 kHz", "6 kHz", "4 kHz"};
static const char* const kSpeakerNames[4] = {"Off", "TV", "Handheld", "Monitor"};
static const plug_param kParams[5] = {
    {"Bits", "bit", 4.0f, 16.0f, 8.0f, 1.0f, 0, 0, 0},
    {"Rate", "", 0.0f, 7.0f, 4.0f, 1.0f, 8, 0, kRateNames},
    {"Ladder", "%", 0.0f, 100.0f, 0.0f, 1.0f, 0, 0, 0},
    {"Speaker", "", 0.0f, 3.0f, 1.0f, 1.0f, 4, 0, kSpeakerNames},
    {"Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, 0},
};

static const float k_rate_hz[8] = {0.0f, 32000.0f, 22050.0f, 16000.0f, 11025.0f, 8000.0f, 6000.0f, 4000.0f};
// hp, lp, bump Hz, bump dB
static const float k_shape[4][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {80.0f, 6000.0f, 3000.0f, 2.0f},
    {300.0f, 4500.0f, 1500.0f, 4.0f},
    {40.0f, 12000.0f, 0.0f, 0.0f},
};
// chip_dsp.h kLadderErr: weight error of bit 0 (lowest of the top six) .. bit 5
static const float k_ladder_err[6] = {0.05f, 0.1f, 0.15f, 0.2f, 0.3f, -0.7f};

typedef struct
{
    float hp, lp, s1, s2, held;
} Chan;

typedef struct
{
    float fs;
    float err[64];   // ladder error per top-six-bit code
    // raw parameters
    int bits, rate, ladder, speaker;
    float mix;
    // derived
    int quant, hold, use_ladder, use_speaker, use_bump, rate_new;
    float q, iq, amount, period;
    float ghp, glp, bp_g, bp_h, kg, bump_gain;
    // state
    float phase;
    Chan c[2];
} Inst;

static inline float absf(float x) { return __builtin_fabsf(x); }
static inline float flush(float x) { return absf(x) < 1.0e-20f ? 0.0f : x; }

static float one_pole_g(const Inst* s, float hz)
{
    const float g = tanf(3.14159265f * hz / s->fs);
    return g / (1.0f + g);
}

static void recompute(Inst* s)
{
    s->quant = s->bits < 16;   // a ladder at 16 bits would be -90 dB: not run
    s->use_ladder = s->quant && s->ladder > 0;
    s->q = (float)(1 << (s->bits < 16 ? s->bits - 1 : 15));
    s->iq = 1.0f / s->q;
    s->amount = (float)s->ladder * 0.01f;
    s->hold = s->rate != 0;
    s->period = 1.0f;
    if (s->hold)
    {
        s->period = s->fs / k_rate_hz[s->rate];
        if (s->period < 1.0f) s->period = 1.0f;
    }
    if (s->rate_new)
    {
        s->phase = s->period;
        s->rate_new = 0;
    }
    s->use_speaker = s->speaker != 0;
    s->use_bump = 0;
    if (s->use_speaker)
    {
        const float* sh = k_shape[s->speaker];
        s->ghp = one_pole_g(s, sh[0]);
        s->glp = one_pole_g(s, sh[1]);
        s->use_bump = sh[2] > 0.0f;
        if (s->use_bump)
        {
            const float kk = 1.0f;   // the bump's damping (Q 1)
            const float g = tanf(3.14159265f * sh[2] / s->fs);
            s->bp_g = g;
            s->bp_h = 1.0f / (1.0f + g * (g + kk));
            s->kg = g + kk;
            // the bump's peak is scaled so the whole shape reaches its dB there (the one-poles' own loss at the bump made
            // up: an analog estimate)
            const float f = sh[2], a = f / sh[1], b = sh[0] / f;
            const float loss = sqrtf((1.0f + a * a) * (1.0f + b * b));
            s->bump_gain = (powf(10.0f, sh[3] / 20.0f) * loss - 1.0f) * kk;
        }
        else
            for (int k = 0; k < 2; k++) s->c[k].s1 = s->c[k].s2 = 0.0f;   // a band-pass that comes in later starts clean
    }
}

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    const int iv = (int)(value + (value >= 0.0f ? 0.5f : -0.5f));
    switch (index)
    {
        case 0: s->bits = iv < 4 ? 4 : (iv > 16 ? 16 : iv); break;
        case 1:
            s->rate = iv < 0 ? 0 : (iv > 7 ? 7 : iv);
            s->rate_new = 1;
            break;
        case 2: s->ladder = iv < 0 ? 0 : (iv > 100 ? 100 : iv); break;
        case 3: s->speaker = iv < 0 ? 0 : (iv > 3 ? 3 : iv); break;
        case 4: s->mix = value < 0.0f ? 0.0f : (value > 100.0f ? 1.0f : value * 0.01f); break;
        default: return;
    }
    recompute(s);
}

static void reset(void* p)
{
    Inst* s = (Inst*)p;
    for (int k = 0; k < 2; k++) s->c[k].hp = s->c[k].lp = s->c[k].s1 = s->c[k].s2 = s->c[k].held = 0.0f;
    s->phase = 0.0f;
}

static int32_t init(void* p, const plug_host* host)
{
    Inst* s = (Inst*)p;
    s->fs = host->sample_rate > 0.0f ? host->sample_rate : 44117.647f;
    for (int i = 0; i < 64; i++)
    {
        float e = 0.0f;
        for (int k = 0; k < 6; k++)
            if (i & (1 << k)) e += k_ladder_err[k];
        s->err[i] = e;
    }
    reset(p);
    s->rate_new = 0;
    for (uint32_t i = 0; i < 5; i++) set_param(p, i, kParams[i].def);
    return 0;
}

// The DAC on one sample: rounded to 2^bits levels (mid-tread, clamped, an exact .5 rounds away from zero), the ladder's
// weight errors on the code's top six bits.
static inline float dac(const Inst* s, float x)
{
    const float q = s->q;
    float y = x * q;
    y = y < -q ? -q : (y > q - 1.0f ? q - 1.0f : y);
    const int c = (int)(y + (y >= 0.0f ? 0.5f : -0.5f));
    float out = (float)c * s->iq;
    if (s->use_ladder)
    {
        const uint32_t code = (uint32_t)(c + (1 << (s->bits - 1)));   // offset binary 0 .. 2^bits - 1
        const uint32_t top = s->bits >= 6 ? (code >> (s->bits - 6)) & 63u : (code << (6 - s->bits)) & 63u;
        out += s->amount * s->err[top] * s->iq;
    }
    return out;
}

// The speaker on one sample: high-pass, the bump (an SVF band-pass), low-pass.
static inline float speak(const Inst* s, Chan* c, float x)
{
    float v = (x - c->hp) * s->ghp;
    const float lo = v + c->hp;
    c->hp = lo + v;
    float y = x - lo;
    if (s->use_bump)
    {
        const float hp = (y - s->kg * c->s1 - c->s2) * s->bp_h;
        const float v1 = s->bp_g * hp;
        const float bp = v1 + c->s1;
        c->s1 = bp + v1;
        const float v2 = s->bp_g * bp;
        c->s2 = (v2 + c->s2) + v2;
        y += s->bump_gain * bp;
    }
    v = (y - c->lp) * s->glp;
    const float out = v + c->lp;
    c->lp = out + v;
    return out;
}

static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2) return;
    const float wet = s->mix, dry = 1.0f - s->mix;
    float ph = s->phase;
    for (uint32_t i = 0; i < frames; i++)
    {
        int take = 0;
        if (s->hold)
        {   // the hold of the DAC's output: the DAC only where the hold takes a value, one clock for both channels
            ph += 1.0f;
            if (ph >= s->period)
            {
                ph -= s->period;
                take = 1;
            }
        }
        for (int k = 0; k < 2; k++)
        {
            Chan* c = &s->c[k];
            const float d = ch[k][i];
            float x = d;
            if (s->hold)
            {
                if (take) c->held = s->quant ? dac(s, x) : x;
                x = c->held;
            }
            else if (s->quant)
                x = dac(s, x);
            if (s->use_speaker) x = speak(s, c, x);
            ch[k][i] = x * wet + d * dry;
        }
    }
    s->phase = ph;
    for (int k = 0; k < 2; k++)
    {
        Chan* c = &s->c[k];
        c->hp = flush(c->hp);
        c->lp = flush(c->lp);
        c->s1 = flush(c->s1);
        c->s2 = flush(c->s2);
    }
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Lo-Fi", "OpenTracker (M. Spoczynski)", 5, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
