// ACD (SimpleAnalog) parameter metadata for the open PolySynth shim on MK66.
// Indices and ranges follow OpenTracker's ACD engine (synth-open/ot/audio/engines/acd.h, Apache-2.0),
// labels and sections follow the Tracker Mini manual naming as used by OpenTracker.
#include "open_engine.h"

#include "audio/engines/acd.h"
#include "PolySynth/config.h"
#include "plug/plug_chord.h"

namespace polysynth::open
{
const char* const kWaveNames[6] = { "Triangle", "Sine", "Ramp Up", "Ramp Down", "Square", "Random S&H" };
const char* const kRetrigNames[3] = { "Free Running", "One Shot", "Note" };
const char* const kGlideNames[4] = { "Always", "Overlap", "Legato", "Legato Overlap" };

namespace
{
using ot::audio::acd::Param;

const char* const kSubMode[] = { "-1 Oct Square", "-1 Oct Pulse",   "-1 Oct Triangle",
                                 "-2 Oct Square", "-2 Oct Pulse",   "-2 Oct Triangle" };
const char* const kFilter[] = { "LP SVF 12dB", "LP SVF 24dB", "LP RD3" };
const char* const kChordMode[] = { "Poly", "Para" };
const char* const* const kWave = kWaveNames;
const char* const* const kRetrig = kRetrigNames;
const char* const* const kGlide = kGlideNames;

using F = Fmt;
using C = CurveKind;

const ParamDef kParams[Param::kParamCount] = {
    { "Saw Mix", 1, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Square Mix", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Sub Mix", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Noise Mix", 0, -1, 1, F::kPct, C::kLinear, nullptr },
    { "PW", 0.5f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Sub Mode", 0, 0, 5, F::kEnum, C::kLinear, kSubMode },
    { "PW LFO", 0, -1, 1, F::kPct, C::kLinear, nullptr },
    { "PW Env", 0, -1, 1, F::kPct, C::kLinear, nullptr },
    { "Pitch LFO", 0, -3, 3, F::kPctMax, C::kLinear, nullptr },
    { "Cutoff", 400, 20, 20000, F::kHz, C::kExp, nullptr },
    { "Resonance", 0.5f, 0, 1.5f, F::kPctMax, C::kLinear, nullptr },
    { "Type", 1, 0, 2, F::kEnum, C::kLinear, kFilter },
    { "Note Track", 1, -2, 2, F::kPct, C::kLinear, nullptr },
    { "LFO Amt", 0, -1, 1, F::kPct, C::kLinear, nullptr },
    { "Env Amt", 0.14f, -1, 1, F::kPct, C::kLinear, nullptr },
    { "Attack", 0, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Decay", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Sustain", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Attack", 0, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Decay", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Sustain", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Frequency", 1, 0.1f, 100, F::kLfoHz, C::kExp, nullptr },
    { "Waveform", 1, 0, 5, F::kEnum, C::kLinear, kWave },
    { "Retrigger", 2, 0, 2, F::kEnum, C::kLinear, kRetrig },
    { "Glide Mode", 0, 0, 3, F::kEnum, C::kLinear, kGlide },
    { "Glide Time", 0, 0, 3, F::kSec, C::kPower, nullptr },
    { "Volume", 1, 0, 2, F::kPct, C::kLinear, nullptr },
    // paraphony (tracker-ff): Para sounds the Chord in one voice, a MIDI Chord step plays its own chord there (PLUG_EXT_CHORD)
    { "Chord Mode", 0, 0, 1, F::kEnum, C::kLinear, kChordMode },
    { "Chord", 0, 0, PLUG_CHORD_COUNT - 1, F::kEnum, C::kLinear, plugchord::kNames },
    // Para strum: the chord notes start one after another, 2 ms a unit, + up, - down. A Strum FX step sets it per note-on
    { "Strum", 0, -63, 63, F::kInt, C::kLinear, nullptr },
};

const uint8_t kOsc[] = { Param::kSawMix, Param::kSquareMix, Param::kSubMix,   Param::kNoiseMix, Param::kPw,
                         Param::kSubMode, Param::kPitchLfo,  Param::kPwEnv,    Param::kPwLfo };
const uint8_t kFlt[] = { Param::kCutoff, Param::kResonance, Param::kFilterType,
                         Param::kFilterEnv, Param::kNoteTrack, Param::kFilterLfo };
const uint8_t kAmp[] = { Param::kAmpA, Param::kAmpD, Param::kAmpS, Param::kAmpR };
const uint8_t kEnv[] = { Param::kModA, Param::kModD, Param::kModS, Param::kModR };
const uint8_t kLfo[] = { Param::kLfoHz, Param::kLfoWave, Param::kLfoRetrig };
const uint8_t kVoice[] = { Param::kGlideMode, Param::kGlideTime, Param::kVolume, Param::kChordMode, Param::kChord,
                           Param::kStrum };

const SectionDef kSections[] = {
    { "Oscillator", "Osc", kOsc, sizeof(kOsc) },
    { "Filter", "Filter", kFlt, sizeof(kFlt) },
    { "Amp Envelope", "Amp Env", kAmp, sizeof(kAmp) },
    { "Envelope", "Env", kEnv, sizeof(kEnv) },
    { "LFO", "LFO", kLfo, sizeof(kLfo) },
    { "Voice", "Voice", kVoice, sizeof(kVoice) },
};

// Default macros (this shim's own choice, not the factory ones)
const MacroDef kMacros[] = {
    { "Cutoff", false, 1, { { Param::kCutoff, 0.4f } } },
    { "Resonance", false, 1, { { Param::kResonance, 1.0f } } },
    { "Env Amount", true, 1, { { Param::kFilterEnv, 1.0f } } },
    { "Decay / Release", false, 2, { { Param::kAmpD, 0.3f }, { Param::kAmpR, 0.3f } } },
    { "Pulse Width", true, 1, { { Param::kPw, 0.5f } } },
    { "Pitch LFO", false, 1, { { Param::kPitchLfo, 0.3f } } },
};

} // namespace

const EngineDef kAcdDef = {
    "ACD",
    static_cast<int>(InstrumentTypeId::kSimpleAnalogSynth),
    kParams,
    Param::kParamCount,
    kSections,
    static_cast<int>(sizeof(kSections) / sizeof(kSections[0])),
    kMacros,
    static_cast<int>(sizeof(kMacros) / sizeof(kMacros[0])),
    Param::kVolume,
    kMaxVoicesPerInstrument,
    &ot::audio::synthAcdEngine,
    false,
};

} // namespace polysynth::open
