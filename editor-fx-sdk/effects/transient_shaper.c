// PTFX_NAME: Transient Shaper
// PTFX_AUTHOR: Emanuele Lapponi
// PTFX_FLAGS: EFFECT, STEREO
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[2] = {
    PTFX_PARAM_FLOAT("Attack", -12.0f, 12.0f, 0.0f, 0.5f, "dB"),
    PTFX_PARAM_FLOAT("Sustain", -12.0f, 12.0f, 0.0f, 0.5f, "dB")
};

// Sustain only applies once the signal rises above the noise floor: it is
// off below -66 dBFS and fully on from -48 dBFS, so silence and hiss are not
// boosted and the next hit does not start from a lifted gain.
#define SUSTAIN_GATE_LOW  0.0005f
#define SUSTAIN_GATE_HIGH 0.004f

// Steady material leaves the fast envelope a little above the slow one (about
// 0.14 for pure tones, 0.2 for noise, up to 0.3 for dense mixes). Treat
// anything below this as "no transient" so Sustain reaches its dB value on
// steady notes, while quick re-hits are still shaped.
#define TRANSIENT_DEADZONE 0.20f

static const PtfxHostApi* s_host;
static float s_attack_gain;
static float s_sustain_gain;
static float s_fast;
static float s_slow;
static float s_transient;
static float s_fast_rise_coeff;
static float s_fast_fall_coeff;
static float s_slow_rise_coeff;
static float s_slow_fall_coeff;
static float s_transient_rise_coeff;
static float s_transient_fall_coeff;

// One-pole smoothing coefficient for a time constant in seconds.
static float smoothing_coeff(float seconds, float sample_rate) {
    if (s_host && s_host->expf) {
        return 1.0f - s_host->expf(-1.0f / (seconds * sample_rate));
    }
    return 1.0f / (1.0f + seconds * sample_rate);
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    float sample_rate = (host && host->sample_rate) ? (float)host->sample_rate : 44100.0f;
    s_fast_rise_coeff = smoothing_coeff(0.001f, sample_rate);
    s_fast_fall_coeff = smoothing_coeff(0.020f, sample_rate);
    s_slow_rise_coeff = smoothing_coeff(0.015f, sample_rate);
    s_slow_fall_coeff = smoothing_coeff(0.100f, sample_rate);
    s_transient_rise_coeff = smoothing_coeff(0.0005f, sample_rate);
    s_transient_fall_coeff = smoothing_coeff(0.008f, sample_rate);
    s_attack_gain = 1.0f;
    s_sustain_gain = 1.0f;
    s_fast = 0.0f;
    s_slow = 0.0f;
    s_transient = 0.0f;
}

void plugin_set_param(uint8_t index, float value) {
    if (index > 1) return;
    if (value < -12.0f) value = -12.0f;
    if (value > 12.0f) value = 12.0f;
    float gain = (s_host && s_host->powf) ? s_host->powf(10.0f, value * 0.05f) : 1.0f;
    if (index == 0) s_attack_gain = gain;
    else s_sustain_gain = gain;
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    if (!in || !out || !in[0] || !out[0] || channels == 0) return 0;
    uint8_t nch = (channels > 2) ? 2 : channels;
    for (uint8_t ch = 0; ch < nch; ch++) {
        if (!in[ch] || !out[ch]) return 0;
    }

    // At neutral settings preserve every PCM value exactly, including -32768.
    if (s_attack_gain == 1.0f && s_sustain_gain == 1.0f) {
        for (uint8_t ch = 0; ch < nch; ch++) {
            for (uint32_t i = 0; i < samples; i++) out[ch][i] = in[ch][i];
        }
        return (int32_t)samples;
    }

    for (uint32_t i = 0; i < samples; i++) {
        // A shared peak detector keeps the stereo image stable.
        float level = 0.0f;
        for (uint8_t ch = 0; ch < nch; ch++) {
            float x = (float)in[ch][i] * (1.0f / 32768.0f);
            float magnitude = x < 0.0f ? -x : x;
            if (magnitude > level) level = magnitude;
        }
        // Peak followers rise quickly but release more slowly, so individual
        // waveform cycles (especially low bass notes) do not retrigger the
        // transient detector on every peak.
        float fast_coeff = level > s_fast ? s_fast_rise_coeff : s_fast_fall_coeff;
        float slow_coeff = level > s_slow ? s_slow_rise_coeff : s_slow_fall_coeff;
        s_fast += fast_coeff * (level - s_fast);
        s_slow += slow_coeff * (level - s_slow);

        // The relative excess of the fast envelope identifies rising attacks.
        // The floor avoids amplifying detector noise near digital silence.
        float transient = ((s_fast - s_slow) / (s_fast + 0.0001f) - TRANSIENT_DEADZONE) *
                          (1.0f / (1.0f - TRANSIENT_DEADZONE));
        if (transient < 0.0f) transient = 0.0f;
        if (transient > 1.0f) transient = 1.0f;
        // Smooth the transient measure itself (fast rise, slower fall) so the
        // gain reacts to a hit within about a millisecond whether Attack boosts
        // or cuts, and the gain follows without direction-dependent lag.
        float t_coeff = transient > s_transient ? s_transient_rise_coeff : s_transient_fall_coeff;
        s_transient += t_coeff * (transient - s_transient);
        float active = (s_slow - SUSTAIN_GATE_LOW) * (1.0f / (SUSTAIN_GATE_HIGH - SUSTAIN_GATE_LOW));
        if (active < 0.0f) active = 0.0f;
        if (active > 1.0f) active = 1.0f;
        float sustain = 1.0f + active * (s_sustain_gain - 1.0f);
        float gain = sustain + s_transient * (s_attack_gain - sustain);

        for (uint8_t ch = 0; ch < nch; ch++) {
            out[ch][i] = (int16_t)SATURATE_16BIT((int32_t)(in[ch][i] * gain));
        }
    }
    return (int32_t)samples;
}
