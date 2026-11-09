// PTFX_NAME: Tape Saturation
// PTFX_AUTHOR: OpenTracker
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0
//
// Tape: drive into a soft clip (tape saturation), a gentle high cut (head loss),
// wow (0.6 Hz) and flutter (7 Hz) as a modulated delay of up to ~2.3 ms, then a
// dry / wet mix.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the TAPE module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx,
// modules/tape/src/tape.cpp, commit 6e5a7da), MIT licence, text in
// editor-fx-sdk/LICENSE-opentracker-fx-MIT.
//
// Modified: rewritten in C as an offline Polyend Tracker .ptfx plugin. Mono only
// (the original runs one state per channel), the On switch is gone, the Off fade
// and the block-rate ramps of wow, flutter and mix are replaced by a per-sample
// phase accumulator (so there is no 128-frame block assumption left), and the
// sample rate comes from the host (44100 Hz instead of OpenTracker's 44117.6 Hz).

#include "ot_common.h"

const PluginParam PLUGIN_PARAMS[4] = {
    PTFX_PARAM_INT("Drive",   0.0f, 100.0f, 30.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Wow",     0.0f, 100.0f, 20.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Flutter", 0.0f, 100.0f, 10.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Mix",     0.0f, 100.0f, 100.0f, 1.0f, "%")
};

#define LINE 256              // delay line, frames (int16: 512 B), power of two
#define WOW_HZ     0.6f
#define FLUTTER_HZ 7.0f
#define WOW_MAX    88.0f      // frames (~2 ms) at 100 %
#define FLUTTER_MAX 13.0f     // frames (~0.3 ms) at 100 %
#define BASE       120.0f     // centre delay (frames): room for both depths
#define LP_COEF    0.55f      // one-pole high cut (~5.6 kHz)

static int16_t  s_line[LINE];
static uint32_t s_w;
static float    s_lp;
static float    s_phW, s_phF;
static float    s_incW, s_incF;
static float    s_drive = 1.0f, s_makeup = 1.0f;
static float    s_dW, s_dF, s_mix;

// sin(2 pi x), x in [0, 1): parabola with one refinement (error < 0.1 %).
static inline float sin_cyc(float x) {
    const float t = 2.0f * x - 1.0f;
    const float y = 4.0f * t * (1.0f - ot_absf(t));
    return -(y + 0.225f * (y * ot_absf(y) - y));
}

// Soft clip: unity slope at 0, flat at +-1.5 -> +-1.
static inline float soft_clip(float x) {
    x = 0.5f * (ot_absf(x + 1.5f) - ot_absf(x - 1.5f));
    return x - (4.0f / 27.0f) * x * x * x;
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    const float fs = host && host->sample_rate ? (float)host->sample_rate : 44100.0f;
    s_incW = WOW_HZ / fs;
    s_incF = FLUTTER_HZ / fs;
    for (uint32_t i = 0; i < LINE; i++) s_line[i] = 0;
    s_w = 0;
    s_lp = 0.0f;
    s_phW = s_phF = 0.0f;
    OT_APPLY_DEFAULTS(4);
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0:
            s_drive  = 1.0f + 0.05f * value;            // x1 .. x6
            s_makeup = 1.0f / (1.0f + 0.012f * value);
            break;
        case 1: s_dW = WOW_MAX * 0.01f * value; break;
        case 2: s_dF = FLUTTER_MAX * 0.01f * value; break;
        case 3: s_mix = ot_clampf(value * 0.01f, 0.0f, 1.0f); break;
        default: break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0) return 0;
    if (!in || !in[0]) {
        for (uint32_t i = 0; i < samples; i++) out[0][i] = 0;
        return (int32_t)samples;
    }
    const int16_t* src = in[0];
    int16_t* dst = out[0];
    uint32_t w = s_w;
    float lp = s_lp, phW = s_phW, phF = s_phF;
    for (uint32_t i = 0; i < samples; i++) {
        const float x = ot_s16f(src[i]);
        const float d = BASE + s_dW * sin_cyc(phW) + s_dF * sin_cyc(phF);
        phW += s_incW;
        if (phW >= 1.0f) phW -= 1.0f;
        phF += s_incF;
        if (phF >= 1.0f) phF -= 1.0f;

        // Saturate, high cut, then into the line. |lp| <= 1 (clip ceiling x makeup <= 1).
        lp += LP_COEF * (soft_clip(s_drive * x) * s_makeup - lp);
        s_line[w] = (int16_t)(lp * 32767.0f);

        float rp = (float)w - d;
        if (rp < 0.0f) rp += (float)LINE;
        const uint32_t i0 = (uint32_t)rp;
        const float fr = rp - (float)i0;
        const float y0 = (float)s_line[i0 & (LINE - 1)];
        const float y1 = (float)s_line[(i0 + 1u) & (LINE - 1)];
        const float y = (y0 + fr * (y1 - y0)) * (1.0f / 32767.0f);

        dst[i] = ot_f16s(x + s_mix * (y - x));
        w = (w + 1u) & (LINE - 1);
    }
    s_w = w;
    s_lp = ot_flush(lp);
    s_phW = phW;
    s_phF = phF;
    ot_pass_other_channels(in, out, samples, channels);
    return (int32_t)samples;
}
