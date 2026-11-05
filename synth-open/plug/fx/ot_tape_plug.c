// Tape: a live stereo master insert plugin (tape.plg). Drive into a soft clip (tape saturation), a gentle high cut (head
// loss), wow (0.6 Hz) and flutter (7 Hz) as a modulated delay of up to about 2.3 ms, then a dry / wet mix.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the TAPE module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx, modules/tape/src/tape.cpp,
// commit 6e5a7da), MIT licence, text in licenses/LICENSE-OpenTracker-fx-MIT.txt (shipped next to tape.plg).
//
// Modified: from the offline .ptfx port (editor-fx-sdk/effects/ot_tape.c on branch ptfx-ot): back to stereo (one delay
// line, one high cut per channel, the wow and flutter phases shared like one tape transport), float in and out on the
// plugin ABI (plug_abi.h), all state in the host's instance memory, the sample rate from the host (44117.6 Hz on the MK66).
// The DSP per sample is the .ptfx port's: per-sample phase accumulators, no block-rate ramps, no On switch.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c

#define LINE 256           // delay line, frames (int16), power of two
#define WOW_HZ 0.6f
#define FLUTTER_HZ 7.0f
#define WOW_MAX 88.0f      // frames (~2 ms) at 100 %
#define FLUTTER_MAX 13.0f  // frames (~0.3 ms) at 100 %
#define BASE 120.0f        // centre delay (frames): room for both depths
#define LP_COEF 0.55f      // one-pole high cut (~5.6 kHz)

typedef struct
{
    int16_t line[2][LINE];
    uint32_t w;
    float lp[2];
    float phW, phF, incW, incF;
    float drive, makeup, dW, dF, mix;
} Inst;

static inline float absf(float x) { return __builtin_fabsf(x); }
static inline float flush(float x) { return absf(x) < 1.0e-20f ? 0.0f : x; }

// sin(2 pi x), x in [0, 1): parabola with one refinement (error < 0.1 %)
static inline float sin_cyc(float x)
{
    const float t = 2.0f * x - 1.0f;
    const float y = 4.0f * t * (1.0f - absf(t));
    return -(y + 0.225f * (y * absf(y) - y));
}

// Soft clip: unity slope at 0, flat at +-1.5 -> +-1
static inline float soft_clip(float x)
{
    x = 0.5f * (absf(x + 1.5f) - absf(x - 1.5f));
    return x - (4.0f / 27.0f) * x * x * x;
}

static const plug_param kParams[4] = {
    {"Drive", "%", 0.0f, 100.0f, 30.0f, 1.0f, 0, 0, 0},
    {"Wow", "%", 0.0f, 100.0f, 20.0f, 1.0f, 0, 0, 0},
    {"Flutter", "%", 0.0f, 100.0f, 10.0f, 1.0f, 0, 0, 0},
    {"Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, 0},
};

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    switch (index)
    {
        case 0:
            s->drive = 1.0f + 0.05f * value;   // x1 .. x6
            s->makeup = 1.0f / (1.0f + 0.012f * value);
            break;
        case 1: s->dW = WOW_MAX * 0.01f * value; break;
        case 2: s->dF = FLUTTER_MAX * 0.01f * value; break;
        case 3: s->mix = value < 0.0f ? 0.0f : (value > 100.0f ? 1.0f : value * 0.01f); break;
        default: break;
    }
}

static void reset(void* p)
{
    Inst* s = (Inst*)p;
    for (int c = 0; c < 2; c++)
    {
        for (int i = 0; i < LINE; i++) s->line[c][i] = 0;
        s->lp[c] = 0.0f;
    }
    s->w = 0;
}

static int32_t init(void* p, const plug_host* host)
{
    Inst* s = (Inst*)p;
    const float fs = host->sample_rate > 0.0f ? host->sample_rate : 44117.647f;
    s->incW = WOW_HZ / fs;
    s->incF = FLUTTER_HZ / fs;
    s->phW = s->phF = 0.0f;
    reset(p);
    for (uint32_t i = 0; i < 4; i++) set_param(p, i, kParams[i].def);
    return 0;
}

static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2) return;
    float* const L = ch[0];
    float* const R = ch[1];
    uint32_t w = s->w;
    float phW = s->phW, phF = s->phF, lp0 = s->lp[0], lp1 = s->lp[1];
    const float drive = s->drive, makeup = s->makeup, mix = s->mix, dW = s->dW, dF = s->dF;
    for (uint32_t i = 0; i < frames; i++)
    {
        const float d = BASE + dW * sin_cyc(phW) + dF * sin_cyc(phF);
        phW += s->incW;
        if (phW >= 1.0f) phW -= 1.0f;
        phF += s->incF;
        if (phF >= 1.0f) phF -= 1.0f;
        float rp = (float)w - d;
        if (rp < 0.0f) rp += (float)LINE;
        const uint32_t i0 = (uint32_t)rp;
        const float fr = rp - (float)i0;

        // left: saturate, high cut, into the line (|lp| <= 1: clip ceiling x makeup <= 1), read back modulated
        const float xl = L[i];
        lp0 += LP_COEF * (soft_clip(drive * xl) * makeup - lp0);
        s->line[0][w] = (int16_t)(lp0 * 32767.0f);
        float y0 = (float)s->line[0][i0 & (LINE - 1)];
        float y1 = (float)s->line[0][(i0 + 1u) & (LINE - 1)];
        const float yl = (y0 + fr * (y1 - y0)) * (1.0f / 32767.0f);
        L[i] = xl + mix * (yl - xl);

        const float xr = R[i];
        lp1 += LP_COEF * (soft_clip(drive * xr) * makeup - lp1);
        s->line[1][w] = (int16_t)(lp1 * 32767.0f);
        y0 = (float)s->line[1][i0 & (LINE - 1)];
        y1 = (float)s->line[1][(i0 + 1u) & (LINE - 1)];
        const float yr = (y0 + fr * (y1 - y0)) * (1.0f / 32767.0f);
        R[i] = xr + mix * (yr - xr);

        w = (w + 1u) & (LINE - 1);
    }
    s->w = w;
    s->lp[0] = flush(lp0);
    s->lp[1] = flush(lp1);
    s->phW = phW;
    s->phF = phF;
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Tape", "OpenTracker (M. Spoczynski)", 4, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
