// WTFM (FM) parameter metadata for the open PolySynth shim on MK66.
// Indices and ranges follow OpenTracker's WTFM engine (synth-open/ot/audio/engines/wtfm.h, Apache-2.0),
// labels and sections follow the Tracker Mini manual naming as used by OpenTracker.
#include "open_engine.h"

#include "audio/engines/wtfm.h"
#include "PolySynth/config.h"

namespace polysynth::open
{
namespace
{
using ot::audio::wtfm::Param;

const char* const kFilter[] = { "LP MG 24dB",   "LP OB 24dB",   "LP OB 12dB",   "LP SVF 24dB",   "LP SVF 12dB",
                                "HP OB 24dB",   "HP OB 12dB",   "HP SVF 24dB",  "HP SVF 12dB",   "BP OB 24dB",
                                "BP OB 12dB",   "BP SVF 24dB",  "BP SVF 12dB",  "Notch SVF 24dB", "Notch SVF 12dB" };
const char* const kChar[] = { "Smoother", "Sharper", "Wilder", "Add 1", "Add 2", "Add 3", "Add 5", "Add 7", "Add 11" };
const char* const kModSrc[] = { "Amp Env", "Env 1 (Fltr)", "Env 2", "LFO 1", "LFO 2" };
const char* const kModDest[] = { "Osc1 Pitch",   "Osc2 Pitch",   "Op1 Shape",    "Op2 Shape",       "Op1 Feedback",
                                 "Op2 Feedback", "Op21 Feedback", "FM",          "Filter Cutoff",   "Amp Env Sustain",
                                 "Flt Env Sustain", "Wave Env Sustain", "Volume", "Flt Env Decay" };

using F = Fmt;
using C = CurveKind;

#define MODROW(k) \
    { "Src " #k, 0, 0, 4, F::kEnum, C::kLinear, kModSrc }, { "Dest " #k, 0, 0, 13, F::kEnum, C::kLinear, kModDest }, \
      { "Amt " #k, 0, -4, 4, F::kPctMax, C::kLinear, nullptr }

const ParamDef kParams[Param::kParamCount] = {
    { "Ratio 1", 3, 0, 47, F::kRatio, C::kLinear, nullptr },
    { "Ratio 2", 3, 0, 47, F::kRatio, C::kLinear, nullptr },
    { "Finetune 1", 0, -100, 100, F::kCents, C::kLinear, nullptr },
    { "Finetune 2", 0, -100, 100, F::kCents, C::kLinear, nullptr },
    { "Shape 1", 0, 0, 1, F::kVal100, C::kLinear, nullptr },
    { "Shape 2", 0, 0, 1, F::kVal100, C::kLinear, nullptr },
    { "", 0, 0, 0, F::kVal100, C::kLinear, nullptr }, // 6, 7: unused (no range)
    { "", 0, 0, 0, F::kVal100, C::kLinear, nullptr },
    { "Oversampling", 0, 0, 1, F::kOnOff, C::kLinear, nullptr },
    { "Character 1", 0, 0, 8, F::kEnum, C::kLinear, kChar },
    { "Character 2", 0, 0, 8, F::kEnum, C::kLinear, kChar },
    { "FM", 0, 0, 10, F::kPct, C::kLinear, nullptr },
    { "Feedback 1", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Feedback 2", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Feedback 2>1", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Type", 0, 0, 14, F::kEnum, C::kLinear, kFilter },
    { "Cutoff", 400, 20, 20000, F::kHz, C::kExp, nullptr },
    { "Resonance", 0.5f, 0, 1.5f, F::kPctMax, C::kLinear, nullptr },
    { "Env Amt", 0.14f, -1, 1, F::kPct, C::kLinear, nullptr },
    { "Note Track", 1, -2, 2, F::kPct, C::kLinear, nullptr },
    { "Attack", 0, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Decay", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Sustain", 1, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.3f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Attack", 0, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Decay", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Sustain", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Attack", 0, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Decay", 0.5f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Sustain", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.3f, 0, 10, F::kSec, C::kSteep, nullptr },
    { "Ratio", 0, 0, 100, F::kLfoHz, C::kExp, nullptr },
    { "Waveform", 1, 0, 5, F::kEnum, C::kLinear, kWaveNames },
    { "Retrigger", 2, 0, 2, F::kEnum, C::kLinear, kRetrigNames },
    { "Sync", 0, 0, 1, F::kOnOff, C::kLinear, nullptr },
    { "Ratio", 0, 0, 100, F::kLfoHz, C::kExp, nullptr },
    { "Waveform", 1, 0, 5, F::kEnum, C::kLinear, kWaveNames },
    { "Retrigger", 2, 0, 2, F::kEnum, C::kLinear, kRetrigNames },
    { "Sync", 0, 0, 1, F::kOnOff, C::kLinear, nullptr },
    { "Glide Mode", 0, 0, 3, F::kEnum, C::kLinear, kGlideNames },
    { "Glide Time", 0, 0, 3, F::kSec, C::kPower, nullptr },
    { "Volume", 1, 0, 2, F::kPct, C::kLinear, nullptr },
    MODROW(1), MODROW(2), MODROW(3), MODROW(4), MODROW(5), MODROW(6),
    { "Reset 1", 0, 0, 1, F::kOnOff, C::kLinear, nullptr },
    { "Reset 2", 0, 0, 1, F::kOnOff, C::kLinear, nullptr },
};
#undef MODROW

const uint8_t kOsc[] = { Param::kFm,     Param::kRatio1, Param::kRatio2, Param::kShape1, Param::kShape2,
                         Param::kChar1,  Param::kChar2,  Param::kFb1,    Param::kFb2,    Param::kFb21,
                         Param::kFine1,  Param::kFine2,  Param::kReset1, Param::kReset2, Param::kOversample };
const uint8_t kFlt[] = { Param::kCutoff, Param::kResonance, Param::kFilterType, Param::kFilterEnvAmt, Param::kNoteTrack };
const uint8_t kAmp[] = { Param::kAmpA, Param::kAmpD, Param::kAmpS, Param::kAmpR };
const uint8_t kEnv1[] = { Param::kEnv1A, Param::kEnv1D, Param::kEnv1S, Param::kEnv1R };
const uint8_t kEnv2[] = { Param::kEnv2A, Param::kEnv2D, Param::kEnv2S, Param::kEnv2R };
const uint8_t kLfo1[] = { Param::kLfo1Ratio, Param::kLfo1Wave, Param::kLfo1Retrig, Param::kLfo1Sync };
const uint8_t kLfo2[] = { Param::kLfo2Ratio, Param::kLfo2Wave, Param::kLfo2Retrig, Param::kLfo2Sync };
const uint8_t kMod[] = { 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60 };
const uint8_t kVoice[] = { Param::kGlideMode, Param::kGlideTime, Param::kVolume };

const SectionDef kSections[] = {
    { "Oscillator", "Osc", kOsc, sizeof(kOsc) },
    { "Filter", "Filter", kFlt, sizeof(kFlt) },
    { "Amp Envelope", "Amp Env", kAmp, sizeof(kAmp) },
    { "Envelope 1", "Env 1", kEnv1, sizeof(kEnv1) },
    { "Envelope 2", "Env 2", kEnv2, sizeof(kEnv2) },
    { "LFO 1", "LFO 1", kLfo1, sizeof(kLfo1) },
    { "LFO 2", "LFO 2", kLfo2, sizeof(kLfo2) },
    { "Modulation", "Mod", kMod, sizeof(kMod) },
    { "Voice", "Voice", kVoice, sizeof(kVoice) },
};

// Default macros (this shim's own choice)
const MacroDef kMacros[] = {
    { "Cutoff", false, 1, { { Param::kCutoff, 0.4f } } },
    { "Resonance", false, 1, { { Param::kResonance, 1.0f } } },
    { "Env Amount", true, 1, { { Param::kFilterEnvAmt, 1.0f } } },
    { "Decay / Release", false, 2, { { Param::kAmpD, 0.3f }, { Param::kAmpR, 0.3f } } },
    { "FM", false, 1, { { Param::kFm, 0.1f } } },
    { "Feedback 1", false, 1, { { Param::kFb1, 0.5f } } },
};

} // namespace

const EngineDef kWtfmDef = {
    "WTFM",
    static_cast<int>(InstrumentTypeId::kFMSynth),
    kParams,
    Param::kParamCount,
    kSections,
    static_cast<int>(sizeof(kSections) / sizeof(kSections[0])),
    kMacros,
    static_cast<int>(sizeof(kMacros) / sizeof(kMacros[0])),
    Param::kVolume,
    kMaxVoicesPerInstrument,
    &ot::audio::wtfm::engine,
    false,
    true, // monoOut: the engine writes L only
};

} // namespace polysynth::open
