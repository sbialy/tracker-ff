// SAWF: the smallest complete synth engine plugin (sawf.plg). One band limited saw or square oscillator through a resonant
// low pass with a short attack and an adjustable release. Worked example of docs/plugin_sdk.md, section 9.
// Copyright (c) 2026 sbialy (tracker-ff). MIT licence, see synth-open/plug/LICENSE. Original code, no third party source.
//
// A synth plugin is three things in one file:
//   1. an ISynthEngine (audio/synth_engine.h): noteOn, noteOff and render, a stateless singleton, all voice state in SynthVoice::state
//   2. an EngineDef (open_engine.h): the parameter, section and macro tables the editor and the patch files are built from
//   3. the PLUG_SYNTH_PLUGIN line (synth_plug.h): the plugin entry, the numbers of the engine and the PLUG_EXT_SYNTH extension
//
// No static constructors (build_plug.py refuses them), no allocation, no globals that change: the engine object only has a
// vtable. sinf and exp2f are calls to the host's libm (plug_rt.c).
#define PLUG_SYNTH_AUTHOR "example (MIT)"   // the descriptor's author line, before the include
#include "synth_plug.h"

#include <math.h>

namespace polysynth::open
{
extern const EngineDef kSawDef;
}

namespace
{
using namespace ot::audio;
using polysynth::open::CurveKind;
using polysynth::open::EngineDef;
using polysynth::open::Fmt;
using polysynth::open::MacroDef;
using polysynth::open::ParamDef;
using polysynth::open::SectionDef;

// The engine's numbers: FNV-1a of "org.example.synth.sawf" (what build_plug.py --id hashes into the uid). stored is the
// number in a project, patch is field 1 of a .patch file, type is the shim's instrument key. They must be unique among all
// engines. The same three numbers go to build_plug.py --synth.
constexpr int32_t kNumber = 0x64880b9c;

enum Param : int { kWave, kCutoff, kReso, kTune, kRelease, kVolume, kParamCount };

// Everything one voice remembers. The framework hands over kSynthVoiceStateBytes (128) zeroed bytes per voice.
struct VoiceState
{
    float phase, low, band;   // oscillator phase 0..1, filter state
    float env;                // amplitude 0..1
    float pitch;              // the note in semitones (MIDI numbering, 69 = 440 Hz)
    uint8_t released;
};
static_assert(sizeof(VoiceState) <= kSynthVoiceStateBytes, "voice state too large");

constexpr float kAttack = 0.002f;   // seconds, to avoid a click at note on

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Polynomial band limited step: t is the phase, dt the phase increment per sample
inline float blep(float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

class SawEngine final : public ISynthEngine
{
  public:
    int32_t id() const override { return kNumber; }
    const char* name() const override { return "SAWF"; }

    void noteOn(SynthVoice& v, const SynthParams&, const SynthNote& n) override
    {
        VoiceState& s = v.as<VoiceState>();
        if (!n.reused) __builtin_memset(&s, 0, sizeof(s));   // a fresh voice is zeroed already, a reused one keeps its state
        s.pitch = n.pitch;
        s.released = 0;
        v.level = 1.0f;
    }

    void noteOff(SynthVoice& v, const SynthParams&) override { v.as<VoiceState>().released = 1; }

    // Adds kBlock frames into L and R. g0 + i * gInc is the framework's gain (Volume, velocity, fades): multiply the voice by it,
    // never apply the Volume parameter yourself. Returns false once the voice is silent for good.
    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override
    {
        VoiceState& s = v.as<VoiceState>();
        const float* e = p.end;   // the parameter values of this block, macros and automation already applied
        const float hz = 440.0f * exp2f((s.pitch + e[kTune] + v.bend - 69.0f) * (1.0f / 12.0f));
        const float dt = clampf(hz / kSampleRate, 0.0001f, 0.45f);
        float cut = e[kCutoff];
        if (v.cutoffOct != 0.0f) cut *= exp2f(v.cutoffOct);   // the track's Performance filter offset, in octaves
        const float fc = clampf(cut, 40.0f, 7000.0f);         // Chamberlin filter, stable up to fs / 6
        const float f = 2.0f * sinf(3.14159265f * fc / kSampleRate);
        const float q = 1.0f - 0.9f * clampf(e[kReso], 0.0f, 1.0f);
        const float up = 1.0f / (kAttack * kSampleRate);
        const float down = 1.0f / (clampf(e[kRelease], 0.005f, 2.0f) * kSampleRate);
        const bool square = e[kWave] > 0.5f;
        float phase = s.phase, low = s.low, band = s.band, env = s.env, g = g0;
        for (uint32_t i = 0; i < kBlock; i++)
        {
            float x;
            if (square)
            {
                const float t2 = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
                x = (phase < 0.5f ? 1.0f : -1.0f) + blep(phase, dt) - blep(t2, dt);
            }
            else
            {
                x = 2.0f * phase - 1.0f - blep(phase, dt);
            }
            phase += dt;
            if (phase >= 1.0f) phase -= 1.0f;
            low += f * band;
            const float high = x - low - q * band;
            band += f * high;
            if (s.released)
                env = env > down ? env - down : 0.0f;
            else
                env = env + up < 1.0f ? env + up : 1.0f;
            const float o = low * env * g;
            L[i] += o;
            R[i] += o;
            g += gInc;
        }
        s.phase = phase;
        s.low = low;
        s.band = band;
        s.env = env;
        v.level = env;   // the amplitude level 0..1, the pool steals the quietest voice first
        return !(s.released && env <= 0.0f) && !v.fading;
    }
};

SawEngine gSaw;

// ---- metadata. Names of 11 characters or less show in full in the editor.
const char* const kWaves[] = {"Saw", "Square"};

const ParamDef kParams[kParamCount] = {
    //  name       default  min     max     format         curve                enum names
    {"Wave", 0.0f, 0.0f, 1.0f, Fmt::kEnum, CurveKind::kLinear, kWaves},
    {"Cutoff", 3000.0f, 40.0f, 7000.0f, Fmt::kHz, CurveKind::kExp, nullptr},
    {"Reso", 0.2f, 0.0f, 1.0f, Fmt::kPct, CurveKind::kLinear, nullptr},
    {"Tune", 0.0f, -24.0f, 24.0f, Fmt::kSemi, CurveKind::kLinear, nullptr},
    {"Release", 0.25f, 0.01f, 2.0f, Fmt::kSec, CurveKind::kExp, nullptr},
    {"Volume", 1.0f, 0.0f, 2.0f, Fmt::kPct, CurveKind::kLinear, nullptr},
};

// The editor picks the section icon from the name ("Oscillator", "Filter", "Amplifier", "Voice")
const uint8_t kOscIds[] = {kWave, kTune};
const uint8_t kFilterIds[] = {kCutoff, kReso};
const uint8_t kAmpIds[] = {kRelease};
const uint8_t kVoiceIds[] = {kVolume};
const SectionDef kSections[] = {
    {"Oscillator", "Osc", kOscIds, sizeof(kOscIds)},
    {"Filter", "Filter", kFilterIds, sizeof(kFilterIds)},
    {"Amplifier", "Amp", kAmpIds, sizeof(kAmpIds)},
    {"Voice", "Voice", kVoiceIds, sizeof(kVoiceIds)},
};

// Default macros M1..M3: a name, bipolar or not, one or two targets (parameter, amount -2..+2 of the range)
const MacroDef kMacros[] = {
    {"Cutoff", false, 1, {{kCutoff, 0.5f}}},
    {"Reso", false, 1, {{kReso, 0.5f}}},
    {"Release", false, 1, {{kRelease, 0.5f}}},
};

ISynthEngine* sawEngine() { return &gSaw; }
}   // namespace

namespace polysynth::open
{
const EngineDef kSawDef = {
    "SAWF",
    kNumber,
    kParams,
    kParamCount,
    kSections,
    int(sizeof(kSections) / sizeof(kSections[0])),
    kMacros,
    int(sizeof(kMacros) / sizeof(kMacros[0])),
    kVolume,   // the framework applies this parameter (and Macro 7) as the gain ramp passed to render
    4,         // voices per instrument, 1..4
    &sawEngine,
    false,     // no slot effect
};
}   // namespace polysynth::open

PLUG_SYNTH_PLUGIN("SAWF", polysynth::open::kSawDef, kNumber, kNumber)
