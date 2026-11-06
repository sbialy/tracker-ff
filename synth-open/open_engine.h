// Open PolySynth shim for MK66 - engine description (parameter metadata + hosted engine).
// Written only from the open headers in synth/include/PolySynth and OpenTracker's Apache-2.0
// ISynthEngine interface (synth-open/ot). Never derived from the closed libPolySynth/libPolyDSP.
#pragma once

#include "audio/synth_engine.h"

#include <cstdint>

// Engines with a slot effect (ISynthEngine::slotPost) need a private bus per slot in the render path of the shim. That
// costs flash (about 0.4 KB), so it is only compiled when such an engine is picked (JU6, engines.mk) or can be installed
// (FEATURE_PLUG_HOST: the JU6 plugin has it).
#if defined(SYNTH_ENGINE_JU6) || defined(FEATURE_PLUG_HOST)
#define OPEN_SLOT_FX 1
#else
#define OPEN_SLOT_FX 0
#endif

namespace polysynth
{
class InstrumentTypeRegistry;
}

namespace polysynth::open
{

enum class Fmt : uint8_t
{
    kPct,    // value * 100 %
    kPctMax, // value / max(|min|,|max|) * 100 %
    kHz,
    kSec,
    kLfoHz,
    kEnum,
    kVal100,   // value * 100 as a plain number
    kOnOff,
    kSemi,     // "n st"
    kCents,    // "n c"
    kInt,
    kRatio,    // WTFM ratio index -> 0.25 * (i + 1)
    kMs10,     // value / 10 ms
    kDb,
    kPan,      // L50 / 0 / R50
    kUnit,     // value as 0.00 .. 1.00 (PHZ X / Y). Always 15: a PHZ plugin is built without PERC and the host with it, so the
               // number must not depend on which engines a build has (PLUG_FMT_UNIT)
#ifdef SYNTH_ENGINE_PERC
    kNote,     // PERC pitch note name, only compiled with PERC (built in only, no plugin may use it)
    kBodyWave, // PERC body wave: enumNames = 16 families, 11 positions each, only compiled with PERC (built in only)
#endif
};

enum class CurveKind : uint8_t
{
    kLinear,
    kExp,   // geometric when min > 0, else quadratic
    kSteep, // cubic
    kPower, // quadratic
};

struct ParamDef
{
    const char* name;
    float def;
    float min;
    float max;
    Fmt fmt;
    CurveKind curve;
    const char* const* enumNames; // kEnum only, max + 1 entries
};

struct SectionDef
{
    const char* name;      // the editor picks the icon from this ("Oscillator", "Filter", ...)
    const char* shortName;
    const uint8_t* ids;
    uint8_t count;
};

struct MacroDef
{
    const char* name;
    bool bipolar;
    uint8_t count;
    struct Target
    {
        uint8_t param;
        float amount;
    } targets[2];
};

// Voice records per synth instrument (the Voices parameter tops out here, 4 as Tracker+ stock feel).
// A slot's engine caps it lower with EngineDef::maxVoices (PERC and TTYPE: 1). The shared pool (open_load.h)
// still limits what sounds at once.
constexpr int kMaxVoicesPerInstrument = 4;

struct EngineDef
{
    const char* name;
    int typeId; // polysynth::InstrumentTypeId
    const ParamDef* params;
    int paramCount;
    const SectionDef* sections;
    int sectionCount;
    const MacroDef* macros; // user macros, may be fewer than 6
    int macroCount;
    int volumeParam;        // framework applies it (and Macro 7) as voice gain
    int maxVoices;          // voice cap per instrument
    ot::audio::ISynthEngine* (*engine)();
    bool slotFx;            // the engine has a slot effect (ISynthEngine::slotPost): the shim renders its voices
                            // into a private bus, runs the effect on it and adds the result to the mix
    ot::audio::ISynthEngine* inst = nullptr;   // a plugin engine (plug_synth.cpp: the PLUG_EXT_SYNTH adapter), else engine()
};

// The engine of a definition. nullptr for a missing engine's placeholder (missingDef): the part stays silent.
inline ot::audio::ISynthEngine* engineOf(const EngineDef* d)
{
    return !d ? nullptr : (d->inst ? d->inst : (d->engine ? d->engine() : nullptr));
}

// The engine list at run time, in the order of the model list on screen: the engines built in (engine_list.h) and the
// installed plugin engines (FEATURE_PLUG_HOST, engineListAdd before the synth starts), merged by stored number, so a
// plugin of an engine that used to be built in (JU6 7, FM4 9) sits where it always sat.
struct EngineRef
{
    const EngineDef* def;
    int32_t stored;        // the number in a project (strMtValues synthPart[].synthType)
    int32_t patchEngine;   // field 1 of a .patch file
    int8_t builtin;        // position in engine_list.h (its icons), -1 for a plugin engine (shown by its name)
};
bool engineListAdd(const EngineDef* def, int stored, int patchEngine);   // false: full, or a number already taken
int engineCount();
const EngineRef& engineAt(int ui);
int engineUiForStored(int stored);   // -1: not in the list
// A known engine (engine_list.h numbers 7 to 17) that is neither built in nor installed: a placeholder definition with its
// name and type, no parameters and no engine. A part on it stays silent and keeps its project values and patch files
// untouched ("missing" state), installing the plugin brings it back. nullptr for any other number.
const EngineDef* missingDef(int stored);
int typeForStored(int stored);       // the list entry's or the placeholder's type, -1 when neither

void registerEngines(InstrumentTypeRegistry& registry);   // the list and the missing placeholders (registry.cpp)

// nullptr when the type has no open engine in this build (a missing engine's placeholder counts as one)
const EngineDef* engineDefForType(int typeId);
// type id of the first engine of this build (the fallback for a type this build does not have)
int firstEngineTypeId();

// One EngineDef per engine picked at build time (engine_list.h)
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, ...) extern const EngineDef DEF;
#include "engine_list.h"
#undef SYNTH_ENGINE

// Enum name tables shared by several engines (defined in engine_acd.cpp)
extern const char* const kWaveNames[6];
extern const char* const kRetrigNames[3];
extern const char* const kGlideNames[4];

} // namespace polysynth::open
