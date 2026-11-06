// BBD: a live stereo master insert plugin (bbd.plg), the classic two mode BBD chorus. The mono sum passes three one pole
// low-pass stages (the BBD's filters, Tone) and a gentle soft clip (standing in for the compander) into one int16 delay line,
// read by two taps moved by one triangle LFO on opposite phases, each tap mixed over the dry signal.
// Mode I: 0.5 Hz, 2.0 .. 5.0 ms. Mode II: 0.8 Hz, 1.5 .. 5.5 ms. Mix 100 = dry and wet equal, as on the classic chorus.
//
// Copyright (c) 2026 Marcin Spoczynski (OpenTracker). MIT licence for the BBDC module, Apache-2.0 for the shared DSP header.
// Port of the BBDC master insert of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx, modules/bbd/src/bbdc.cpp,
// commit 23d58b2, MIT, text in licenses/LICENSE-OpenTracker-fx-MIT.txt) and of the shared BBD chorus DSP it runs,
// audio/bbd_chorus.h of OpenTracker 3.x (https://github.com/sandlbn/polyend-tracker-re, branch v3_fixes, commit 789d55a4c5e8,
// Apache-2.0, text in licenses/LICENSE-OpenTracker-Apache-2.0.txt, copy in synth-open/ot/audio/bbd_chorus.h).
//
// Modified: rewritten in C, one process() per block as in the original (the same ramps of the tap delays, wet and width over
// the block, so a parameter change never clicks), the sample rate from the host (44117.6 Hz), the Tone coefficient built in
// set_param with the host's expf (the original calls it in the block), the delay line inside the instance (512 B, no
// static storage), the mode a parameter (Mode I or II, the chorus is on while the plugin is loaded), the "Off and faded
// out" and clear paths dropped (Mix 0 fades the wet out and the dry passes untouched). All state lives in the instance.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c

#define LINE 256u   // frames (5.8 ms), int16 (512 B)

typedef struct { float hz, centre, depth; } Mode;   // LFO rate, centre delay and swing in frames
#define MS (0.001f * 44117.647f)
static const Mode kModes[2] = {
    {0.5f, 3.5f * MS, 1.5f * MS},   // Chorus I: 2.0 .. 5.0 ms
    {0.8f, 3.5f * MS, 2.0f * MS},   // Chorus II: 1.5 .. 5.5 ms
};

typedef struct
{
    int16_t line[LINE];
    uint32_t w;            // line write index
    float ph;              // LFO phase, cycles
    float dl, dr;          // tap delays the last block ended on (0: start at the mode's)
    float wet;             // the wet amount the last block ended on
    float width;           // the width the last block ended on
    float g;               // one pole coefficient of the BBD filters (Tone)
    float lp[3];           // the BBD filters' states
    float fs;
    float mixT, widthT;    // targets: Mix, Width 0..1
    uint8_t mode;          // the mode the last block ran, 0 or 1
    uint8_t modeT;         // the mode parameter
    uint8_t widthSet;      // width is valid (0: jump to the first target)
} Inst;

static const char* const kModeNames[2] = {"I", "II"};
static const plug_param kParams[4] = {
    {"Mode", "", 0.0f, 1.0f, 0.0f, 1.0f, 2, 0, kModeNames},
    {"Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, 0},
    {"Width", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, 0},
    {"Tone", "%", 0.0f, 100.0f, 50.0f, 1.0f, 0, 0, 0},   // 0..100 = BBD filter 6 .. 12 kHz
};

static inline float absf(float x) { return __builtin_fabsf(x); }
static inline float tri(float x) { return 4.0f * absf(x - 0.5f) - 1.0f; }   // 1 at 0, -1 at 1/2
static inline float clip(float x)   // soft clip: unity slope at 0, flat at +-1.5 -> +-1
{
    x = 0.5f * (absf(x + 1.5f) - absf(x - 1.5f));
    return x - (4.0f / 27.0f) * x * x * x;
}
static inline int16_t to_q15(float x)
{
    x = x > 0.99997f ? 0.99997f : (x < -1.0f ? -1.0f : x);
    return (int16_t)(x * 32768.0f);
}
static inline float flush(float x) { return absf(x) < 1.0e-20f ? 0.0f : x; }

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    switch (index)
    {
        case 0: s->modeT = value >= 0.5f ? 1 : 0; break;
        case 1: s->mixT = value < 0.0f ? 0.0f : (value > 100.0f ? 1.0f : 0.01f * value); break;
        case 2: s->widthT = value < 0.0f ? 0.0f : (value > 100.0f ? 1.0f : 0.01f * value); break;
        case 3:
        {
            const float t = value < 0.0f ? 0.0f : (value > 100.0f ? 100.0f : value);
            const float hz = 6000.0f * plug_rt_host->powf(2.0f, 0.01f * t);   // 6 .. 12 kHz
            s->g = 1.0f - plug_rt_host->expf(-6.2831853f * hz / s->fs);
            break;
        }
        default: break;
    }
}

static void reset(void* p)
{
    Inst* s = (Inst*)p;
    for (uint32_t i = 0; i < LINE; i++) s->line[i] = 0;
    s->w = 0;
    s->ph = s->dl = s->dr = s->wet = 0.0f;
    s->mode = s->modeT;
    s->widthSet = 0;
    s->lp[0] = s->lp[1] = s->lp[2] = 0.0f;
}

static int32_t init(void* p, const plug_host* host)
{
    Inst* s = (Inst*)p;
    s->fs = host->sample_rate > 0.0f ? host->sample_rate : 44117.647f;
    for (uint32_t i = 0; i < 4; i++) set_param(p, i, kParams[i].def);
    reset(p);
    return 0;
}

static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2 || frames == 0) return;
    float* const l = ch[0];
    float* const r = ch[1];
    const uint32_t n = frames;
    const float wetEnd = s->mixT, widthEnd = s->widthT;
    if (wetEnd <= 0.0f && s->wet <= 0.0f)   // Mix 0 and faded out: untouched, the line holds the last audio (cleared below)
    {
        if (s->w || s->lp[0] != 0.0f) reset(p);
        return;
    }
    if (!s->widthSet)
    {
        s->width = widthEnd;
        s->widthSet = 1;
    }
    s->mode = s->modeT;
    const Mode* md = &kModes[s->mode];
    // The triangle is a straight line between its corners: the tap delays at the block ends, ramped from where the last block
    // ended (a corner inside a block rounds it off, a mode switch glides over one block).
    float ph1 = s->ph + md->hz * (float)n / s->fs;
    ph1 -= (float)(int)ph1;
    const float t1 = tri(ph1);
    if (s->dl <= 0.0f)
    {
        s->dl = md->centre + md->depth * tri(s->ph);
        s->dr = md->centre - md->depth * tri(s->ph);
    }
    const float inv = 1.0f / (float)n;
    float dl = s->dl, dr = s->dr;
    const float dl1 = md->centre + md->depth * t1, dr1 = md->centre - md->depth * t1;
    const float ddl = (dl1 - dl) * inv, ddr = (dr1 - dr) * inv;
    // Gains at the block ends, ramped: dry ducks as the wet comes in (at wet 1 both 0.7), the taps' sum on both sides, their
    // difference scaled by Width. kG: 0.7 / 2 with the int16 line's 1 / 32768 folded in.
    const float kG = 0.35f / 32768.0f;
    const float wet = s->wet;
    float dry = 1.0f - 0.3f * wet, mid = kG * wet, side = kG * wet * s->width;
    const float ddry = -0.3f * (wetEnd - wet) * inv, dmid = kG * (wetEnd - wet) * inv;
    const float dside = (kG * wetEnd * widthEnd - side) * inv;
    const float g = s->g;
    const float gh = 0.5f * g;   // the mono sum's 1/2 folded in
    float a0 = s->lp[0], a1 = s->lp[1], a2 = s->lp[2];
    int16_t* const line = s->line;
    uint32_t w = s->w;
    for (uint32_t i = 0; i < n; ++i)
    {
        a0 += gh * (l[i] + r[i]) - g * a0;
        a1 += g * (a0 - a1);
        a2 += g * (a1 - a2);
        line[w] = to_q15(clip(a2));
        // A tap d = k + f frames back: between w - k and w - k - 1.
        const uint32_t kl = (uint32_t)dl, kr = (uint32_t)dr;
        const float fl = dl - (float)kl, fr = dr - (float)kr;
        const float x0 = (float)line[(w - kl) & (LINE - 1u)], x1 = (float)line[(w - kl - 1u) & (LINE - 1u)];
        const float y0 = (float)line[(w - kr) & (LINE - 1u)], y1 = (float)line[(w - kr - 1u) & (LINE - 1u)];
        const float tl = x0 + fl * (x1 - x0), tr = y0 + fr * (y1 - y0);
        const float m = mid * (tl + tr), sd = side * (tl - tr);
        l[i] = dry * l[i] + (m + sd);
        r[i] = dry * r[i] + (m - sd);
        w = (w + 1u) & (LINE - 1u);
        dl += ddl;
        dr += ddr;
        dry += ddry;
        mid += dmid;
        side += dside;
    }
    s->w = w;
    s->ph = ph1;
    s->dl = dl1;
    s->dr = dr1;
    s->wet = wetEnd;
    s->width = widthEnd;
    // No denormals as the filters decay in silence (the line truncates them).
    s->lp[0] = flush(a0);
    s->lp[1] = flush(a1);
    s->lp[2] = flush(a2);
    if (wetEnd <= 0.0f) reset(p);   // faded out: an empty line, the taps back at their start
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "BBD", "OpenTracker (M. Spoczynski)", 4, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
