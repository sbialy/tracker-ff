// PTFX_NAME: Dub Echo
// PTFX_AUTHOR: OpenTracker
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 132300
//
// Dub Echo: a mono echo whose feedback loop is filtered and clipped, so every
// repeat is darker and thinner than the one before. Into the delay line go the
// input and the feedback (<= 0.98) through the loop's Tone (a one-pole low-pass,
// 500 Hz..9 kHz, 100 = open), the Low Cut (two one-pole high-passes, 20..500 Hz)
// and a soft clip. Duck turns the repeats down while the input is loud (5 ms
// attack, 150 ms release follower). Mix is the wet level added to the dry signal,
// the dry signal is untouched. The tail rings out after the sample (set_extra_samples)
// unless the Tail list (the stock Stereo Delay's "Ring Out|Cut Off") is on Cut Off.
//
// Copyright (c) 2026 Marcin Spoczynski
// Port of the ECHO module of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx,
// modules/echo/src/echo.cpp, commit 6e5a7da), MIT licence, text in
// editor-fx-sdk/LICENSE-opentracker-fx-MIT.
//
// Modified: rewritten in C as an offline Polyend Tracker .ptfx plugin. The time is
// in milliseconds, there is no tempo sync (a plugin has no BPM). Mono, so no
// ping-pong. The original keeps a half-rate line (2:1 decimated, linear upsampled)
// to fit its SDRAM budget. This port runs the line and the loop filters at the full
// sample rate (int16 line, 132 KB of scratch), so the repeats are brighter than in
// OpenTracker at the same Tone. The duck follower runs per sample, there are no
// block-rate ramps and no On / Off handling, no time-change crossfade (the render is
// offline, parameters are constant while it runs), and the filter corners use the
// host's powf and tanf instead of the va_dsp.h approximations, so no va_dsp.h code
// is carried over. The sample rate comes from the host (44100 Hz instead of 44117.6 Hz).

#include "ot_common.h"

const PluginParam PLUGIN_PARAMS[7] = {
    PTFX_PARAM_INT("Time", 20.0f, 1500.0f, 350.0f, 10.0f, "ms"),
    PTFX_PARAM_INT("Fdbk", 0.0f, 100.0f, 45.0f, 1.0f, "%"),
    PTFX_PARAM_INT("LowCut", 0.0f, 100.0f, 40.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Tone", 0.0f, 100.0f, 50.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Duck", 0.0f, 100.0f, 50.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Mix", 0.0f, 100.0f, 35.0f, 1.0f, "%"),
    PTFX_PARAM_LIST("Tail", 0, "Ring Out|Cut Off")
};

#define LINE_MAX    66150          // 1.5 s at 44.1 kHz (int16: 132,300 B of scratch)
#define MAX_TAIL    220500         // 5.0 s
#define TO_LINE     (32767.0f * 0.5f)   // the line holds y / 2 (soft clip ceiling 2)
#define FROM_LINE   (2.0f / 32767.0f)

static const PtfxHostApi* s_host;
static float s_fs = 44100.0f;
static int16_t* s_line;

static uint32_t s_t = 15435;      // delay, samples
static uint32_t s_w;
static float s_fb, s_mix, s_duck;
static uint8_t s_cut;             // Tail: 0 = Ring Out, 1 = Cut Off
static float s_g_lp, s_g_hp;
static int   s_lp_on;
static float s_a_att, s_a_rel;
static float s_lps, s_hp1, s_hp2, s_env;

// Soft clip with a ceiling of 2: a cubic, slope 1 at 0, flat from |x| = 3.
static inline float clip2(float x) {
    const float v = ot_clampf(x * (1.0f / 3.0f), -1.0f, 1.0f);
    return v * (3.0f - v * v);
}

// TPT one-pole: returns the low-pass, moves the state.
static inline float lp1(float* s, float x, float g) {
    const float v = (x - *s) * g;
    const float y = v + *s;
    *s = y + v;
    return y;
}

static float one_pole_g(float hz) {
    hz = ot_clampf(hz, 5.0f, 0.45f * s_fs);
    const float g = s_host->tanf(3.14159265f * hz / s_fs);
    return g / (1.0f + g);
}

// The tail: repeats until they are 60 dB down, at most 5 s.
static void update_tail(void) {
    if (!s_host || !s_host->set_extra_samples) return;
    if (s_cut || s_mix <= 0.0f) {
        s_host->set_extra_samples(0, 0);
        return;
    }
    uint32_t repeats = 1;
    if (s_fb > 0.0f) {
        const float k = s_host->logf(0.001f) / s_host->logf(s_fb);
        repeats = (uint32_t)k + 2u;
    }
    uint32_t tail = repeats * s_t;
    if (tail > MAX_TAIL) tail = MAX_TAIL;
    s_host->set_extra_samples(0, tail);
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    s_host = host;
    s_fs = host->sample_rate ? (float)host->sample_rate : 44100.0f;
    s_line = 0;
    if (scratch_mem && scratch_size >= LINE_MAX * sizeof(int16_t)) {
        s_line = (int16_t*)scratch_mem;
        for (uint32_t i = 0; i < LINE_MAX; i++) s_line[i] = 0;
    }
    s_w = 0;
    s_lps = s_hp1 = s_hp2 = s_env = 0.0f;
    s_a_att = 1.0f - host->expf(-1.0f / (0.005f * s_fs));
    s_a_rel = host->expf(-1.0f / (0.150f * s_fs));
    OT_APPLY_DEFAULTS(7);
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: {
            float t = value * 0.001f * s_fs + 0.5f;
            t = ot_clampf(t, 1.0f, (float)(LINE_MAX - 1));
            s_t = (uint32_t)t;
            break;
        }
        case 1: s_fb = 0.98f * ot_clampf(value, 0.0f, 100.0f) * 0.01f; break;
        case 2: s_g_hp = one_pole_g(20.0f * s_host->powf(25.0f, ot_clampf(value, 0.0f, 100.0f) * 0.01f)); break;   // 20 x 25^(v/100)
        case 3: {
            const float v = ot_clampf(value, 0.0f, 100.0f);
            s_lp_on = v < 99.5f;
            if (s_lp_on) s_g_lp = one_pole_g(500.0f * s_host->powf(18.0f, v * 0.01f));   // 500 x 18^(v/100)
            break;
        }
        case 4: s_duck = ot_clampf(value, 0.0f, 100.0f) * 0.01f; break;
        case 5: s_mix = ot_clampf(value, 0.0f, 100.0f) * 0.01f; break;
        case 6: s_cut = value >= 0.5f; break;
        default: return;
    }
    update_tail();
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0) return 0;
    const int16_t* src = in ? in[0] : 0;
    int16_t* dst = out[0];
    if (!s_line) {   // no scratch: pass through
        for (uint32_t i = 0; i < samples; i++) dst[i] = src ? src[i] : 0;
        return (int32_t)samples;
    }
    uint32_t w = s_w;
    float lps = s_lps, hp1 = s_hp1, hp2 = s_hp2, env = s_env;
    for (uint32_t i = 0; i < samples; i++) {
        const float x = src ? ot_s16f(src[i]) : 0.0f;
        // Duck follower on the input level.
        const float a = ot_absf(x);
        env = a > env ? env + s_a_att * (a - env) : env * s_a_rel;
        const float gate = 1.0f - s_duck * ot_clampf(2.0f * env, 0.0f, 1.0f);

        uint32_t r = w + LINE_MAX - s_t;
        if (r >= LINE_MAX) r -= LINE_MAX;
        const float tap = (float)s_line[r] * FROM_LINE;

        // Into the line: the input and the feedback through tone, low cut and soft clip.
        float y = x + s_fb * tap;
        if (s_lp_on) y = lp1(&lps, y, s_g_lp);
        else lps = y;   // bypassed: the state follows (no step when it comes in)
        y -= lp1(&hp1, y, s_g_hp);
        y -= lp1(&hp2, y, s_g_hp);
        s_line[w] = (int16_t)(clip2(y) * TO_LINE);   // truncates towards 0: the repeats die out

        dst[i] = ot_f16s(x + s_mix * gate * tap);
        if (++w >= LINE_MAX) w = 0;
    }
    s_w = w;
    s_lps = ot_flush(lps);
    s_hp1 = ot_flush(hp1);
    s_hp2 = ot_flush(hp2);
    s_env = ot_flush(env);
    ot_pass_other_channels(in, out, samples, channels);
    return (int32_t)samples;
}
