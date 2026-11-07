// Tilt: the smallest useful master insert plugin (tilt.plg). Output gain plus a spectral tilt around a pivot frequency.
// Worked example of docs/plugin_sdk.md, section 8. Copyright (c) 2026 sbialy (tracker-ff). MIT licence, see synth-open/plug/LICENSE.
// Original code, no third party source.
//
// Parameters: Gain -12..+12 dB, Tilt -6..+6 dB (positive lifts the lows and lowers the highs by the same amount, negative
// does the opposite, the two halves meet at the pivot), Pivot 200..2000 Hz.
//
// The signal is split by one pole low pass at the pivot: lo = lp(x), hi = x - lo, y = gl lo + gh hi, so lo + hi is exactly x
// and Tilt 0 is transparent. The gains are computed in set_param (powf is a host call, never in process) and move linearly
// from the value the last block ended on to the new target over the block, so a change never clicks.
//
// Rules the code follows (docs/plugin_sdk.md, section 5): all state lives in the instance, no globals that change, no libm in
// the loop, no allocation, a denormal flush on the filter state.
#include "plug_abi.h"

extern const plug_host* plug_rt_host;   // plug_rt.c, set in plug_entry

enum { P_GAIN, P_TILT, P_PIVOT, P_COUNT };

typedef struct
{
    float v[P_COUNT];     // the values as the host delivered them
    float a;              // low pass coefficient of the current Pivot
    float gl0, gh0;       // low and high gain at the block start
    float gl1, gh1;       // targets
    float lp[2];          // low pass state, left and right
} Inst;

static const plug_param kParams[P_COUNT] = {
    // name unit min max default step enum_count flags enum_names
    {"Gain", "dB", -12.0f, 12.0f, 0.0f, 0.5f, 0, 0, 0},
    {"Tilt", "dB", -6.0f, 6.0f, 0.0f, 0.5f, 0, 0, 0},
    {"Pivot", "Hz", 200.0f, 2000.0f, 650.0f, 10.0f, 0, 0, 0},
};

static inline float flush(float x) { return __builtin_fabsf(x) < 1.0e-20f ? 0.0f : x; }

// Targets of the stored values. Runs in the audio interrupt at a block start (set_param) and on the main thread (init).
static void retarget(Inst* s)
{
    const float gain = plug_rt_host->powf(10.0f, s->v[P_GAIN] * 0.05f);
    const float half = plug_rt_host->powf(10.0f, s->v[P_TILT] * 0.025f);   // +-Tilt / 2 dB, as an amplitude
    s->gl1 = gain * half;
    s->gh1 = gain / half;
    s->a = 1.0f - plug_rt_host->expf(-6.2831853f * s->v[P_PIVOT] / plug_rt_host->sample_rate);
}

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    if (index >= P_COUNT) return;
    s->v[index] = value;
    retarget(s);
}

static void reset(void* p)
{
    Inst* s = (Inst*)p;
    s->lp[0] = s->lp[1] = 0.0f;
    s->gl0 = s->gl1;
    s->gh0 = s->gh1;
}

static int32_t init(void* p, const plug_host* host)
{
    (void)host;
    Inst* s = (Inst*)p;   // zeroed by the host, the plugin starts at its defaults
    for (uint32_t i = 0; i < P_COUNT; i++) s->v[i] = kParams[i].def;
    retarget(s);
    reset(p);
    return 0;
}

static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2 || frames == 0) return;
    float* const L = ch[0];
    float* const R = ch[1];
    const float a = s->a;
    float lp0 = s->lp[0], lp1 = s->lp[1];   // state and gains in locals: a store to L[i] may alias the instance
    float gl = s->gl0, gh = s->gh0;
    const float inv = 1.0f / (float)frames;
    const float dgl = (s->gl1 - gl) * inv, dgh = (s->gh1 - gh) * inv;
    for (uint32_t i = 0; i < frames; i++)
    {
        gl += dgl;
        gh += dgh;
        float x = L[i];
        lp0 += a * (x - lp0);
        L[i] = gl * lp0 + gh * (x - lp0);
        x = R[i];
        lp1 += a * (x - lp1);
        R[i] = gl * lp1 + gh * (x - lp1);
    }
    s->lp[0] = flush(lp0);
    s->lp[1] = flush(lp1);
    s->gl0 = s->gl1;
    s->gh0 = s->gh1;
}

PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Tilt", "example", P_COUNT, kParams, sizeof(Inst), init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
