// PTFX_NAME: Plate Reverb
// PTFX_AUTHOR: MrAnthropology
// PTFX_FLAGS: EFFECT, STEREO
// PTFX_SCRATCH: 222208

// Dattorro plate (J. Dattorro, "Effect Design Part 1", JAES 1997), ported from
// the live master plate in mranthropology/Tracker-Firmware-Mod (fxbank/fx_bank.c)
// for offline use: same tank, delays scaled from 29761 Hz to the Tracker's rate,
// but float delay lines (no CPU budget to meet here) and a dry/wet mix.
// Processed in sub-blocks of up to 128 samples; every delay is longer than
// that, so each stage runs over a whole sub-block.
//
// Status: tested on an original Tracker (MK66) with CE 1.0.0. Experimental;
// not an official Polyend plugin.

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[6] = {
    PTFX_PARAM_INT("Size",    0.0f, 100.0f, 60.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Damp",    0.0f, 100.0f, 40.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Diffuse", 0.0f, 100.0f, 68.0f, 1.0f, "%"),
    PTFX_PARAM_INT("PreDly",  0.0f, 150.0f, 10.0f, 1.0f, "ms"),
    PTFX_PARAM_INT("Mix",     0.0f, 100.0f, 30.0f, 1.0f, "%"),
    PTFX_PARAM_LIST("Tail", 0, "Ring Out|Cut Off")
};

#define FS          44100.0f                    // host->sample_rate
#define SUB         128                         // sub-block
#define K_RATE      (FS / 29761.0f)
#define D(x)        ((uint32_t)((x) * K_RATE + 0.5f))
#define EXCURSION   24                          // modulated all-pass, peak samples
#define MOD_HZ      0.9f
#define OUT_GAIN    0.6f                        // Dattorro's tap gain
#define WET_GAIN    1.1f                        // wet RMS ~ dry RMS at Mix 100 %
#define MAX_TAIL_S  6.0f
#define FADE_S      0.5f                        // tail fade-out at the end of Ring Out

// ring sizes (powers of two, >= longest read + SUB) and offsets in floats
#define SZ_PRE   8192u
#define SZ_AP1    256u
#define SZ_AP2    256u
#define SZ_AP3   1024u
#define SZ_AP4    512u
#define SZ_LMOD  1024u
#define SZ_LD1   8192u
#define SZ_LAP   4096u
#define SZ_LD2   8192u
#define SZ_RMOD  2048u
#define SZ_RD1   8192u
#define SZ_RAP   4096u
#define SZ_RD2   8192u
#define OFF_PRE  0u
#define OFF_AP1  (OFF_PRE + SZ_PRE)
#define OFF_AP2  (OFF_AP1 + SZ_AP1)
#define OFF_AP3  (OFF_AP2 + SZ_AP2)
#define OFF_AP4  (OFF_AP3 + SZ_AP3)
#define OFF_LMOD (OFF_AP4 + SZ_AP4)
#define OFF_LD1  (OFF_LMOD + SZ_LMOD)
#define OFF_LAP  (OFF_LD1 + SZ_LD1)
#define OFF_LD2  (OFF_LAP + SZ_LAP)
#define OFF_RMOD (OFF_LD2 + SZ_LD2)
#define OFF_RD1  (OFF_RMOD + SZ_RMOD)
#define OFF_RAP  (OFF_RD1 + SZ_RD1)
#define OFF_RD2  (OFF_RAP + SZ_RAP)
#define LINE_WORDS (OFF_RD2 + SZ_RD2)           // 54,272 floats

typedef struct {
    float mem[LINE_WORDS];
    float a[SUB], l[SUB], r[SUB], tmp[SUB];      // work buffers (not on the stack)
    float lfo_l[SUB], lfo_r[SUB];                // per-sample chorus LFO, quadrature
    float win[SUB + 2 * EXCURSION + 4];
    uint32_t t;                                  // line time, advances per sub-block
    uint32_t tail_pos;                           // samples rendered into the tail
    float bw_z, damp_l, damp_r, lfo_phase;
} PlateState;

_Static_assert(sizeof(PlateState) <= 222208, "PTFX_SCRATCH too small");

static const PtfxHostApi* s_host = 0;
static PlateState* s_state = 0;
static float    s_size = 0.6f, s_damp = 0.4f, s_diff = 0.68f, s_mix = 0.3f;
static uint32_t s_pre = 441;
static uint8_t  s_tail_mode = 0;               // 0 = Ring Out, 1 = Cut Off
static uint32_t s_tail_len = 0;

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

static float sin_poly(float x)                 // |x| <= pi/2
{
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040
                + x2 * (1.0f / 362880)))));
}

static float sin_turns(float p)                // sin(2 pi p), p in [0, 1)
{
    float x = p * 4.0f;
    if (x < 1.0f) return sin_poly(x * 1.57079633f);
    if (x < 3.0f) return sin_poly((2.0f - x) * 1.57079633f);
    return sin_poly((x - 4.0f) * 1.57079633f);
}

static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }

static inline float decay_of(float size) { return clampf(0.30f + 0.68f * size, 0.0f, 0.97f); }

// ---------- delay lines ----------

static void load(uint32_t off, uint32_t mask, uint32_t pos, uint32_t n, float* out)
{
    const float* b = s_state->mem + off;
    for (uint32_t i = 0; i < n; i++) out[i] = b[(pos + i) & mask];
}

static void store(uint32_t off, uint32_t mask, uint32_t pos, uint32_t n, const float* in)
{
    float* b = s_state->mem + off;
    for (uint32_t i = 0; i < n; i++) b[(pos + i) & mask] = in[i];
}

// acc[i] += g * line[t + i - d]
static void tap_add(uint32_t off, uint32_t mask, uint32_t t, uint32_t d, float g, uint32_t n, float* acc)
{
    const float* b = s_state->mem + off;
    for (uint32_t i = 0; i < n; i++) acc[i] += g * b[(t + i - d) & mask];
}

// Lattice all-pass in place: w = x + g*w[n-d]; y = w[n-d] - g*w (d > n)
static void allpass(uint32_t off, uint32_t mask, uint32_t t, uint32_t d, float g, uint32_t n, float* x)
{
    float* b = s_state->mem + off;
    for (uint32_t i = 0; i < n; i++) {
        float wd = b[(t + i - d) & mask];
        float w = x[i] + g * wd;
        x[i] = wd - g * w;
        b[(t + i) & mask] = w;
    }
}

// All-pass with the delay modulated by d + EXCURSION * lfo[i]
static void allpass_mod(uint32_t off, uint32_t mask, uint32_t t, uint32_t d,
                        float g, const float* lfo, uint32_t n, float* x)
{
    float* win = s_state->win;
    float* w_out = s_state->tmp;
    load(off, mask, t - d - EXCURSION - 2, n + 2 * EXCURSION + 4, win);
    for (uint32_t i = 0; i < n; i++) {
        float pos = (float)(i + EXCURSION + 2) - (float)EXCURSION * lfo[i];
        int32_t ip = (int32_t)pos;
        float f = pos - (float)ip;
        float wd = win[ip] + f * (win[ip + 1] - win[ip]);
        float w = x[i] + g * wd;
        x[i] = wd - g * w;
        w_out[i] = w;
    }
    store(off, mask, t, n, w_out);
}

// a[0..n) mono in -> l, r wet out
static void plate(uint32_t n)
{
    PlateState* s = s_state;
    const uint32_t t = s->t;
    float *a = s->a, *l = s->l, *r = s->r, *d1 = s->tmp;

    const float decay = decay_of(s_size);
    const float dd1 = 0.70f;                                  // decay diffusion 1
    const float dd2 = clampf(decay + 0.15f, 0.25f, 0.50f);   // Dattorro's rule
    const float id1 = clampf(1.10f * s_diff, 0.0f, 0.85f);
    const float id2 = clampf(0.92f * s_diff, 0.0f, 0.75f);
    const float dmp = 0.85f * s_damp;
    const float bw = 0.9995f;

    // predelay: write first, so any delay 0..SZ_PRE-SUB works
    store(OFF_PRE, SZ_PRE - 1, t, n, a);
    load(OFF_PRE, SZ_PRE - 1, t - s_pre, n, a);

    float z = s->bw_z;                                        // input bandwidth
    for (uint32_t i = 0; i < n; i++) { z += bw * (a[i] - z); a[i] = z; }
    s->bw_z = z;

    allpass(OFF_AP1, SZ_AP1 - 1, t, D(142), id1, n, a);
    allpass(OFF_AP2, SZ_AP2 - 1, t, D(107), id1, n, a);
    allpass(OFF_AP3, SZ_AP3 - 1, t, D(379), id2, n, a);
    allpass(OFF_AP4, SZ_AP4 - 1, t, D(277), id2, n, a);

    // tank inputs: diffused input + decay * the other side's last delay
    load(OFF_RD2, SZ_RD2 - 1, t - D(3163), n, l);
    load(OFF_LD2, SZ_LD2 - 1, t - D(3720), n, r);
    for (uint32_t i = 0; i < n; i++) {
        l[i] = a[i] + decay * l[i];
        r[i] = a[i] + decay * r[i];
    }

    // stepped per sample, so the result does not depend on how the host splits blocks
    float p = s->lfo_phase;
    for (uint32_t i = 0; i < n; i++) {
        s->lfo_l[i] = sin_turns(p);
        s->lfo_r[i] = sin_turns(wrap1(p + 0.25f));
        p = wrap1(p + MOD_HZ / FS);
    }
    s->lfo_phase = p;

    // left half
    allpass_mod(OFF_LMOD, SZ_LMOD - 1, t, D(672), -dd1, s->lfo_l, n, l);
    load(OFF_LD1, SZ_LD1 - 1, t - D(4453), n, d1);
    store(OFF_LD1, SZ_LD1 - 1, t, n, l);
    float y = s->damp_l;
    for (uint32_t i = 0; i < n; i++) { y = d1[i] + dmp * (y - d1[i]); l[i] = decay * y; }
    s->damp_l = y;
    allpass(OFF_LAP, SZ_LAP - 1, t, D(1800), dd2, n, l);
    store(OFF_LD2, SZ_LD2 - 1, t, n, l);

    // right half
    allpass_mod(OFF_RMOD, SZ_RMOD - 1, t, D(908), -dd1, s->lfo_r, n, r);
    load(OFF_RD1, SZ_RD1 - 1, t - D(4217), n, d1);
    store(OFF_RD1, SZ_RD1 - 1, t, n, r);
    y = s->damp_r;
    for (uint32_t i = 0; i < n; i++) { y = d1[i] + dmp * (y - d1[i]); r[i] = decay * y; }
    s->damp_r = y;
    allpass(OFF_RAP, SZ_RAP - 1, t, D(2656), dd2, n, r);
    store(OFF_RD2, SZ_RD2 - 1, t, n, r);

    // output taps: the first four of each side of Dattorro's table 2
    const float g = OUT_GAIN;
    for (uint32_t i = 0; i < n; i++) { l[i] = 0.0f; r[i] = 0.0f; }
    tap_add(OFF_RD1, SZ_RD1 - 1, t, D(266),  g, n, l);
    tap_add(OFF_RD1, SZ_RD1 - 1, t, D(2974), g, n, l);
    tap_add(OFF_RAP, SZ_RAP - 1, t, D(1913), -g, n, l);
    tap_add(OFF_RD2, SZ_RD2 - 1, t, D(1996), g, n, l);

    tap_add(OFF_LD1, SZ_LD1 - 1, t, D(353),  g, n, r);
    tap_add(OFF_LD1, SZ_LD1 - 1, t, D(3627), g, n, r);
    tap_add(OFF_LAP, SZ_LAP - 1, t, D(1228), -g, n, r);
    tap_add(OFF_LD2, SZ_LD2 - 1, t, D(2673), g, n, r);

    s->t = t + n;
}

// ---------- tail ----------

// Time for the tank to fall 60 dB: one trip round both halves takes
// LOOP_S and passes the decay gain four times (damping only shortens it).
#define LOOP_S ((672.0f + 4453.0f + 1800.0f + 3720.0f + 908.0f + 4217.0f + 2656.0f + 3163.0f) / 29761.0f)

static void update_tail_length(void)
{
    if (!s_host || !s_host->set_extra_samples)
        return;
    if (s_tail_mode != 0 || s_mix <= 0.0f || !s_host->logf) {
        s_tail_len = 0;
        s_host->set_extra_samples(0, 0);
        return;
    }
    float per_loop = -4.0f * s_host->logf(decay_of(s_size));  // nepers per loop
    float rt60 = per_loop > 0.0f ? LOOP_S * 6.9077553f / per_loop : MAX_TAIL_S;
    float tail = clampf(rt60 + (float)s_pre / FS, 2.0f * FADE_S, MAX_TAIL_S);
    s_tail_len = (uint32_t)(tail * FS);
    s_host->set_extra_samples(0, s_tail_len);
}

// ---------- plugin entry points ----------

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host)
{
    s_host = host;
    s_size = 0.6f; s_damp = 0.4f; s_diff = 0.68f; s_mix = 0.3f;
    s_pre = 441; s_tail_mode = 0; s_tail_len = 0;
    if (scratch_mem && scratch_size >= sizeof(PlateState)) {
        s_state = (PlateState*)scratch_mem;          // zeroed by the firmware
        s_state->t = 0;
        s_state->tail_pos = 0;
        s_state->bw_z = s_state->damp_l = s_state->damp_r = s_state->lfo_phase = 0.0f;
    } else {
        s_state = 0;                                 // pass-through
    }
    update_tail_length();
}

void plugin_set_param(uint8_t index, float value)
{
    if (index == 0)      s_size = clampf(value * 0.01f, 0.0f, 1.0f);
    else if (index == 1) s_damp = clampf(value * 0.01f, 0.0f, 1.0f);
    else if (index == 2) s_diff = clampf(value * 0.01f, 0.0f, 1.0f);
    else if (index == 3) {
        float p = clampf(value, 0.0f, 150.0f) * (FS / 1000.0f) + 0.5f;
        s_pre = (uint32_t)p;
    }
    else if (index == 4) s_mix = clampf(value * 0.01f, 0.0f, 1.0f);
    else if (index == 5) s_tail_mode = (value >= 0.5f) ? 1 : 0;
    update_tail_length();
}

static inline int16_t to_s16(float x)
{
    return (int16_t)SATURATE_16BIT((int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f));
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels)
{
    if (!out || !out[0] || samples == 0)
        return 0;
    if (channels > 2)
        channels = 2;
    const int16_t* src0 = (in != 0) ? in[0] : 0;
    const int16_t* src1 = (in != 0 && channels == 2) ? in[1] : 0;

    if (!s_state) {
        for (uint8_t ch = 0; ch < channels; ch++) {
            if (!out[ch]) continue;
            const int16_t* src = ch ? src1 : src0;
            for (uint32_t i = 0; i < samples; i++) out[ch][i] = src ? src[i] : 0;
        }
        return (int32_t)samples;
    }

    const float dry_g = 1.0f - s_mix;
    const float wet_g = s_mix * WET_GAIN * 32768.0f;
    const uint32_t fade = (uint32_t)(FADE_S * FS);

    for (uint32_t done = 0; done < samples; ) {
        uint32_t n = samples - done;
        if (n > SUB) n = SUB;
        float* a = s_state->a;
        for (uint32_t i = 0; i < n; i++) {
            float x0 = src0 ? (float)src0[done + i] : 0.0f;
            float x1 = (channels == 2) ? (src1 ? (float)src1[done + i] : 0.0f) : x0;
            a[i] = (x0 + x1) * (0.5f / 32768.0f);
        }
        plate(n);
        const float* l = s_state->l;
        const float* r = s_state->r;
        const int tail = (src0 == 0);
        for (uint32_t i = 0; i < n; i++) {
            float wg = wet_g;
            if (tail) {                              // fade the end of the ring-out
                uint32_t left = s_tail_len > s_state->tail_pos ? s_tail_len - s_state->tail_pos : 0;
                if (left < fade) wg *= (float)left / (float)fade;
                s_state->tail_pos++;
            }
            if (channels == 2) {
                float d0 = src0 ? (float)src0[done + i] : 0.0f;
                float d1 = src1 ? (float)src1[done + i] : 0.0f;
                if (out[0]) out[0][done + i] = to_s16(dry_g * d0 + wg * l[i]);
                if (out[1]) out[1][done + i] = to_s16(dry_g * d1 + wg * r[i]);
            } else {
                float d0 = src0 ? (float)src0[done + i] : 0.0f;
                out[0][done + i] = to_s16(dry_g * d0 + wg * 0.70710678f * (l[i] + r[i]));
            }
        }
        done += n;
    }
    return (int32_t)samples;
}
