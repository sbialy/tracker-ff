// Host render harness for .ptfx effect plugins (not part of any firmware).
//
// Compiled together with ONE plugin source, so the code under test is the very
// C file the ARM builder compiles. A fake host table backed by the real libm
// stands in for the Tracker. It renders a synthetic test sample through the
// plugin at the default parameters and at extreme ones, writes WAVs, and checks
// the result. Driven by tools/ptfx_host_check.py, which passes:
//   -DPTFX_HOST_TEST -DPLUGIN_NPARAMS=<n> -DPLUGIN_SCRATCH=<bytes> -DPLUGIN_TAG=\"ot_tape\"
//
//   usage: <binary> <out_dir>
//
// Checks per render: no NaN or inf inside the plugin, return value equals the
// block length, scratch guard bytes untouched, output not silent, no full-scale
// stuck output, tails do not grow (and for the default render they decay).
// Per plugin: re-running the default render gives identical samples (plugin_init
// resets all state), a render in place gives identical samples to a separate
// output buffer, and chopping the same render into small blocks gives identical
// samples to 8192 sample blocks.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracker_plugin.h"

#ifndef PLUGIN_NPARAMS
#error "define PLUGIN_NPARAMS"
#endif
#ifndef PLUGIN_SCRATCH
#define PLUGIN_SCRATCH 0
#endif
#ifndef PLUGIN_TAG
#define PLUGIN_TAG "plugin"
#endif

unsigned g_ptfx_nonfinite = 0;

extern const PluginParam PLUGIN_PARAMS[];
void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host);
void plugin_set_param(uint8_t index, float value);
int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels);

#define FS 44100
#define GUARD 4096
#define MAX_EXTRA (FS * 30)

// ---- fake host ---------------------------------------------------------------
static uint32_t g_prepend, g_append;
static void host_set_extra(uint32_t p, uint32_t a) { g_prepend = p; g_append = a; }
static float h_fminf(float a, float b) { return fminf(a, b); }
static float h_fmaxf(float a, float b) { return fmaxf(a, b); }
static float h_fabsf(float a) { return fabsf(a); }
static float h_log10f(float a) { return log10f(a); }
static PtfxHostApi g_host = {
    FS, sinf, cosf, tanf, tanhf, powf, expf, sqrtf, logf, h_log10f,
    h_fminf, h_fmaxf, h_fabsf, memcpy, memset, rand, host_set_extra,
};

// ---- test signal -------------------------------------------------------------
static uint32_t g_rng = 12345u;
static float noise(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(int32_t)g_rng * (1.0f / 2147483648.0f);
}

static int16_t* make_input(uint32_t* n_out) {
    const uint32_t n = FS * 2;
    float* x = (float*)calloc(n, sizeof(float));
    g_rng = 12345u;
    // 0.00-0.50 s kick: exponential sine sweep 130 -> 45 Hz
    double ph = 0;
    for (uint32_t i = 0; i < FS / 2; i++) {
        double t = (double)i / FS;
        double f = 45.0 + 85.0 * exp(-t * 28.0);
        ph += 2.0 * M_PI * f / FS;
        x[i] += (float)(0.85 * sin(ph) * exp(-t * 7.0));
    }
    // 0.50-1.25 s: A minor chord of saws, plucked
    const double notes[3] = {220.0, 329.63, 440.0};
    for (uint32_t i = FS / 2; i < FS * 5 / 4; i++) {
        double t = (double)(i - FS / 2) / FS;
        double env = exp(-t * 3.0) * (t < 0.003 ? t / 0.003 : 1.0);
        double s = 0;
        for (int k = 0; k < 3; k++) {
            double p = fmod(t * notes[k], 1.0);
            s += 2.0 * p - 1.0;
        }
        x[i] += (float)(0.22 * env * s / 3.0 * 3.0 * 0.5);
    }
    // closed hats every 250 ms: 8 ms of differenced noise
    for (uint32_t b = 0; b < 8; b++) {
        uint32_t s0 = b * FS / 4;
        float prev = 0;
        for (uint32_t i = 0; i < FS * 8 / 1000 && s0 + i < n; i++) {
            float w = noise();
            x[s0 + i] += 0.35f * (w - prev) * 0.5f * expf(-(float)i / (FS * 0.0025f));
            prev = w;
        }
    }
    // snare at 1.0 s and 1.5 s: noise burst plus a 190 Hz body
    for (int sn = 0; sn < 2; sn++) {
        uint32_t s0 = FS + sn * FS / 2;
        for (uint32_t i = 0; i < FS / 5 && s0 + i < n; i++) {
            float e = expf(-(float)i / (FS * 0.05f));
            x[s0 + i] += 0.45f * noise() * e + 0.35f * sinf(2.0f * (float)M_PI * 190.0f * i / FS) * e;
        }
    }
    float pk = 0;
    for (uint32_t i = 0; i < n; i++) pk = fmaxf(pk, fabsf(x[i]));
    int16_t* o = (int16_t*)malloc(n * sizeof(int16_t));
    for (uint32_t i = 0; i < n; i++) o[i] = (int16_t)lrintf(x[i] / pk * 0.80f * 32767.0f);   // peak -2 dBFS
    free(x);
    *n_out = n;
    return o;
}

// ---- wav ---------------------------------------------------------------------
static void write_wav(const char* path, const int16_t* d, uint32_t n) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(2); }
    uint32_t data = n * 2, riff = 36 + data, rate = FS, brate = FS * 2, fmtlen = 16;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(d, 2, n, f);
    fclose(f);
}

// ---- render ------------------------------------------------------------------
typedef struct {
    int16_t* pcm;        // prepend + sample + append
    uint32_t total, prepend, sample_len, append;
    int ok;              // plugin returned the block length every call, guards intact, no non-finite
    char why[160];
} Render;

static int param_is_list(int i) { return PLUGIN_PARAMS[i].type == PARAM_LIST; }
static int list_count(int i) {
    int c = 1;
    for (const char* p = PLUGIN_PARAMS[i].unit; *p; p++) if (*p == '|') c++;
    return c;
}
static float pmin(int i) { return PLUGIN_PARAMS[i].min; }
static float pmax(int i) { return param_is_list(i) ? (float)(list_count(i) - 1) : PLUGIN_PARAMS[i].max; }
static float pdef(int i) { return PLUGIN_PARAMS[i].default_val; }

// blk: block length per call, inplace: in == out.
static Render render(const int16_t* in, uint32_t n, const float* vals, uint32_t blk, int inplace) {
    Render r;
    memset(&r, 0, sizeof(r));
    r.ok = 1;
    uint8_t* mem = (uint8_t*)malloc(PLUGIN_SCRATCH + 2 * GUARD + 16);
    uint8_t* scratch = mem + GUARD;
    memset(mem, 0xA5, PLUGIN_SCRATCH + 2 * GUARD + 16);
    memset(scratch, 0, PLUGIN_SCRATCH);
    g_prepend = g_append = 0;
    g_ptfx_nonfinite = 0;
    plugin_init(PLUGIN_SCRATCH ? scratch : NULL, PLUGIN_SCRATCH, &g_host);
    for (int i = 0; i < PLUGIN_NPARAMS; i++) plugin_set_param((uint8_t)i, vals[i]);
    // a host may change parameters before the first block, take the final extra lengths
    uint32_t pre = g_prepend, app = g_append;
    if (app > MAX_EXTRA) app = MAX_EXTRA;
    r.prepend = pre; r.append = app; r.sample_len = n;
    r.total = pre + n + app;
    int16_t* src = (int16_t*)calloc(r.total + 1, sizeof(int16_t));
    memcpy(src + pre, in, n * sizeof(int16_t));
    int16_t* dst = inplace ? src : (int16_t*)calloc(r.total + 1, sizeof(int16_t));
    for (uint32_t pos = 0; pos < r.total; pos += blk) {
        uint32_t len = r.total - pos < blk ? r.total - pos : blk;
        const int16_t* ip[1] = {src + pos};
        int16_t* op[1] = {dst + pos};
        // the loader reads the extra lengths again after every set_param, so do the same
        int32_t rv = plugin_process_block(ip, op, len, 1);
        if (rv != (int32_t)len) { r.ok = 0; snprintf(r.why, sizeof r.why, "returned %d for %u samples", rv, len); break; }
    }
    if (g_ptfx_nonfinite) { r.ok = 0; snprintf(r.why, sizeof r.why, "%u non-finite values inside the plugin", g_ptfx_nonfinite); }
    for (uint32_t i = 0; i < GUARD; i++)
        if (mem[i] != 0xA5 || mem[GUARD + PLUGIN_SCRATCH + i] != 0xA5) { r.ok = 0; snprintf(r.why, sizeof r.why, "scratch guard overwritten"); break; }
    r.pcm = dst;
    if (!inplace) free(src);
    free(mem);
    return r;
}

static double rms_db(const int16_t* d, uint32_t a, uint32_t b) {
    if (b <= a) return -200.0;
    double s = 0;
    for (uint32_t i = a; i < b; i++) { double v = d[i] / 32768.0; s += v * v; }
    s /= (b - a);
    return s <= 1e-20 ? -200.0 : 10.0 * log10(s);
}

static int g_fail = 0;
#define FAIL(...) do { printf("    FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } while (0)

static void check(const char* scen, Render* r, int is_default) {
    const int16_t* d = r->pcm;
    uint32_t n = r->total;
    if (!r->ok) FAIL("%s", r->why);
    int16_t pk = 0; uint32_t clipped = 0;
    for (uint32_t i = 0; i < n; i++) {
        int v = d[i] < 0 ? -(int)d[i] : d[i];
        if (v > pk) pk = (int16_t)(v > 32767 ? 32767 : v);
        if (d[i] >= 32767 || d[i] <= -32768) clipped++;
    }
    uint32_t sl = r->prepend + r->sample_len;
    double rms_all = rms_db(d, 0, n), rms_body = rms_db(d, r->prepend, sl);
    double tail_a = -200, tail_b = -200;
    uint32_t win = FS / 20;   // 50 ms
    if (r->append >= 4 * win) {
        tail_a = rms_db(d, sl, sl + win);
        tail_b = rms_db(d, n - win, n);
    }
    printf("  %-8s len %6u (+%u pre +%u tail)  peak %6d  rms body %6.1f dBFS  tail %6.1f -> %6.1f dBFS  clip %u\n",
           scen, r->sample_len, r->prepend, r->append, pk, rms_body, tail_a, tail_b, clipped);
    if (rms_all < -80.0 || pk < 16) FAIL("output is silent (rms %.1f dBFS, peak %d)", rms_all, pk);
    if (clipped * 50u > n) FAIL("%u of %u samples at full scale (>2 %%)", clipped, n);
    // stuck output: the last 1024 samples flat and far from zero
    if (n > 2048) {
        int mn = 32767, mx = -32768; double mean = 0;
        for (uint32_t i = n - 1024; i < n; i++) { if (d[i] < mn) mn = d[i]; if (d[i] > mx) mx = d[i]; mean += d[i]; }
        mean /= 1024.0;
        if (mx - mn <= 4 && fabs(mean) > 2000.0) FAIL("stuck output at %.0f", mean);
    }
    if (r->append >= 4 * win) {
        if (tail_a > -90.0 && tail_b > tail_a + 0.5) FAIL("tail grows (%.1f -> %.1f dBFS)", tail_a, tail_b);
        if (tail_a > -90.0 && tail_b > tail_a - 6.0) FAIL("tail does not decay by 6 dB (%.1f -> %.1f dBFS)", tail_a, tail_b);
        if (is_default && tail_a > -90.0 && tail_b > tail_a - 20.0 && tail_b > -60.0)
            FAIL("default tail does not decay (%.1f -> %.1f dBFS)", tail_a, tail_b);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <out_dir>\n", argv[0]); return 2; }
    const char* dir = argv[1];
    uint32_t n;
    int16_t* in = make_input(&n);
    char path[512];
    snprintf(path, sizeof path, "%s/_input.wav", dir);
    write_wav(path, in, n);

    printf("%s: %d params, scratch %u B\n", PLUGIN_TAG, PLUGIN_NPARAMS, (unsigned)PLUGIN_SCRATCH);
    float v[16];
    // PTFX_PARAMS="v0,v1,..." renders just that one parameter set as <tag>_custom.wav (for listening or probing)
    const char* custom = getenv("PTFX_PARAMS");
    if (custom) {
        const char* p = custom;
        for (int i = 0; i < PLUGIN_NPARAMS; i++) {
            char* e;
            v[i] = i == 0 || *p ? strtof(p, &e) : pdef(i);
            if (*p) { p = *e == ',' ? e + 1 : e; }
        }
        Render r = render(in, n, v, 8192, 0);
        check("custom", &r, 0);
        snprintf(path, sizeof path, "%s/%s_custom.wav", dir, PLUGIN_TAG);
        write_wav(path, r.pcm, r.total);
        free(r.pcm); free(in);
        return g_fail ? 1 : 0;
    }
    const char* names[5] = {"default", "min", "max", "mixA", "mixB"};
    int16_t* default_pcm = NULL; uint32_t default_total = 0;
    for (int s = 0; s < 5; s++) {
        for (int i = 0; i < PLUGIN_NPARAMS; i++) {
            switch (s) {
                case 0: v[i] = pdef(i); break;
                case 1: v[i] = pmin(i); break;
                case 2: v[i] = pmax(i); break;
                case 3: v[i] = (i & 1) ? pmax(i) : pmin(i); break;
                default: v[i] = (i & 1) ? pmin(i) : pmax(i); break;
            }
        }
        Render r = render(in, n, v, 8192, 0);
        check(names[s], &r, s == 0);
        snprintf(path, sizeof path, "%s/%s_%s.wav", dir, PLUGIN_TAG, names[s]);
        write_wav(path, r.pcm, r.total);
        // chunking must not change the result
        Render c = render(in, n, v, 37, 0);
        if (c.total != r.total || memcmp(c.pcm, r.pcm, r.total * 2) != 0) FAIL("%s: 37 sample blocks differ from 8192 sample blocks", names[s]);
        // in place must equal separate buffers
        Render ip = render(in, n, v, 8192, 1);
        if (ip.total != r.total || memcmp(ip.pcm, r.pcm, r.total * 2) != 0) FAIL("%s: in-place render differs", names[s]);
        free(c.pcm); free(ip.pcm);
        if (s == 0) { default_pcm = r.pcm; default_total = r.total; }
        else free(r.pcm);
    }
    // a second init + render of the defaults must be identical (state reset)
    for (int i = 0; i < PLUGIN_NPARAMS; i++) v[i] = pdef(i);
    Render again = render(in, n, v, 8192, 0);
    if (again.total != default_total || memcmp(again.pcm, default_pcm, default_total * 2) != 0) FAIL("second default render differs (state not reset by plugin_init)");
    free(again.pcm); free(default_pcm); free(in);
    printf("%s: %s\n", PLUGIN_TAG, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
