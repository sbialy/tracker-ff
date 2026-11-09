// PTFX_NAME: Lo-Fi Chip
// PTFX_AUTHOR: OpenTracker
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0
//
// Lo-Fi: the sound of a small chip's output stage. A DAC of Bits bits (4..16,
// 16 = none) with an optional Ladder character (uneven top steps: the code's
// top six bits carry weight errors), a sample-and-hold at Rate (32 kHz .. 4 kHz,
// no interpolation: the aliasing is the point), a Speaker shape (TV: high-pass
// 80 Hz, low-pass 6 kHz, +2 dB at 3 kHz. Handheld: 300 Hz, 4.5 kHz, +4 dB at
// 1.5 kHz. Monitor: 40 Hz, 12 kHz) and a dry / wet Mix. The TV and Handheld
// shapes are not level neutral: a full scale square peaks near +-2.2 before the
// output clamp.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the LOFI module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx,
// modules/lofi/src/lofi.cpp, commit 6e5a7da), MIT licence, text in
// editor-fx-sdk/LICENSE-opentracker-fx-MIT.
// The ladder error weights are the table kLadderErr of OpenTracker's chip_dsp.h
// (Apache-2.0, https://github.com/sandlbn/polyend-tracker-re, branch v3_fixes,
// commit 789d55a4c5e8), see NOTICE.md. The speaker filters follow lofi.cpp.
//
// Modified: rewritten in C as an offline Polyend Tracker .ptfx plugin. Mono only
// (the original has a Mono switch and runs two channels), no On switch, no
// block-rate crossfade on a Rate / Speaker change and no wet ramp (the render is
// offline, parameters are constant while it runs). The DAC is plain float code
// instead of ARM Q31 assembly and the filter coefficients use the host's tanf and
// powf instead of the va_dsp.h approximations, so no va_dsp.h or chip_dsp.h code
// is carried over except the ladder weights above. The sample rate comes from the
// host (44100 Hz instead of 44117.6 Hz).

#include "ot_common.h"

const PluginParam PLUGIN_PARAMS[5] = {
    PTFX_PARAM_INT("Bits", 4.0f, 16.0f, 8.0f, 1.0f, "bit"),
    PTFX_PARAM_LIST("Rate", 4, "Off|32 kHz|22 kHz|16 kHz|11 kHz|8 kHz|6 kHz|4 kHz"),
    PTFX_PARAM_INT("Ladder", 0.0f, 100.0f, 0.0f, 1.0f, "%"),
    PTFX_PARAM_LIST("Speaker", 1, "Off|TV|Handheld|Monitor"),
    PTFX_PARAM_INT("Mix", 0.0f, 100.0f, 100.0f, 1.0f, "%")
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

static const PtfxHostApi* s_host;
static float s_fs = 44100.0f;
static float s_err[64];       // ladder error per top-six-bit code

// raw parameters
static int   s_bits = 8, s_rate = 4, s_ladder = 0, s_speaker = 1;
static float s_mix = 1.0f;

// derived
static int   s_quant, s_hold, s_use_ladder, s_use_speaker, s_use_bump;
static float s_q = 128.0f, s_iq = 1.0f / 128.0f, s_amount;
static float s_period = 1.0f;
static float s_ghp, s_glp;
static float s_bp_g, s_bp_k, s_bp_h, s_kg, s_bump_gain;

// state
static float s_hp, s_lp, s_s1, s_s2, s_held, s_phase;
static int   s_rate_new;      // a new Rate takes its first value at once

static float one_pole_g(float hz) {
    const float g = s_host->tanf(3.14159265f * hz / s_fs);
    return g / (1.0f + g);
}

static void recompute(void) {
    s_quant = s_bits < 16;                       // a ladder at 16 bits would be -90 dB: not run
    s_use_ladder = s_quant && s_ladder > 0;
    s_q = (float)(1 << (s_bits < 16 ? s_bits - 1 : 15));
    s_iq = 1.0f / s_q;
    s_amount = (float)s_ladder * 0.01f;
    s_hold = s_rate != 0;
    s_period = 1.0f;
    if (s_hold) {
        s_period = s_fs / k_rate_hz[s_rate];
        if (s_period < 1.0f) s_period = 1.0f;
    }
    if (s_rate_new) {
        s_phase = s_period;
        s_rate_new = 0;
    }
    s_use_speaker = s_speaker != 0;
    s_use_bump = 0;
    if (s_use_speaker) {
        const float* sh = k_shape[s_speaker];
        s_ghp = one_pole_g(sh[0]);
        s_glp = one_pole_g(sh[1]);
        s_use_bump = sh[2] > 0.0f;
        if (s_use_bump) {
            const float kk = 1.0f;               // the bump's damping (Q 1)
            const float g = s_host->tanf(3.14159265f * sh[2] / s_fs);
            s_bp_g = g;
            s_bp_k = kk;
            s_bp_h = 1.0f / (1.0f + g * (g + kk));
            s_kg = g + kk;
            // the bump's peak is scaled so the whole shape reaches its dB there
            // (the one-poles' own loss at the bump made up: an analog estimate)
            const float f = sh[2], a = f / sh[1], b = sh[0] / f;
            const float loss = s_host->sqrtf((1.0f + a * a) * (1.0f + b * b));
            s_bump_gain = (s_host->powf(10.0f, sh[3] / 20.0f) * loss - 1.0f) * kk;
        } else {
            s_s1 = s_s2 = 0.0f;                  // a band-pass that comes in later starts clean
        }
    }
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    s_fs = host->sample_rate ? (float)host->sample_rate : 44100.0f;
    for (int i = 0; i < 64; i++) {
        float e = 0.0f;
        for (int k = 0; k < 6; k++)
            if (i & (1 << k)) e += k_ladder_err[k];
        s_err[i] = e;
    }
    s_hp = s_lp = s_s1 = s_s2 = s_held = 0.0f;
    s_phase = 0.0f;
    s_rate_new = 0;
    OT_APPLY_DEFAULTS(5);
}

void plugin_set_param(uint8_t index, float value) {
    const int iv = (int)(value + (value >= 0.0f ? 0.5f : -0.5f));
    switch (index) {
        case 0: s_bits = iv < 4 ? 4 : (iv > 16 ? 16 : iv); break;
        case 1: s_rate = iv < 0 ? 0 : (iv > 7 ? 7 : iv); s_rate_new = 1; break;
        case 2: s_ladder = iv < 0 ? 0 : (iv > 100 ? 100 : iv); break;
        case 3: s_speaker = iv < 0 ? 0 : (iv > 3 ? 3 : iv); break;
        case 4: s_mix = ot_clampf(value * 0.01f, 0.0f, 1.0f); break;
        default: return;
    }
    if (s_host) recompute();
}

// The DAC on one sample: rounded to 2^bits levels (mid-tread, clamped, an exact .5
// rounds away from zero), the ladder's weight errors on the code's top six bits.
static inline float dac(float x) {
    const float q = s_q;
    float y = x * q;
    y = y < -q ? -q : (y > q - 1.0f ? q - 1.0f : y);
    const int c = (int)(y + (y >= 0.0f ? 0.5f : -0.5f));
    float out = (float)c * s_iq;
    if (s_use_ladder) {
        const uint32_t code = (uint32_t)(c + (1 << (s_bits - 1)));   // offset binary 0 .. 2^bits - 1
        const uint32_t top = s_bits >= 6 ? (code >> (s_bits - 6)) & 63u : (code << (6 - s_bits)) & 63u;
        out += s_amount * s_err[top] * s_iq;
    }
    return out;
}

// The speaker on one sample: high-pass, the bump (an SVF band-pass), low-pass.
static inline float speak(float x) {
    float v = (x - s_hp) * s_ghp;
    const float lo = v + s_hp;
    s_hp = lo + v;
    float y = x - lo;
    if (s_use_bump) {
        const float hp = (y - s_kg * s_s1 - s_s2) * s_bp_h;
        const float v1 = s_bp_g * hp;
        const float bp = v1 + s_s1;
        s_s1 = bp + v1;
        const float v2 = s_bp_g * bp;
        s_s2 = (v2 + s_s2) + v2;
        y += s_bump_gain * bp;
    }
    v = (y - s_lp) * s_glp;
    const float out = v + s_lp;
    s_lp = out + v;
    return out;
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0) return 0;
    if (!in || !in[0]) {
        for (uint32_t i = 0; i < samples; i++) out[0][i] = 0;
        return (int32_t)samples;
    }
    const int16_t* src = in[0];
    int16_t* dst = out[0];
    const float wet = s_mix, dry = 1.0f - s_mix;
    float ph = s_phase;
    for (uint32_t i = 0; i < samples; i++) {
        const float d = ot_s16f(src[i]);
        float x = d;
        if (s_hold) {
            // The hold of the DAC's output: the DAC only where the hold takes a value.
            ph += 1.0f;
            if (ph >= s_period) {
                ph -= s_period;
                s_held = s_quant ? dac(x) : x;
            }
            x = s_held;
        } else if (s_quant) {
            x = dac(x);
        }
        if (s_use_speaker) x = speak(x);
        dst[i] = ot_f16s(x * wet + d * dry);
    }
    s_phase = ph;
    s_hp = ot_flush(s_hp);
    s_lp = ot_flush(s_lp);
    s_s1 = ot_flush(s_s1);
    s_s2 = ot_flush(s_s2);
    ot_pass_other_channels(in, out, samples, channels);
    return (int32_t)samples;
}
