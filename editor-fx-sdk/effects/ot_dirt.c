// PTFX_NAME: Dirt Drive
// PTFX_AUTHOR: OpenTracker
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0
//
// Dirt: a pre-gain (1 + 15 d^2, d = Drive / 100) into one of four shapers (Soft:
// rational tanh. Hard: clip at +-1. Fold: a triangle fold back into +-1. Asym: a
// biased tanh, even harmonics, with a 10 Hz DC block), a make-up gain
// 1 / (1 + 0.5 d), a quantiser to Bits (4..16, 16 = none), a sample-and-hold
// every Rate frames (1..32, 1 = none), a one-pole low-pass Tone
// (0..100 = 200 Hz .. 20 kHz, 100 = none) and a dry / wet Mix.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the DIRT module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx,
// modules/dirt/src/dirt.cpp, commit 6e5a7da), MIT licence, text in
// editor-fx-sdk/LICENSE-opentracker-fx-MIT.
//
// Modified: rewritten in C as an offline Polyend Tracker .ptfx plugin. Mono only,
// no On switch, no block-rate ramps of Drive and Mix and no crossfade on a Type
// change (the render is offline, parameters are constant while it runs). The ARM
// Q31 assembly clamp and quantiser are plain float code, the four shapers share one
// per-sample path (the original has a separate fast loop for Rate 1 that computes
// the same values) and the Tone corner uses the host's powf and tanf instead of
// the va_dsp.h approximations, so no va_dsp.h code is carried over. The sample rate
// comes from the host (44100 Hz instead of 44117.6 Hz).

#include "ot_common.h"

const PluginParam PLUGIN_PARAMS[6] = {
    PTFX_PARAM_LIST("Type", 0, "Soft|Hard|Fold|Asym"),
    PTFX_PARAM_INT("Drive", 0.0f, 100.0f, 30.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Bits", 4.0f, 16.0f, 16.0f, 1.0f, "bit"),
    PTFX_PARAM_INT("Rate", 1.0f, 32.0f, 1.0f, 1.0f, "fr"),
    PTFX_PARAM_INT("Tone", 0.0f, 100.0f, 100.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Mix", 0.0f, 100.0f, 100.0f, 1.0f, "%")
};

#define BIAS   0.3f   // Asym: tanh(x + b) - tanh(b)
// tanhR(BIAS): the rational tanh at the bias, so Asym is 0 at 0
#define BIAS_Y (BIAS * (27.0f + BIAS * BIAS) / (27.0f + 9.0f * BIAS * BIAS))

static const PtfxHostApi* s_host;
static float s_fs = 44100.0f;

// parameters
static int   s_type, s_bits = 16, s_rate = 1;
static int   s_tone_on;
static float s_pre = 1.0f, s_post = 1.0f, s_mix = 1.0f, s_g, s_dc_a, s_q = 32768.0f, s_iq;

// state
static float s_held, s_lp, s_dc;
static int   s_cnt;

static inline float clamp1(float x) { return ot_clampf(x, -1.0f, 1.0f); }

// Rational tanh, exact at 0 and +-3 (where it reaches +-1), clamped beyond.
static inline float tanh_c(float x) {
    float u = clamp1(x * (1.0f / 3.0f));
    const float u2 = u * u;
    return u * (3.0f + u2) / (1.0f + 3.0f * u2);
}

static inline float shape(int type, float x) {
    switch (type) {
        case 0: return tanh_c(x);
        case 1: return clamp1(x);
        case 2: {   // triangle fold: period 4, +-1 at x = +-1
            float t = (x + 1.0f) * 0.25f;
            const int k = (int)t;
            t -= (float)k - (t < (float)k ? 1.0f : 0.0f);
            return 1.0f - 4.0f * ot_absf(t - 0.5f);
        }
        default: return tanh_c(x + BIAS) - BIAS_Y;   // Asym
    }
}

// Mid-rise quantiser of y in [-1, 1] to 2^bits levels: floor to the level, then half a step.
static inline float quant(float y) {
    y = ot_clampf(y, -1.0f, 0.99999994f);
    const float v = y * s_q;
    int k = (int)v;
    if (v < (float)k) k--;
    return ((float)k + 0.5f) * s_iq;
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    s_fs = host->sample_rate ? (float)host->sample_rate : 44100.0f;
    s_held = s_lp = s_dc = 0.0f;
    s_cnt = 0;
    OT_APPLY_DEFAULTS(6);
}

void plugin_set_param(uint8_t index, float value) {
    const int iv = (int)(value + (value >= 0.0f ? 0.5f : -0.5f));
    switch (index) {
        case 0: s_type = iv < 0 ? 0 : (iv > 3 ? 3 : iv); break;
        case 1: {
            const float d = ot_clampf(value, 0.0f, 100.0f) * 0.01f;
            s_pre = 1.0f + 15.0f * d * d;
            s_post = 1.0f / (1.0f + 0.5f * d);
            break;
        }
        case 2:
            s_bits = iv < 4 ? 4 : (iv > 16 ? 16 : iv);
            s_q = (float)(1 << (s_bits - 1));
            s_iq = 1.0f / s_q;
            break;
        case 3: s_rate = iv < 1 ? 1 : (iv > 32 ? 32 : iv); break;
        case 4: {
            const int t = iv < 0 ? 0 : (iv > 100 ? 100 : iv);
            s_tone_on = t < 100;
            if (s_tone_on) {
                const float hz = 200.0f * s_host->powf(2.0f, 6.64385619f * (float)t * 0.01f);
                const float g = s_host->tanf(3.14159265f * hz / s_fs);
                s_g = g / (1.0f + g);
            }
            break;
        }
        case 5: s_mix = ot_clampf(value * 0.01f, 0.0f, 1.0f); break;
        default: break;
    }
    // the Asym DC block (10 Hz) updates once per hold, so its corner scales with the rate
    s_dc_a = 2.0f * 3.14159265f * 10.0f / s_fs * (float)s_rate;
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0) return 0;
    if (!in || !in[0]) {
        for (uint32_t i = 0; i < samples; i++) out[0][i] = 0;
        return (int32_t)samples;
    }
    const int16_t* src = in[0];
    int16_t* dst = out[0];
    const float wet = s_mix * s_post, dry = 1.0f - s_mix;
    const int asym = s_type == 3, quantise = s_bits < 16;
    float held = s_held, lp = s_lp, dc = s_dc;
    int cnt = s_cnt;
    if (cnt > s_rate) cnt = s_rate;
    for (uint32_t i = 0; i < samples; i++) {
        const float x = ot_s16f(src[i]);
        if (cnt == 0) {   // the shaper only at the hold points
            float y = shape(s_type, s_pre * x);
            if (asym) {
                dc += s_dc_a * (y - dc);
                y -= dc;
            }
            held = quantise ? quant(y) : y;
            cnt = s_rate;
        }
        --cnt;
        float y = held;
        if (s_tone_on) {   // TPT one-pole low-pass
            const float v = (y - lp) * s_g;
            y = v + lp;
            lp = y + v;
        } else {           // bypassed: the state follows (no step when it comes in)
            lp = y;
        }
        dst[i] = ot_f16s(y * wet + x * dry);
    }
    s_held = held;
    s_lp = ot_flush(lp);
    s_dc = ot_flush(dc);
    s_cnt = cnt;
    ot_pass_other_channels(in, out, samples, channels);
    return (int32_t)samples;
}
