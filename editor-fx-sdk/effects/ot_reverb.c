// PTFX_NAME: Reverb
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 65536
//
// Reverb: the Griesinger topology described in the Dattorro paper (an input
// predelay and low-pass, 4 allpass diffusers, then a loop of two halves with
// 2 allpasses and 1 long delay each, the long delay of one half modulated for a
// slow shimmer). This is the reverb of the Tracker firmware (core/libs/Audio/Polyverb),
// rewritten in C so it can run as a sample editor plugin.
//
// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Adapted for Polyverb by Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)
//
// -----------------------------------------------------------------------------
//
// Modified: ported from the C++ templates of core/libs/Audio/Polyverb
// (reverb.h, fx_engine.h, cosine_oscillator.h, util.h) to plain C as an offline
// Polyend Tracker .ptfx plugin. The delay line layout, lengths, gains and the
// order of operations are unchanged. The mono sample feeds both inputs of the
// stereo reverb and the two outputs are averaged. The firmware's activity
// detector is gone, the cosine oscillator uses only its approximate mode (no
// libm), and the parameters are mapped as the firmware does (Size = reverb time,
// Damp = 1 - low-pass coefficient, stopping at 0.95, Predly = fraction of a 5000 sample line, Diffus = diffusion,
// 0 .. 0.85) except that Size stops at 0.95 so the tail always dies down inside the 5 s
// the plugin adds and Damp at 0.95 because a low-pass coefficient of 0 mutes the loop.
// The input gain is 0.2 instead of the firmware's 0.1, which is a send level: as an
// insert at Mix 100 the wet signal is then about 2 dB under the dry one instead of
// 8 dB. The tail rings out after the sample (set_extra_samples)
// unless the Tail list (the stock Stereo Delay's "Ring Out|Cut Off") is on Cut Off.

#include "ot_common.h"

const PluginParam PLUGIN_PARAMS[6] = {
    PTFX_PARAM_INT("Size",   0.0f, 100.0f, 70.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Damp",   0.0f, 100.0f, 50.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Predly", 0.0f, 100.0f,  0.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Diffus", 0.0f,  85.0f, 62.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Mix",    0.0f, 100.0f, 35.0f, 1.0f, "%"),
    PTFX_PARAM_LIST("Tail", 0, "Ring Out|Cut Off")
};

#define SIZE 16384
#define MASK (SIZE - 1)
#define MAX_TAIL 220500   // 5.0 s
#define PREDELAY_LEN 5000

// Delay lines of the first ring (E: ap1 .. ap4, dap1a, dap1b, del1, del0). base + length
// is the last slot of a line (the engine reserves length + 1 slots per line).
#define E_AP1_BASE    0
#define E_AP1_LEN     150
#define E_AP2_BASE    151
#define E_AP2_LEN     214
#define E_AP3_BASE    366
#define E_AP3_LEN     319
#define E_AP4_BASE    686
#define E_AP4_LEN     527
#define E_DAP1A_BASE  1214
#define E_DAP1A_LEN   2182
#define E_DAP1B_BASE  3397
#define E_DAP1B_LEN   2690
#define E_DEL1_BASE   6088
#define E_DEL1_LEN    4501
#define E_DEL0_BASE   10590
#define E_DEL0_LEN    PREDELAY_LEN
// Second ring (E2: dap2a, dap2b, del2)
#define F_DAP2A_BASE  0
#define F_DAP2A_LEN   2525
#define F_DAP2B_BASE  2526
#define F_DAP2B_LEN   2197
#define F_DEL2_BASE   4724
#define F_DEL2_LEN    6312

typedef char ot_static_assert_e[(E_DEL0_BASE + E_DEL0_LEN) < SIZE ? 1 : -1];

static int16_t* s_buf;    // first ring, SIZE slots
static int16_t* s_buf2;   // second ring, SIZE slots
static int32_t  s_wp, s_wp2;

// accumulator and the last read value of the engine context
static float s_acc, s_prev;

static float s_lp0, s_lp1, s_lp2;
static float s_kap = 0.62f, s_klp = 0.5f, s_krt = 0.7f, s_amount = 0.35f, s_input_gain = 0.2f;
static int   s_predelay;

// Cosine oscillator (approximate mode) of the slow modulation, y = 0..1.
static float s_lfo_iir, s_lfo_y0, s_lfo_y1, s_lfo_value;

static int g_tail_ready;
static uint8_t s_cut;   // Tail: 0 = Ring Out, 1 = Cut Off
static const PtfxHostApi* s_host;

static void lfo_init(float frequency) {
    float sign = 16.0f;
    frequency -= 0.25f;
    if (frequency < 0.0f) {
        frequency = -frequency;
    } else if (frequency > 0.5f) {
        frequency -= 0.5f;
    } else {
        sign = -16.0f;
    }
    s_lfo_iir = sign * frequency * (1.0f - 2.0f * frequency);
    s_lfo_y1 = s_lfo_iir * 0.25f;
    s_lfo_y0 = 0.5f;
}

static inline float lfo_next(void) {
    const float temp = s_lfo_y0;
    s_lfo_y0 = s_lfo_iir * s_lfo_y0 - s_lfo_y1;
    s_lfo_y1 = temp;
    return temp + 0.5f;
}

static inline int16_t compress(float v) {
    v *= 32768.0f;
    v = v > -32768.0f ? v : -32768.0f;   // also maps NaN to a finite value
    v = v < 32767.0f ? v : 32767.0f;
    return (int16_t)(int32_t)v;
}
static inline float decompress(int16_t v) { return (float)v * (1.0f / 32768.0f); }

// Engine operations on ring A (buf, wp) and ring B (buf2, wp2). offset -1 is the tail of a line.
static inline void read_a(int base, int len, int offset, float scale) {
    const int pos = (s_wp + base + (offset == -1 ? len - 1 : offset)) & MASK;
    const float r = decompress(s_buf[pos]);
    s_prev = r;
    s_acc += r * scale;
}
static inline void read_b(int base, int len, int offset, float scale) {
    const int pos = (s_wp2 + base + (offset == -1 ? len - 1 : offset)) & MASK;
    const float r = decompress(s_buf2[pos]);
    s_prev = r;
    s_acc += r * scale;
}
static inline void write_a(int base, int offset, float scale) {
    s_buf[(s_wp + base + offset) & MASK] = compress(s_acc);
    s_acc *= scale;
}
static inline void write_b(int base, int offset, float scale) {
    s_buf2[(s_wp2 + base + offset) & MASK] = compress(s_acc);
    s_acc *= scale;
}
static inline void write_ap_a(int base, float scale) { write_a(base, 0, scale); s_acc += s_prev; }
static inline void write_ap_b(int base, float scale) { write_b(base, 0, scale); s_acc += s_prev; }

static inline void lp(float* state, float coefficient) {
    *state += coefficient * (s_acc - *state);
    s_acc = *state;
}

// del2 read with a modulated offset (ring B), linearly interpolated
static inline void interpolate_b(int base, float offset, float amplitude, float lfo, float scale) {
    offset += amplitude * lfo;
    const int32_t integral = (int32_t)offset;
    const float fractional = offset - (float)integral;
    const float a = decompress(s_buf2[(s_wp2 + integral + base) & MASK]);
    const float b = decompress(s_buf2[(s_wp2 + integral + base + 1) & MASK]);
    const float x = a + (b - a) * fractional;
    s_prev = x;
    s_acc += x * scale;
}

// The tail: the loop passes its two long delays twice per round, so the level after
// n rounds is about Size^(2n). A round is about 20300 samples. Down to -60 dB, at most 5 s.
static void update_tail(void) {
    if (!s_host || !s_host->set_extra_samples || !g_tail_ready) return;
    if (s_cut || s_amount <= 0.0f) {
        s_host->set_extra_samples(0, 0);
        return;
    }
    uint32_t tail = 20300u;
    if (s_krt > 0.0f) {
        const float rounds = s_host->logf(0.001f) / (2.0f * s_host->logf(s_krt));
        const float t = rounds * 20300.0f;
        tail = t > (float)MAX_TAIL ? (uint32_t)MAX_TAIL : (uint32_t)t + 20300u;
    }
    if (tail > MAX_TAIL) tail = MAX_TAIL;
    s_host->set_extra_samples(0, tail);
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    s_host = host;
    s_buf = 0;
    s_buf2 = 0;
    g_tail_ready = 0;
    if (scratch_mem && scratch_size >= 2u * SIZE * sizeof(int16_t)) {
        s_buf = (int16_t*)scratch_mem;
        s_buf2 = s_buf + SIZE;
        for (uint32_t i = 0; i < 2u * SIZE; i++) s_buf[i] = 0;
    }
    s_wp = s_wp2 = 0;
    s_acc = s_prev = 0.0f;
    s_lp0 = s_lp1 = s_lp2 = 0.0f;
    s_input_gain = 0.2f;
    // The firmware: SetLFOFrequency(LFO_2, 0.3 / 32000), the oscillator is initialised at 32 x that.
    lfo_init((0.3f / 32000.0f) * 32.0f);
    s_lfo_value = s_lfo_y1 + 0.5f;
    OT_APPLY_DEFAULTS(6);
    g_tail_ready = 1;
    update_tail();
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: s_krt = ot_clampf(value, 0.0f, 100.0f) * 0.0095f; break;
        case 1: s_klp = 1.0f - ot_clampf(value, 0.0f, 100.0f) * 0.0095f; break;   // never 0: the low-pass would mute the loop
        case 2: s_predelay = (int)(ot_clampf(value, 0.0f, 100.0f) * 0.01f * (float)PREDELAY_LEN); break;
        case 3: s_kap = ot_clampf(value, 0.0f, 85.0f) * 0.01f; break;
        case 4: s_amount = ot_clampf(value, 0.0f, 100.0f) * 0.01f; break;
        case 5: s_cut = value >= 0.5f; break;   // Tail: 0 = Ring Out, 1 = Cut Off
        default: return;
    }
    update_tail();
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!out || !out[0] || samples == 0) return 0;
    const int16_t* src = in ? in[0] : 0;
    int16_t* dst = out[0];
    if (!s_buf) {   // no scratch: pass through
        for (uint32_t i = 0; i < samples; i++) dst[i] = src ? src[i] : 0;
        return (int32_t)samples;
    }
    const float kap = s_kap, klp = s_klp, krt = s_krt, amount = s_amount, gain = s_input_gain;
    float lp_0 = s_lp0, lp_1 = s_lp1, lp_2 = s_lp2;
    for (uint32_t i = 0; i < samples; i++) {
        const float dry = src ? (float)src[i] : 0.0f;

        // engine Start(): both write pointers step back, the LFO ticks at (write_ptr & 30) == 0
        if (--s_wp < 0) s_wp += SIZE;
        if (--s_wp2 < 0) s_wp2 += SIZE;
        s_acc = 0.0f;
        s_prev = 0.0f;
        if ((s_wp & 30) == 0) s_lfo_value = lfo_next();
        else s_lfo_value = s_lfo_y1 + 0.5f;

        // the mono sample feeds both inputs: gain * (L + R) / 32768
        s_acc = gain * (dry + dry) * (1.0f / 32768.0f);

        write_a(E_DEL0_BASE, 0, 0.0f);
        read_a(E_DEL0_BASE, E_DEL0_LEN, s_predelay, 1.0f);
        lp(&lp_0, klp);

        // Diffuse through 4 allpasses.
        read_a(E_AP1_BASE, E_AP1_LEN, -1, kap);
        write_ap_a(E_AP1_BASE, -kap);
        read_a(E_AP2_BASE, E_AP2_LEN, -1, kap);
        write_ap_a(E_AP2_BASE, -kap);
        read_a(E_AP3_BASE, E_AP3_LEN, -1, kap);
        write_ap_a(E_AP3_BASE, -kap);
        read_a(E_AP4_BASE, E_AP4_LEN, -1, kap);
        write_ap_a(E_AP4_BASE, -kap);
        const float apout = s_acc;

        // Main reverb loop, first half.
        s_acc = apout;
        interpolate_b(F_DEL2_BASE, 6211.0f, 100.0f, s_lfo_value, krt);
        lp(&lp_1, klp);
        read_a(E_DAP1A_BASE, E_DAP1A_LEN, -1, -kap);
        write_ap_a(E_DAP1A_BASE, kap);
        read_a(E_DAP1B_BASE, E_DAP1B_LEN, -1, kap);
        write_ap_a(E_DAP1B_BASE, -kap);
        write_a(E_DEL1_BASE, 0, 2.0f);
        const float wet_l = s_acc;

        // Second half.
        s_acc = apout;
        read_a(E_DEL1_BASE, E_DEL1_LEN, -1, krt);
        lp(&lp_2, klp);
        read_b(F_DAP2A_BASE, F_DAP2A_LEN, -1, kap);
        write_ap_b(F_DAP2A_BASE, -kap);
        read_b(F_DAP2B_BASE, F_DAP2B_LEN, -1, -kap);
        write_ap_b(F_DAP2B_BASE, kap);
        write_b(F_DEL2_BASE, 0, 2.0f);
        const float wet_r = s_acc;

        // The firmware crossfades each output with amount. The mono output is the mean of both.
        const float wet = 0.5f * (wet_l + wet_r) * 32768.0f;
        dst[i] = ot_f16s((dry + (wet - dry) * amount) * (1.0f / 32768.0f));
    }
    s_lp0 = ot_flush(lp_0);
    s_lp1 = ot_flush(lp_1);
    s_lp2 = ot_flush(lp_2);
    ot_pass_other_channels(in, out, samples, channels);
    return (int32_t)samples;
}
