// Small helpers shared by the ot_*.c effects (ports of OpenTracker 3.x mix effects).
//
// Header only, no libc and no libm: everything here compiles to inline code
// under the ptfx_builder.py flags (-nostdlib -fno-builtin). Only
// __builtin_fabsf is used, which is a single FPU instruction.
#ifndef OT_COMMON_H_
#define OT_COMMON_H_

#include "tracker_plugin.h"

// Host render harness (tools/ptfx_host_render.c) defines PTFX_HOST_TEST and
// provides the counter, so a NaN or inf inside a plugin cannot hide behind
// the clamp below. The device build never sees it.
#ifdef PTFX_HOST_TEST
extern unsigned g_ptfx_nonfinite;
#endif

static inline float ot_absf(float x) { return __builtin_fabsf(x); }

// int16 PCM <-> float -1..1
static inline float ot_s16f(int16_t s) { return (float)s * (1.0f / 32768.0f); }

static inline int16_t ot_f16s(float v) {
#ifdef PTFX_HOST_TEST
    if (!(v == v) || v > 1.0e30f || v < -1.0e30f) g_ptfx_nonfinite++;
#endif
    if (!(v == v)) return 0;   // NaN
    float s = v * 32768.0f;
    if (s > 32767.0f) s = 32767.0f;
    else if (s < -32768.0f) s = -32768.0f;
    return (int16_t)(s + (s >= 0.0f ? 0.5f : -0.5f));
}

// Flush tiny values to zero so filter states do not decay into denormals.
static inline float ot_flush(float x) { return ot_absf(x) < 1.0e-20f ? 0.0f : x; }

// Clamp to [lo, hi].
static inline float ot_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// The MK66 sample engine is mono. If a host ever hands over more channels,
// channel 0 is processed and the others are passed through untouched.
static inline void ot_pass_other_channels(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    for (uint8_t ch = 1; ch < channels; ch++) {
        if (!out[ch]) continue;
        for (uint32_t i = 0; i < samples; i++) out[ch][i] = in && in[ch] ? in[ch][i] : 0;
    }
}

void plugin_set_param(uint8_t index, float value);

// Apply every parameter's default value (called from plugin_init, so the
// plugin does not depend on the host sending the defaults first).
#define OT_APPLY_DEFAULTS(count)                                              \
    do {                                                                      \
        for (uint8_t ot_i_ = 0; ot_i_ < (count); ot_i_++)                     \
            plugin_set_param(ot_i_, PLUGIN_PARAMS[ot_i_].default_val);        \
    } while (0)

#endif /* OT_COMMON_H_ */
