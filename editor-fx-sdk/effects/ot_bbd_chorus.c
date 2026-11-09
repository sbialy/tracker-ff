// PTFX_NAME: BBD Chorus
// PTFX_AUTHOR: OpenTracker
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0
//
// BBD Chorus: the classic two mode bucket brigade chorus. The input passes the
// BBD's low-pass filters (three one-pole stages at Tone, 6..12 kHz) and a gentle
// soft clip (standing in for the compander) into one short delay line (int16,
// like a bucket brigade), read by two taps moved by one triangle LFO in opposite
// directions, then mixed over the dry signal. The mode fixes rate and depth as
// the original's buttons do (I: 0.5 Hz, 2.0..5.0 ms. II: 0.8 Hz, 1.5..5.5 ms).
// No hiss: silence stays silence.
//
// Origin: OpenTracker 3.x, header audio/bbd_chorus.h of the core
// (https://github.com/sandlbn/polyend-tracker-re, branch v3_fixes, commit
// 789d55a4c5e8), Apache License 2.0, text in synth-open/LICENSE-OpenTracker-Apache-2.0.
// It is the DSP of the BBDC module of opentracker-fx (MIT, Copyright (c) 2026
// Marcin Spoczynski, https://github.com/opentracker-fw/opentracker-fx, commit 6e5a7da).
// The upstream file carries no copyright line of its own. See NOTICE.md.
//
// Modified (Apache-2.0 section 4b): translated from C++ to C as an offline Polyend
// Tracker .ptfx plugin. Mono only: the MK66 sample engine has one channel, so the
// two taps are summed (this is the original's Width 0 setting, and the Width
// control is gone). The On switch is gone, the LFO is evaluated per sample instead
// of linearly ramped between block ends, the dry / wet gains are fixed per render,
// and the Tone corner uses the host's powf and expf. The sample rate comes from the
// host (44100 Hz instead of 44117.6 Hz).
//
// Tail: the wet signal is a short delay behind the dry one, so cutting the render at the
// sample end loses the last few ms. Ring Out (the stock Stereo Delay's "Tail" list,
// "Ring Out|Cut Off") appends the longest tap delay plus the BBD filters' settling time
// (about 7 ms), Cut Off keeps the length.

#include "ot_common.h"

const PluginParam PLUGIN_PARAMS[4] = {
    PTFX_PARAM_LIST("Mode", 0, "Chorus I|Chorus II"),
    PTFX_PARAM_INT("Tone", 0.0f, 100.0f, 50.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Mix", 0.0f, 100.0f, 100.0f, 1.0f, "%"),
    PTFX_PARAM_LIST("Tail", 0, "Ring Out|Cut Off")
};

#define LINE 256   // frames (5.8 ms), int16 (512 B), power of two

static const PtfxHostApi* s_host;
static float s_fs = 44100.0f;

static int16_t  s_line[LINE];
static uint32_t s_w;
static float    s_ph;                 // LFO phase, cycles
static float    s_lp[3];              // the BBD filters' states
static float    s_g;                  // their one-pole coefficient
static float    s_hz, s_centre, s_depth;
static float    s_wet = 1.0f;
static uint8_t  s_cut;                // Tail: 0 = Ring Out, 1 = Cut Off

static inline float tri(float x) { return 4.0f * ot_absf(x - 0.5f) - 1.0f; }   // 1 at 0, -1 at 1/2

// Soft clip: unity slope at 0, flat at +-1.5 -> +-1.
static inline float soft_clip(float x) {
    x = 0.5f * (ot_absf(x + 1.5f) - ot_absf(x - 1.5f));
    return x - (4.0f / 27.0f) * x * x * x;
}

static inline int16_t to_q15(float x) {
    x = x > 0.99997f ? 0.99997f : (x < -1.0f ? -1.0f : x);
    return (int16_t)(x * 32768.0f);
}

// The tail: the longest tap delay (centre + depth) plus 64 frames for the three one-pole
// stages to settle (at Tone 0 they are down 60 dB after about 25 frames).
static void update_tail(void) {
    if (!s_host || !s_host->set_extra_samples) return;
    if (s_cut || s_wet <= 0.0f) {
        s_host->set_extra_samples(0, 0);
        return;
    }
    s_host->set_extra_samples(0, (uint32_t)(s_centre + s_depth) + 64u);
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    s_fs = host->sample_rate ? (float)host->sample_rate : 44100.0f;
    for (uint32_t i = 0; i < LINE; i++) s_line[i] = 0;
    s_w = 0;
    s_ph = 0.0f;
    s_lp[0] = s_lp[1] = s_lp[2] = 0.0f;
    OT_APPLY_DEFAULTS(4);
}

void plugin_set_param(uint8_t index, float value) {
    const float ms = 0.001f * s_fs;   // frames per millisecond
    switch (index) {
        case 0:
            if (value >= 0.5f) { s_hz = 0.8f; s_centre = 3.5f * ms; s_depth = 2.0f * ms; }   // Chorus II
            else               { s_hz = 0.5f; s_centre = 3.5f * ms; s_depth = 1.5f * ms; }   // Chorus I
            break;
        case 1: {
            const float t = ot_clampf(value, 0.0f, 100.0f);
            const float hz = 6000.0f * s_host->powf(2.0f, 0.01f * t);
            s_g = 1.0f - s_host->expf(-6.2831853f * hz / s_fs);
            break;
        }
        case 2: s_wet = ot_clampf(value * 0.01f, 0.0f, 1.0f); break;
        case 3: s_cut = value >= 0.5f; break;
        default: break;
    }
    update_tail();
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0) return 0;
    if (!in || !in[0]) {
        for (uint32_t i = 0; i < samples; i++) out[0][i] = 0;
        return (int32_t)samples;
    }
    const int16_t* src = in[0];
    int16_t* dst = out[0];
    // Dry ducks as the wet comes in (at wet 1 both 0.7). kG: 0.7 / 2 with the int16
    // line's 1 / 32768 folded in, over the sum of the two taps.
    const float dry = 1.0f - 0.3f * s_wet;
    const float mid = (0.35f / 32768.0f) * s_wet;
    const float g = s_g;
    const float inc = s_hz / s_fs;
    float a0 = s_lp[0], a1 = s_lp[1], a2 = s_lp[2], ph = s_ph;
    uint32_t w = s_w;
    for (uint32_t i = 0; i < samples; i++) {
        const float x = ot_s16f(src[i]);
        a0 += g * (x - a0);
        a1 += g * (a0 - a1);
        a2 += g * (a1 - a2);
        s_line[w] = to_q15(soft_clip(a2));
        const float t = tri(ph);
        ph += inc;
        if (ph >= 1.0f) ph -= 1.0f;
        const float dl = s_centre + s_depth * t, dr = s_centre - s_depth * t;
        // A tap d = k + f frames back: between w - k and w - k - 1.
        const uint32_t kl = (uint32_t)dl, kr = (uint32_t)dr;
        const float fl = dl - (float)kl, fr = dr - (float)kr;
        const float x0 = (float)s_line[(w - kl) & (LINE - 1)], x1 = (float)s_line[(w - kl - 1u) & (LINE - 1)];
        const float y0 = (float)s_line[(w - kr) & (LINE - 1)], y1 = (float)s_line[(w - kr - 1u) & (LINE - 1)];
        const float tl = x0 + fl * (x1 - x0), tr = y0 + fr * (y1 - y0);
        dst[i] = ot_f16s(dry * x + mid * (tl + tr));
        w = (w + 1u) & (LINE - 1);
    }
    s_w = w;
    s_ph = ph;
    s_lp[0] = ot_flush(a0);
    s_lp[1] = ot_flush(a1);
    s_lp[2] = ot_flush(a2);
    ot_pass_other_channels(in, out, samples, channels);
    return (int32_t)samples;
}
