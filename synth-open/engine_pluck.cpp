// PLK ("Pluck") parameter metadata for the open PolySynth shim on MK66. PLK is our clean-room Karplus-Strong plucked string
// engine (synth-open/pluck/pluck.cpp). The parameters, ranges and defaults are this shim's own choice: no Polyend engine
// of this kind exists, so there is no manual to follow and no .patch file from another firmware to stay compatible with.
#include "open_engine.h"

#include "PolySynth/config.h"
#include "pluck/pluck.h"

namespace polysynth::open
{
namespace
{
using ot::audio::pluck::Param;

const char* const kExciter[] = { "Noise", "Click", "Saw", "Mallet" };

using F = Fmt;
using C = CurveKind;

// Names are 11 characters at most
const ParamDef kParams[Param::kParamCount] = {
    { "Exciter", 0, 0, 3, F::kEnum, C::kLinear, kExciter },
    { "Brightness", 0.6f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Damping", 0.35f, 0, 1, F::kPct, C::kLinear, nullptr },   // 0 % rings for 15 s, 100 % for 0.1 s
    { "Pick Pos", 0.35f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Stretch", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.15f, 0.02f, 2, F::kSec, C::kExp, nullptr },
    { "Tune", 0, -24, 24, F::kSemi, C::kLinear, nullptr },
    { "Fine", 0, -100, 100, F::kCents, C::kLinear, nullptr },
    { "Volume", 1, 0, 2, F::kPct, C::kLinear, nullptr },
};

// The editor picks the section icon from the name
const uint8_t kExc[] = { Param::kExciter, Param::kPickPos, Param::kTune, Param::kFine };
const uint8_t kString[] = { Param::kBrightness, Param::kDamping, Param::kStretch };
const uint8_t kVoice[] = { Param::kRelease, Param::kVolume };

const SectionDef kSections[] = {
    { "Oscillator", "Exciter", kExc, sizeof(kExc) },
    { "Filter", "String", kString, sizeof(kString) },
    { "Voice", "Voice", kVoice, sizeof(kVoice) },
};

const MacroDef kMacros[] = {
    { "Brightness", false, 1, { { Param::kBrightness, 0.5f } } },
    { "Damping", false, 1, { { Param::kDamping, 0.6f } } },
    { "Stretch", false, 1, { { Param::kStretch, 1.0f } } },
    { "Pick Pos", true, 1, { { Param::kPickPos, 0.5f } } },
    { "Release", false, 1, { { Param::kRelease, 0.5f } } },
    { "Fine", true, 1, { { Param::kFine, 0.5f } } },
};

} // namespace

const EngineDef kPlkDef = {
    "PLK",
    0x8A, // no Polyend id: ours, see engine_list.h
    kParams,
    Param::kParamCount,
    kSections,
    static_cast<int>(sizeof(kSections) / sizeof(kSections[0])),
    kMacros,
    static_cast<int>(sizeof(kMacros) / sizeof(kMacros[0])),
    Param::kVolume,
    kMaxVoicesPerInstrument,
    &ot::audio::pluck::engine,
    false,
};

} // namespace polysynth::open
