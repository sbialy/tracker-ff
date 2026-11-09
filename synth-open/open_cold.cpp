// Cold half of the open PolySynth shim, built at -Os (SYNTH_OPT_COLD): value text, edit curves,
// the registry factories and metadata, OpenPatch's GUI API and the patch store. Nothing here runs
// per audio block (OpenPatch::effective and the instrument are in open_synth.cpp at -O2).
//
// Written only from the open headers in synth/include/PolySynth and OpenTracker's Apache-2.0
// engine interface (synth-open/ot). Nothing here is derived from libPolySynth/libPolyDSP.
#include "open_patch.h"
#include "devhooks.h"
#include <malloc.h>
#include "open_load.h"
#include "open_fileops.h"

#include "PolySynth/PatchStore.h"
#include "PolySynth/sdk/InstrumentType.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

// The patch is applied while the render path cannot read it (macros are vectors). The host test
// build has no audio interrupt.
#ifdef OPEN_HOST_TEST
#define OPEN_LOCK_AUDIO()   ((void)0)
#define OPEN_UNLOCK_AUDIO() ((void)0)
#define OPEN_EXTERNAL_RAM
#else
// The SDRAM, as __NOINIT(EXTERNAL_RAM). It only works once main() has run hardware init (platforms/mk66/source/hardware.cpp,
// externalRAM.init()), after every static constructor (teensy_core/mk20dx128.c, __libc_init_array()). So a global placed here must be
// trivially default constructible (static_assert next to each one): a constructor would write the SDRAM at static init, fault on the bus and
// leave the device on the bootloader's "100" (f563f27). synth-open/tools/check_static_init.py checks the linked image for it.
#define OPEN_EXTERNAL_RAM __attribute__((section(".noinit.$EXTERNAL_RAM")))
#include "interruptsControl.h"
#define OPEN_LOCK_AUDIO()   DISABLE_AUDIO_INTERRUPTS()
#define OPEN_UNLOCK_AUDIO() ENABLE_AUDIO_INTERRUPTS()
#endif

namespace polysynth
{
using open::EngineDef;
using open::EngineRef;
using open::ParamDef;
using open::kVelocityMacro;

namespace
{

std::string formatValue(const ParamDef& d, float v)
{
    char buf[24];
    switch (d.fmt)
    {
        case open::Fmt::kPct: snprintf(buf, sizeof(buf), "%d%%", int(std::lround(v * 100.0f))); break;
        case open::Fmt::kPctMax:
        {
            const float m = std::max(std::fabs(d.min), std::fabs(d.max));
            snprintf(buf, sizeof(buf), "%d%%", int(std::lround(m > 0 ? v / m * 100.0f : 0.0f)));
            break;
        }
        case open::Fmt::kHz:
            if (v >= 1000.0f)
            {
                snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%d Hz", int(std::lround(v)));
            }
            break;
        case open::Fmt::kSec:
            if (v < 1.0f)
            {
                snprintf(buf, sizeof(buf), "%d ms", int(std::lround(v * 1000.0f)));
            }
            else
            {
                snprintf(buf, sizeof(buf), "%.2f s", v);
            }
            break;
        case open::Fmt::kLfoHz: snprintf(buf, sizeof(buf), "%.2f Hz", v); break;
        case open::Fmt::kEnum:
        {
            const int idx = std::clamp(int(std::lround(v)), 0, int(d.max));
            return d.enumNames[idx];
        }
        case open::Fmt::kVal100: snprintf(buf, sizeof(buf), "%d", int(std::lround(v * 100.0f))); break;
        case open::Fmt::kOnOff: return v >= 0.5f ? "On" : "Off";
        case open::Fmt::kSemi: snprintf(buf, sizeof(buf), "%d st", int(std::lround(v))); break;
        case open::Fmt::kCents: snprintf(buf, sizeof(buf), "%d c", int(std::lround(v))); break;
        case open::Fmt::kInt: snprintf(buf, sizeof(buf), "%d", int(std::lround(v))); break;
        case open::Fmt::kRatio:
            snprintf(buf, sizeof(buf), "%.2f", 0.25f * float(std::clamp(int(std::lround(v)), 0, 47) + 1));
            break;
        case open::Fmt::kMs10: snprintf(buf, sizeof(buf), "%.2f ms", v * 0.1f); break;
        case open::Fmt::kDb: snprintf(buf, sizeof(buf), "%.1f dB", v); break;
#ifdef SYNTH_ENGINE_PERC
        case open::Fmt::kNote:
        {
            static const char* const kName[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int n = std::clamp(int(std::lround(v)), 0, 127);
            snprintf(buf, sizeof(buf), "%s%d", kName[n % 12], n / 12 - 1);
            break;
        }
        case open::Fmt::kBodyWave:
        {
            const int i = std::clamp(int(std::lround(v)), 0, 175);
            snprintf(buf, sizeof(buf), "%s %d%%", d.enumNames[i / 11], (i % 11) * 10);
            break;
        }
#endif
        case open::Fmt::kUnit: snprintf(buf, sizeof(buf), "%.2f", v); break;
        case open::Fmt::kPan:
        {
            const int p = int(std::lround(v * 100.0f));
            if (p == 0)
            {
                return "0";
            }
            snprintf(buf, sizeof(buf), "%c%d", p < 0 ? 'L' : 'R', p < 0 ? -p : p);
            break;
        }
    }
    return buf;
}

} // namespace

// Knob position [0,1] <-> value
float open::curveToValue(const ParamDef& d, float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    switch (d.curve)
    {
        case open::CurveKind::kLinear: break;
        case open::CurveKind::kExp:
            if (d.min > 0.0f)
            {
                return d.min * std::pow(d.max / d.min, n);
            }
            n = n * n;
            break;
        case open::CurveKind::kSteep: n = n * n * n; break;
        case open::CurveKind::kPower: n = n * n; break;
    }
    return d.min + (d.max - d.min) * n;
}

float open::valueToCurve(const ParamDef& d, float v)
{
    v = std::clamp(v, d.min, d.max);
    if (d.curve == open::CurveKind::kExp && d.min > 0.0f)
    {
        return std::log(v / d.min) / std::log(d.max / d.min);
    }
    const float n = d.max > d.min ? (v - d.min) / (d.max - d.min) : 0.0f;
    switch (d.curve)
    {
        case open::CurveKind::kExp:
        case open::CurveKind::kPower: return std::sqrt(n);
        case open::CurveKind::kSteep: return std::cbrt(n);
        default: return n;
    }
}

namespace
{

// ----------------------------------------------------------------- registry

void fillFromDef(InstrumentType* type, const EngineDef& def)
{
    type->name = def.name;
    type->createMetadata = [&def](Patch&, Patch::Metadata& metadata)
    {
        using Meta = Patch::Metadata;
        metadata.instrumentTypeId = def.typeId;
        metadata.outputChannels = Meta::OutputChannels::kMono;
        metadata.maxVoices = def.maxVoices;
        DEVHOOK_TRACE(0x4D31, reinterpret_cast<uintptr_t>(&def), def.typeId, def.paramCount);
        DEVHOOK_TRACE(0x4D30, reinterpret_cast<uintptr_t>(&metadata), reinterpret_cast<uintptr_t>(metadata.params.data()), metadata.params.size());
        metadata.params.resize(def.paramCount);
        for (int i = 0; i < def.paramCount; i++)
        {
            const ParamDef& d = def.params[i];
            auto& p = metadata.params[i];
            p.displayName = d.name;
            p.defaultValue = d.def;
            p.minValue = d.min;
            p.maxValue = d.max;
            p.isEnum = d.fmt == open::Fmt::kEnum;
            p.editCurve = static_cast<Meta::Param::Curve>(d.curve);
            p.valueFormatter = [&d](const Patch&, float v) { return formatValue(d, v); };
        }
        metadata.layoutFlat.sections.reserve(def.sectionCount);
        for (int s = 0; s < def.sectionCount; s++)
        {
            const auto& sd = def.sections[s];
            Meta::EditorLayout::EditorSection section;
            section.name = sd.name;
            section.shortName = sd.shortName;
            section.elements.reserve(sd.count);
            for (int e = 0; e < sd.count; e++)
            {
                section.elements.push_back(
                  { Meta::EditorLayout::EditorElement::Type::kParam, sd.ids[e] });
            }
            metadata.layoutFlat.sections.push_back(std::move(section));
        }
        DEVHOOK_TRACE(0x4D37, def.typeId, metadata.layoutFlat.sections.size(), 0);
        metadata.initDefaultMacros = [&def](std::vector<Meta::Macro>& macros)
        {
            for (int m = 0; m < def.macroCount && m < int(kUserMacroCount); m++)
            {
                const auto& md = def.macros[m];
                macros[m].displayName = md.name;
                macros[m].bipolar = md.bipolar;
                macros[m].modifiers.clear();
                for (int t = 0; t < md.count; t++)
                {
                    macros[m].modifiers.push_back({ md.targets[t].param, md.targets[t].amount });
                }
            }
            auto& vel = macros[kVelocityMacro];
            vel.displayName = "Velocity";
            vel.system = true;
            vel.perVoice = true;
            if (def.volumeParam >= 0)
            {
                vel.modifiers = { { def.volumeParam, 1.0f } };
            }
            auto& pr = macros[kVelocityMacro + 1];
            pr.displayName = "Pressure";
            pr.system = true;
            pr.perVoice = true;
        };
    };
    type->createVoice = [](int) -> std::unique_ptr<InstrumentVoice> { return nullptr; };
}

std::unique_ptr<InstrumentType> createFromDef(const EngineDef& def)
{
    auto type = std::make_unique<InstrumentType>();
    fillFromDef(type.get(), def);
    return type;
}

// Engines that live in the closed libs and have no open replacement here: name only.
std::unique_ptr<InstrumentType> createUnavailable(const char* name)
{
    auto type = std::make_unique<InstrumentType>();
    type->name = name;
    type->createMetadata = [](Patch&, Patch::Metadata& metadata)
    { metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo; };
    type->createVoice = [](int) -> std::unique_ptr<InstrumentVoice> { return nullptr; };
    return type;
}

} // namespace

// ----------------------------------------------------------------- engine list (built in + installed plugins)
namespace
{
struct Builtin
{
    const EngineDef* def;
    int16_t stored;
    int16_t patchEngine;
};
constexpr Builtin kBuiltin[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, PATCH_ENGINE, ...) { &open::DEF, STORED, PATCH_ENGINE },
#include "engine_list.h"
#undef SYNTH_ENGINE
};
constexpr int kBuiltinCount = int(sizeof(kBuiltin) / sizeof(kBuiltin[0]));
// The plugin engines of the list (plug_synth.cpp synthBoot, installed or not): each has its own stored number, so the stored number space
// bounds them (249), nothing else. Their tables are in the SDRAM: about 0.1 KB each here (list entries, registry entry, name).
constexpr int kPlugMax = open::kPlugStoredLast - open::kPlugStoredFirst + 1;
// Any plugin engine number (open::kPlugStoredFirst..kPlugStoredLast) that is not in the list is "missing": one placeholder for all
// of them (type open::kMissingTypeId), the project keeps the number, the name comes from the names this firmware has seen.
// 0 to 6 are Polyend's (always built in when they exist in this firmware family), DIRT (5) never had an open engine.
// Remembered plugin engine names, one per stored number (name[0] 0: none). Every number has its place, so none is ever refused.
struct EngineName
{
    char name[9];
};
static_assert(std::is_trivially_default_constructible_v<EngineName>, "OPEN_EXTERNAL_RAM: no static constructor may touch the SDRAM");
OPEN_EXTERNAL_RAM EngineName gNames[kPlugMax];
bool gNamesClear; // gNames was zeroed (the SDRAM holds garbage until then)

void namesClear()
{
    if (!gNamesClear)
    {
        std::memset(gNames, 0, sizeof(gNames));
        gNamesClear = true;
    }
}

static_assert(std::is_trivially_default_constructible_v<EngineRef>, "OPEN_EXTERNAL_RAM: no static constructor may touch the SDRAM");
OPEN_EXTERNAL_RAM EngineRef gPlug[kPlugMax]; // by stored number (engineListAdd keeps them in order)
int gPlugCount;
OPEN_EXTERNAL_RAM EngineRef gList[kBuiltinCount + kPlugMax];
int gCount; // 0 until the list is built (first use)
EngineDef gMissing = {};
// The registry entries of the plugin engines (getInstrumentType), made by registerEngines in place, never destroyed: in the SDRAM, so a
// big library takes no internal heap (a std::map node and two blocks each would)
struct TypeSlot
{
    alignas(InstrumentType) unsigned char bytes[sizeof(InstrumentType)];
};
static_assert(std::is_trivially_default_constructible_v<TypeSlot>, "OPEN_EXTERNAL_RAM: no static constructor may touch the SDRAM");
OPEN_EXTERNAL_RAM TypeSlot gPlugType[kPlugMax];
int gPlugTypes; // gPlugType[0..gPlugTypes) are made, for gPlug[0..gPlugTypes)

// built in and plugin engines by stored number (both ascend already): a merge
void buildList()
{
    int b = 0, p = 0;
    while (gCount < kBuiltinCount + gPlugCount)
    {
        if (b < kBuiltinCount && (p >= gPlugCount || kBuiltin[b].stored < gPlug[p].stored))
        {
            gList[gCount] = { kBuiltin[b].def, kBuiltin[b].stored, kBuiltin[b].patchEngine, int8_t(b) };
            b++;
        }
        else
        {
            gList[gCount] = gPlug[p++];
        }
        gCount++;
    }
    gMissing.name = "Missing";
    gMissing.typeId = open::kMissingTypeId;
    gMissing.volumeParam = -1;
    gMissing.maxVoices = open::kMaxVoicesPerInstrument; // never clamps the part's voice count
}

inline void ensureList()
{
    if (!gCount)
    {
        buildList();
    }
}
} // namespace

bool open::engineListAdd(const EngineDef* def, int stored, int patchEngine)
{
    if (gCount || gPlugCount >= kPlugMax || stored < kPlugStoredFirst || stored > kPlugStoredLast)
    {
        return false; // the list is in use (the synth started), full, or not a plugin number (each has its own, so 249 never fill up)
    }
    for (int i = 0; i < kBuiltinCount + gPlugCount; i++) // its numbers must be its own
    {
        const bool bi = i < kBuiltinCount;
        const EngineRef& r = gPlug[bi ? 0 : i - kBuiltinCount];
        const int st = bi ? kBuiltin[i].stored : r.stored;
        const int pe = bi ? kBuiltin[i].patchEngine : r.patchEngine;
        const int ty = bi ? kBuiltin[i].def->typeId : r.def->typeId;
        if (st == stored || ty == def->typeId || pe == patchEngine || def->typeId == open::kMissingTypeId)
        {
            return false;
        }
    }
    int at = gPlugCount++; // in order of stored numbers (buildList merges)
    for (; at > 0 && gPlug[at - 1].stored > stored; at--)
    {
        gPlug[at] = gPlug[at - 1];
    }
    gPlug[at] = { def, stored, patchEngine, -1 };
    return true;
}

int open::engineCount()
{
    ensureList();
    return gCount;
}

const EngineRef& open::engineAt(int ui)
{
    ensureList();
    return gList[ui >= 0 && ui < gCount ? ui : 0];
}

int open::engineUiForStored(int stored)
{
    ensureList();
    for (int i = 0; i < gCount; i++)
    {
        if (gList[i].stored == stored)
        {
            return i;
        }
    }
    return -1;
}

const EngineDef* open::missingDef(int stored)
{
    return stored >= kPlugStoredFirst && stored <= kPlugStoredLast && engineUiForStored(stored) < 0 ? &gMissing : nullptr;
}

const char* open::missingName(int stored)
{
    if (!missingDef(stored))
    {
        return "";
    }
    namesClear();
    if (gNames[stored - kPlugStoredFirst].name[0])
    {
        return gNames[stored - kPlugStoredFirst].name;
    }
    static char num[8];
    snprintf(num, sizeof(num), "#%d", stored);
    return num;
}

bool open::engineNameRemember(int stored, const char* name)
{
    if (stored < kPlugStoredFirst || stored > kPlugStoredLast || !name || !name[0])
    {
        return false;
    }
    namesClear();
    EngineName& n = gNames[stored - kPlugStoredFirst];
    if (std::strncmp(n.name, name, 8) == 0)
    {
        return false;
    }
    snprintf(n.name, sizeof(n.name), "%.8s", name);
    return true;
}

int open::engineNamesText(char* buf, int cap)
{
    namesClear();
    int len = 0;
    for (int i = 0; i < kPlugMax && len < cap; i++)
    {
        if (gNames[i].name[0])
        {
            len += snprintf(buf + len, size_t(cap - len), "%d %s\r\n", i + kPlugStoredFirst, gNames[i].name);
        }
    }
    return len < cap ? len : cap - 1;
}

int open::typeForStored(int stored)
{
    const int ui = engineUiForStored(stored);
    return ui >= 0 ? gList[ui].def->typeId : (missingDef(stored) ? kMissingTypeId : -1);
}

const EngineDef* open::engineDefForType(int typeId)
{
    ensureList();
    for (int i = 0; i < gCount; i++)
    {
        if (gList[i].def->typeId == typeId)
        {
            return gList[i].def;
        }
    }
    return typeId == kMissingTypeId ? &gMissing : nullptr;
}

int open::firstEngineTypeId()
{
    return engineAt(0).def->typeId;
}

int open::engineIdForType(int typeId)
{
    ensureList();
    for (int i = 0; i < gCount; i++)
    {
        if (gList[i].def->typeId == typeId)
        {
            return gList[i].patchEngine;
        }
    }
    return typeId == kMissingTypeId ? kMissingTypeId : 0; // the placeholder's own number (its files are never written)
}

int open::typeForEngineId(int engine)
{
    ensureList();
    for (int i = 0; i < gCount; i++)
    {
        if (gList[i].patchEngine == engine)
        {
            return gList[i].def->typeId;
        }
    }
    // a plugin engine id this build has not got: the missing placeholder's (a part on it keeps its slot 0 backup on an engine change)
    return engine >= kPlugPatchFirst && engine <= kPlugPatchLast ? kMissingTypeId : -1;
}

// Every engine of the list and every missing placeholder into the registry (InstrumentTypeRegistry::registerInstrumentTypes)
void open::registerEngines(InstrumentTypeRegistry& registry)
{
    ensureList();
    for (int i = 0; i < gCount; i++)
    {
        if (gList[i].builtin >= 0)
        {
            registry.addInstrumentType(gList[i].def->typeId, createFromDef(*gList[i].def));
        }
    }
    for (; gPlugTypes < gPlugCount; gPlugTypes++) // the plugin engines: in place, once (getInstrumentType finds them)
    {
        fillFromDef(new (gPlugType[gPlugTypes].bytes) InstrumentType(), *gPlug[gPlugTypes].def);
    }
    registry.addInstrumentType(kMissingTypeId, createFromDef(gMissing));
}

std::shared_ptr<const InstrumentType> InstrumentTypeRegistry::getInstrumentType(int typeId) const
{
    auto it = instrumentTypes.find(typeId);
    if (it != instrumentTypes.end())
    {
        return it->second;
    }
    for (int i = 0; i < gPlugTypes; i++)
    {
        if (gPlug[i].def->typeId == typeId)
        {   // owns nothing (aliasing constructor, no control block): the entry lives as long as the firmware runs
            return std::shared_ptr<const InstrumentType>(std::shared_ptr<const InstrumentType>(),
                                                         reinterpret_cast<const InstrumentType*>(gPlugType[i].bytes));
        }
    }
    return nullptr;
}

void InstrumentTypeRegistry::addInstrumentType(int typeId,
                                               std::unique_ptr<InstrumentType> instrumentType)
{
    instrumentTypes[typeId] = std::move(instrumentType);
}

std::unique_ptr<InstrumentType> createMidiOut() { return createUnavailable("MIDI"); }

namespace open
{

// ------------------------------------------------------------------- patch

// The Patch FX bank (end of this file) and the live patch of a part. The values the editor works on are the ones in the patch, or in the
// shadow row of the bank while the FX has replaced them. Audio interrupts are disabled by the caller where it is said so.
float patchBankEditedValue(const OpenPatch* p, const float* live, int id); // any thread state of the FX, no lock
void patchBankBeforeEdit(const OpenPatch* p);                              // locked: the shadow goes back before a value is written
void patchBankExportLocked(const OpenPatch& p, PatchData& out);            // locked: the shadow goes back, then p is exported
void patchBankValuesReplaced(const OpenPatch& p);                          // locked: an import replaced the values, no shadow is left
void patchBankPublish(int part, const OpenPatch& p, const PatchData& saved); // takes the lock: the row of the open slot is what was saved

// The pool of OpenPatch objects (3,036 B each on the MK66), in the SDRAM. A part holds one live patch, a new one is made before the old is
// freed (engine change, init) and the Synth keeps a few spare references, so 5 is enough. Main thread only, like every create and free of a
// patch. A full pool falls back to the heap, as before.
namespace
{
constexpr int kPatchPoolSize = 5;
struct PatchPoolSlot
{
    alignas(8) unsigned char bytes[(sizeof(OpenPatch) + 7) & ~size_t(7)];
};
static_assert(std::is_trivially_default_constructible_v<PatchPoolSlot>, "OPEN_EXTERNAL_RAM: no static constructor may touch the SDRAM");
OPEN_EXTERNAL_RAM PatchPoolSlot gPatchPool[kPatchPoolSize];
bool gPatchPoolUsed[kPatchPoolSize];
} // namespace

void* OpenPatch::operator new(size_t n)
{
    if (n <= sizeof(PatchPoolSlot::bytes))
    {
        for (int i = 0; i < kPatchPoolSize; i++)
        {
            if (!gPatchPoolUsed[i])
            {
                gPatchPoolUsed[i] = true;
                DEVHOOK_TRACE(0x4F4F, i, reinterpret_cast<uintptr_t>(gPatchPool[i].bytes), n);
                return gPatchPool[i].bytes;
            }
        }
    }
    void* const heap = ::operator new(n);
    DEVHOOK_TRACE(0x4F4E, n, reinterpret_cast<uintptr_t>(heap), 0);   // the pool is full: a 3 KB patch from the heap
    return heap;
}

void OpenPatch::operator delete(void* p) noexcept
{
    {
        uint32_t ipsr;
        __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
        DEVHOOK_TRACE(0x4F44, reinterpret_cast<uintptr_t>(p), ipsr, 0);   // ipsr != 0: freed inside an interrupt
    }
    const auto* b = static_cast<const unsigned char*>(p);
    if (b >= reinterpret_cast<const unsigned char*>(gPatchPool) && b < reinterpret_cast<const unsigned char*>(gPatchPool + kPatchPoolSize))
    {
        gPatchPoolUsed[(b - reinterpret_cast<const unsigned char*>(gPatchPool)) / sizeof(PatchPoolSlot)] = false;
        return;
    }
    ::operator delete(p);
}

OpenPatch::OpenPatch(int typeId, const InstrumentType* type)
  : def(open::engineDefForType(typeId))
{
    {
        int used = 0;
        for (int i = 0; i < kPatchPoolSize; i++) used += gPatchPoolUsed[i] ? 1 : 0;
        DEVHOOK_TRACE(0x4F50, reinterpret_cast<uintptr_t>(this), typeId, used);
        const struct mallinfo mi = mallinfo();
        DEVHOOK_TRACE(0x4F51, mi.arena, mi.uordblks, mi.fordblks);
    }
    metadata.instrumentTypeId = typeId;
    if (type && type->createMetadata)
    {
        type->createMetadata(*this, metadata);
    }
    metadata.initDefaultMacros(metadata.macros);
    count = std::min<int>(metadata.params.size(), kMaxParams);
    for (int i = 0; i < count; i++)
    {
        values[i] = metadata.params[i].defaultValue.value_or(0.0f);
    }
    for (int m = 0; m < int(kMacroCount); m++)
    {
        macroCC[m] = metadata.macros[m].bipolar ? 63 : 0;
    }
}

Patch::optional_paramvalue_t OpenPatch::getParamValue(int id) const
{
    return inRange(id) ? optional_paramvalue_t{ patchBankEditedValue(this, values, id) } : optional_paramvalue_t{ std::nullopt };
}

tiny_optional<float> OpenPatch::getParamValueEditMapped(int id) const
{
    if (!inRange(id) || !def)
    {
        return std::nullopt;
    }
    return valueToCurve(def->params[id], patchBankEditedValue(this, values, id));
}

void OpenPatch::setParamValueEditMapped(int id, tiny_optional<float> v, ParamChangeSource src)
{
    if (inRange(id) && def && v.has_value())
    {
        setParamValue(id, curveToValue(def->params[id], *v), src);
    }
}

Patch::optional_paramvalue_t OpenPatch::getParamFinalValue(int id) const
{
    if (!inRange(id))
    {
        return std::nullopt;
    }
    float out[kMaxParams];
    effective(out);
    return out[id];
}

void OpenPatch::setParamValue(int id, optional_paramvalue_t value, ParamChangeSource)
{
    if (!inRange(id))
    {
        return;
    }
    const auto& p = metadata.params[id];
    const float v = value.has_value() ? *value : p.defaultValue.value_or(0.0f);
    patchBankBeforeEdit(this); // the caller holds the audio lock: the edit lands on the edited values, never on the ones of an FX slot
    values[id] = std::clamp(v, float(p.minValue), float(p.maxValue));
}

Patch::optional_paramvalue_t OpenPatch::getMacroValue(int id) const
{
    return validMacro(id) ? optional_paramvalue_t{ macroCC[id] * kMaxMacroValue / 127.0f }
                          : optional_paramvalue_t{ std::nullopt };
}

void OpenPatch::setMacroValue(int id, optional_paramvalue_t value, tiny_optional<int>)
{
    if (validMacro(id) && value.has_value())
    {
        macroCC[id] = uint8_t(std::clamp(std::lround(*value * 127.0f / kMaxMacroValue), 0L, 127L));
    }
}

void OpenPatch::setMacroModifier(int macroId, int idx, tiny_optional<int> paramId)
{
    if (!validMacro(macroId) || idx < 0)
    {
        return;
    }
    auto& mods = metadata.macros[macroId].modifiers;
    if (!paramId.has_value())
    {
        if (idx < int(mods.size()))
        {
            mods.erase(mods.begin() + idx);
        }
    }
    else if (idx < int(mods.size()))
    {
        mods[idx].paramId = *paramId;
    }
    else if (idx == int(mods.size()))
    {
        mods.push_back({ *paramId, 0.0f });
    }
}

void OpenPatch::setMacroModifierAmount(int macroId, int idx, float amount)
{
    if (auto* mod = modifier(macroId, idx))
    {
        mod->amount = std::clamp(amount,
                                 -Metadata::Macro::ParamModifier::kMaxAmount,
                                 Metadata::Macro::ParamModifier::kMaxAmount);
    }
}

tiny_optional<float> OpenPatch::getMacroModifierAmount(int macroId, int idx)
{
    auto* mod = modifier(macroId, idx);
    return mod ? tiny_optional<float>{ mod->amount } : tiny_optional<float>{ std::nullopt };
}

void OpenPatch::clearMacroModifiers()
{
    for (auto& macro : metadata.macros)
    {
        macro.modifiers.clear();
    }
}

void OpenPatch::changeMacroModifierAmount(int macroId, int idx, paramvalue_t delta)
{
    if (auto* mod = modifier(macroId, idx))
    {
        setMacroModifierAmount(macroId, idx, mod->amount + delta);
    }
}

void OpenPatch::setMacroBipolar(int macroId, bool bipolar)
{
    if (validMacro(macroId))
    {
        metadata.macros[macroId].bipolar = bipolar;
    }
}

void OpenPatch::setMacroDisplayName(int macroId, std::string name)
{
    if (validMacro(macroId))
    {
        name.resize(std::min<size_t>(name.size(), Metadata::Macro::kMaxDisplayNameLen));
        metadata.macros[macroId].displayName = std::move(name);
    }
}

Patch::Metadata::Macro::ParamModifier* OpenPatch::modifier(int macroId, int idx)
{
    if (!validMacro(macroId) || idx < 0 || idx >= int(metadata.macros[macroId].modifiers.size()))
    {
        return nullptr;
    }
    return &metadata.macros[macroId].modifiers[idx];
}

// ---------------------------------------------------------------- .patch transfer

void OpenPatch::exportData(PatchData& d) const
{
    d.engine = engineIdForType(metadata.instrumentTypeId);
    d.nParams = count;
    for (int i = 0; i < count; i++)
    {
        const auto& p = metadata.params[i];
        d.values[i] = float(p.minValue) == float(p.maxValue) ? NAN : values[i]; // unused slot: absent
    }
    d.nMacros = kPatchMacros;
    for (int m = 0; m < kPatchMacros; m++)
    {
        const auto& src = metadata.macros[m];
        PatchMacroData& dst = d.macros[m];
        std::strncpy(dst.name, src.displayName.c_str(), kMacroNameMax);
        dst.bipolar = src.bipolar;
        int n = 0;
        for (const auto& mod : src.modifiers)
        {
            if (mod.paramId.has_value() && n < kMacroTargets)
            {
                dst.param[n] = int16_t(*mod.paramId);
                dst.amount[n] = mod.amount;
                n++;
            }
        }
        dst.count = int8_t(n);
    }
    for (int i = 0; i < kModSlots; i++)
    {
        d.mod[i] = mod[i]; // the stored identity, so an unresolved destination is written back as it was read
    }
    d.extra = extra;
}

// The data of a new patch of the engine, as exportData of a fresh OpenPatch gives it (the smoke test compares both for every engine) but
// from the engine definition alone: no metadata, no allocation. `d` is fresh.
void initPatchData(const EngineDef& def, PatchData& d)
{
    d.engine = engineIdForType(def.typeId);
    d.nParams = std::min(def.paramCount, kMaxParams);
    for (int i = 0; i < d.nParams; i++)
    {
        const ParamDef& p = def.params[i];
        d.values[i] = p.min == p.max ? NAN : p.def; // unused slot: absent
    }
    d.nMacros = kPatchMacros;
    for (int m = 0; m < kPatchMacros; m++)
    {
        PatchMacroData& dst = d.macros[m];
        if (m < def.macroCount && m < int(kUserMacroCount))
        {
            const MacroDef& src = def.macros[m];
            std::strncpy(dst.name, src.name, kMacroNameMax);
            dst.bipolar = src.bipolar;
            for (int t = 0; t < src.count && t < kMacroTargets; t++)
            {
                dst.param[t] = int16_t(src.targets[t].param);
                dst.amount[t] = src.targets[t].amount;
                dst.count++;
            }
        }
        else if (m == kVelocityMacro)
        {
            std::strncpy(dst.name, "Velocity", kMacroNameMax);
            if (def.volumeParam >= 0)
            {
                dst.param[0] = int16_t(def.volumeParam);
                dst.amount[0] = 1.0f;
                dst.count = 1;
            }
        }
        else if (m == kVelocityMacro + 1)
        {
            std::strncpy(dst.name, "Pressure", kMacroNameMax);
        }
    }
}

void OpenPatch::valuesFromData(const PatchData& d, float* out) const
{
    for (int i = 0; i < count; i++)
    {
        const auto& p = metadata.params[i];
        const float v = i < d.nParams ? d.values[i] : NAN;
        const bool ok = std::isfinite(v) && (float(p.minValue) == float(p.maxValue) ||
                                             (v >= float(p.minValue) && v <= float(p.maxValue)));
        out[i] = ok ? v : p.defaultValue.value_or(0.0f);
    }
}

void OpenPatch::importData(const PatchData& d)
{
    valuesFromData(d, values);
    for (int m = 0; m < d.nMacros && m < kPatchMacros; m++)
    {
        const PatchMacroData& src = d.macros[m];
        auto& dst = metadata.macros[m];
        if (src.name[0])
        {
            dst.displayName = src.name;
        }
        else if (m < int(kUserMacroCount))
        {
            dst.displayName = std::string("Macro ") + char('1' + m);
        }
        if (dst.bipolar != src.bipolar)
        {
            macroCC[m] = src.bipolar ? 63 : 0;
        }
        dst.bipolar = src.bipolar;
        dst.modifiers.clear();
        for (int t = 0; t < src.count; t++)
        {
            if (src.param[t] >= 0 && src.param[t] < count)
            {
                dst.modifiers.push_back({ src.param[t], std::clamp(src.amount[t], -2.0f, 2.0f) });
            }
        }
    }
    // Automation: range checks, then each destination is looked up in this engine's table (open_mod.h modResolve). A param found under
    // another index (a plugin update moved it) follows the param. The slot state of the instrument starts over (serial).
    for (int i = 0; i < kModSlots; i++)
    {
        ModSlot m = d.mod[i];
        modSanitize(m);
        m.dest = modResolve(def, m);
        if (m.dest >= 0 && m.kind == ModKind::kParam)
        {
            m.index = m.dest;
        }
        mod[i] = m;
    }
    modOn = modAnyOn(mod);
    modSer++;
    extra = d.extra;
}

// ------------------------------------------------------------- automation (editor access)

const ModSlot& modSlotOf(const Patch& p, int s)
{
    return static_cast<const OpenPatch&>(p).modSlot(s);
}

void modSlotSet(Patch& p, int s, const ModSlot& v)
{
    static_cast<OpenPatch&>(p).setModSlot(s, v);
}

const EngineDef* modEngineOf(const Patch& p)
{
    return static_cast<const OpenPatch&>(p).getDef();
}

const char* modDestName(const Patch& p, int code)
{
    const auto& op = static_cast<const OpenPatch&>(p);
    if (code >= kModPickMacro && code < kModPickMacro + kModMacros)
    {
        return op.getMetadata().macros[code - kModPickMacro].displayName.c_str();
    }
    const EngineDef* d = op.getDef();
    return d && code >= 0 && code < d->paramCount && d->params[code].name ? d->params[code].name : "";
}

// ------------------------------------------------------------- patch store
// Patches/<ENGINE>/<name>.patch (typeFolders) or Workspace/<name>.patch, stock-compatible files.

namespace
{
// Heap buffer of the slot names file (1 KB). On the MK66 operator new is a plain malloc (teensy_core/new.cpp) and returns null when the
// heap is short, so std::make_unique would zero-fill from address 0 and fault. This returns null instead and the callers give up cleanly.
inline std::unique_ptr<uint8_t[]> tryBytes(size_t n)
{
    return std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[n]);
}

// The buffers of every .patch read and write (store and bank): the file bytes and the decoded data, 9.8 KB. They live in the SDRAM,
// not on the heap: on the device the heap had no 8 KB block left after a while and every slot select failed ("Error loading slot").
// Main thread only and never nested. A second lease while one is held gets nothing and its caller fails cleanly.
// The decoded data is raw bytes, built in place by each lease: PatchData has member initialisers, so a PatchData member made gPatchScratch
// a static constructor that wrote the SDRAM before it was set up (f563f27 never booted).
struct PatchScratch
{
    uint8_t file[kPatchFileMax + 1];
    alignas(PatchData) unsigned char data[sizeof(PatchData)];
};
static_assert(std::is_trivially_default_constructible_v<PatchScratch>, "OPEN_EXTERNAL_RAM: no static constructor may touch the SDRAM");
OPEN_EXTERNAL_RAM PatchScratch gPatchScratch;
bool gPatchScratchBusy = false;

class ScratchLease
{
  public:
    ScratchLease()
    {
        if (!gPatchScratchBusy)
        {
            gPatchScratchBusy = true;
            s = &gPatchScratch;
            d = new (s->data) PatchData(); // fresh: the SDRAM is not initialised
        }
    }
    ~ScratchLease()
    {
        if (s)
        {
            gPatchScratchBusy = false;
        }
    }
    ScratchLease(const ScratchLease&) = delete;
    ScratchLease& operator=(const ScratchLease&) = delete;
    explicit operator bool() const { return s != nullptr; }
    uint8_t* buf() const { return s->file; }
    PatchData& data() const { return *d; }

  private:
    PatchScratch* s = nullptr;
    PatchData* d = nullptr;
};

class OpenPatchStore final : public PatchStore
{
    static constexpr size_t kExtLen = 6; // ".patch"

  public:
    OpenPatchStore(const InstrumentTypeRegistry& registry, FileSystem* fs, bool typeFolders)
      : registry(registry)
      , fs(fs)
      , typeFolders(typeFolders)
    {
    }

    std::unique_ptr<Patch> createPatch(int typeId) override
    {
        // A type this build does not have (PERC, Shaping, an engine left out of SYNTH_ENGINES): the Init patch
        // of the first engine of this build
        if (!engineDefForType(typeId))
        {
            typeId = firstEngineTypeId();
        }
        DEVHOOK_TRACE(0x4F43, typeId, 0, 0);
        auto type = registry.getInstrumentType(typeId);
        DEVHOOK_TRACE(0x4F42, typeId, reinterpret_cast<uintptr_t>(type.get()), type ? type->createMetadata.operator bool() : 0);
        return std::make_unique<OpenPatch>(typeId, type.get());
    }
    std::vector<std::pair<std::string_view, int>> listPatchesFilter(
      std::function<bool(InstrumentTypeId)>) override
    {
        return {};
    }
    // The views point into the file system's own list buffer (valid until its next listFiles), cut
    // before ".patch". The editor copies exactly size() characters, so no terminator is needed.
    std::vector<std::string_view> listPatchesByType(int typeId) override
    {
        const EngineDef* def = engineDefForType(typeId);
        if (!fs || !def)
        {
            return {};
        }
        auto files = fs->listFiles(typeFolders ? def->name : "",
                                   [](const std::string& n)
                                   {
                                       return n.size() > kExtLen &&
                                              std::strcmp(n.c_str() + n.size() - kExtLen, ".patch") == 0;
                                   },
                                   std::nullopt);
        for (auto& f : files)
        {
            f = f.substr(0, f.size() - kExtLen);
        }
        for (size_t i = 1; i < files.size(); i++) // alphabetical, a few hundred names at most
        {
            for (size_t j = i; j > 0 && files[j] < files[j - 1]; j--)
            {
                std::swap(files[j], files[j - 1]);
            }
        }
        return files;
    }
    bool loadPatch(const std::string& name, Patch& patch, bool synchronizeAudio) override
    {
        OpenPatch& op = static_cast<OpenPatch&>(patch);
        const EngineDef* def = op.getDef();
        if (!fs || !def)
        {
            return false;
        }
        if (!open::engineOf(def))
        {
            return true; // a missing engine: nothing to load into a patch without parameters, the file stays as it is
        }
        ScratchLease sc;
        if (!sc || readPatchFile(def, op.typeId(), name, sc.buf(), sc.data()) != FileRead::kOk)
        {
            return false;
        }
        if (synchronizeAudio)
        {
            OPEN_LOCK_AUDIO();
            op.importData(sc.data());
            patchBankValuesReplaced(op); // what the FX had set aside belonged to the patch that was here
            OPEN_UNLOCK_AUDIO();
        }
        else
        {
            op.importData(sc.data());
            patchBankValuesReplaced(op);
        }
        return true;
    }
    bool savePatch(const std::string& name, const Patch& patch) override
    {
        return saveSnapshot(static_cast<const OpenPatch&>(patch), name, -1);
    }
    // Writes the patch as file `name`. The values and macros are exported in one audio-free window, with the values an FX replaced put
    // back first, so the file is one patch whatever the FX does while it is encoded and written. The old file stays until the new one is
    // complete. `part` >= 0: the file is the open slot of that part and its row of the Patch FX bank becomes the values that were saved.
    bool saveSnapshot(const OpenPatch& op, const std::string& name, int part)
    {
        const EngineDef* def = op.getDef();
        if (!fs || !def)
        {
            return false;
        }
        if (!open::engineOf(def))
        {
            return true; // a missing engine: its file already holds the patch, never overwrite it with an empty one
        }
        constexpr size_t kCap = 4096; // every file this writes is at most 4 KB
        static_assert(kCap <= sizeof(PatchScratch::file));
        ScratchLease sc;
        if (!sc)
        {
            return false;
        }
        OPEN_LOCK_AUDIO();
        patchBankExportLocked(op, sc.data());
        OPEN_UNLOCK_AUDIO();
        const size_t n = encodePatch(sc.data(), sc.buf(), kCap);
        if (n == 0)
        {
            return false;
        }
        if (typeFolders)
        {
            fs->createFolder(def->name);
        }
        if (!writeRaw(pathFor(def, name), sc.buf(), n))
        {
            return false;
        }
        if (part >= 0)
        {
            patchBankPublish(part, op, sc.data());
        }
        return true;
    }
    std::optional<InstrumentTypeId> loadPatchInstrumentType(const std::string&) override
    {
        return std::nullopt;
    }
    // Reads and decodes one file of the engine `typeId` (buf holds kPatchFileMax + 1 bytes, out is fresh).
    enum class FileRead
    {
        kOk,
        kMissing, // no such file
        kOther,   // there, but damaged or of another engine
        kError,   // the card failed while it was read
    };
    FileRead readPatchFile(const EngineDef* def, int typeId, const std::string& name, uint8_t* buf, PatchData& out)
    {
        if (!fs)
        {
            return FileRead::kMissing;
        }
        bool openError = false;
        auto h = openRead(pathFor(def, name), openError);
        if (!h)
        {
            return openError ? FileRead::kError : FileRead::kMissing;
        }
        size_t n = 0;
        int32_t got = 1;
        while (n <= kPatchFileMax && (got = fs->read(*h, std::span<uint8_t>(buf + n, kPatchFileMax + 1 - n))) > 0)
        {
            n += size_t(got);
        }
        fs->close(*h);
        if (got < 0) // a failed read is not the end of the file: the prefix read so far may decode and would pass for the whole patch
        {
            return FileRead::kError;
        }
        return decodePatch(buf, n, out) && typeForEngineId(out.engine) == typeId ? FileRead::kOk : FileRead::kOther;
    }
    // A whole small file by its full name (with extension) from the store's folder, at most `cap` bytes. 0 when missing or empty.
    // `ioError` (if given) says the card failed: it could not be read, or the file may be there and could not be opened. The bytes
    // returned are then not the file. 0 without ioError: the file is not there (confirmed by the card).
    size_t readRaw(const std::string& file, uint8_t* buf, size_t cap, bool* ioError = nullptr)
    {
        if (ioError)
        {
            *ioError = false;
        }
        bool openError = false;
        auto h = fs ? openRead(file, openError) : std::nullopt;
        if (!h)
        {
            if (ioError)
            {
                *ioError = openError;
            }
            return 0;
        }
        size_t n = 0;
        int32_t got = 1;
        while (n < cap && (got = fs->read(*h, std::span<uint8_t>(buf + n, cap - n))) > 0)
        {
            n += size_t(got);
        }
        fs->close(*h);
        if (got < 0)
        {
            if (ioError)
            {
                *ioError = true;
            }
            return 0;
        }
        return n;
    }
    // The file `file` becomes exactly these bytes, or stays as it was: they go to <file>.tmp first and replace the old file only when
    // all of them are written (an SD error, a full card or a power loss never leaves half a project file).
    bool writeRaw(const std::string& file, const uint8_t* data, size_t n)
    {
        if (!fs)
        {
            return false;
        }
        const std::string tmp = file + ".tmp";
        auto h = fs->open(tmp, FileSystem::OpenOptions::kWrite);
        if (!h)
        {
            return false;
        }
        const bool ok = fs->write(*h, std::span<const uint8_t>(data, n));
        fs->close(*h);
        if (!ok)
        {
            fs->remove(tmp);
            return false;
        }
        if (!fs->replace(tmp, file))
        {
            fs->remove(tmp);
            return false;
        }
        return true;
    }

  private:
    // Opens a file for reading. No handle and openError false: the card says the file is not there (and no interrupted replace left its
    // old version at <file>.bak, which is moved back first). openError: it may be there and could not be opened, or the card failed.
    std::optional<FileSystem::FileHandle> openRead(const std::string& path, bool& openError)
    {
        openError = false;
        auto h = fs->open(path, FileSystem::OpenOptions::kNone);
        if (h)
        {
            return h;
        }
        FsOps ops{ fs };
        switch (recoverBackupOps(ops, path.c_str()))
        {
            case Presence::kAbsent: return std::nullopt;
            case Presence::kPresent: break; // there now (moved back from <path>.bak) or the first open failed on something else
            default: openError = true; return std::nullopt;
        }
        h = fs->open(path, FileSystem::OpenOptions::kNone);
        openError = !h;
        return h;
    }
    // open_fileops.h over the shim's FileSystem: rename is replace(), which moves a file to a name that is free
    struct FsOps
    {
        FileSystem* fs;
        Presence stat(const char* path) { return fs->presence(path); }
        bool remove(const char* path) { return fs->remove(path); }
        bool rename(const char* from, const char* to) { return fs->replace(from, to); }
    };

    std::string pathFor(const EngineDef* def, const std::string& name) const
    {
        return (typeFolders ? std::string(def->name) + "/" : std::string()) + name + ".patch";
    }

    const InstrumentTypeRegistry& registry;
    FileSystem* fs;
    bool typeFolders;
};

} // namespace

std::unique_ptr<PatchStore> makePatchStore(const InstrumentTypeRegistry& registry,
                                           FileSystem* fs,
                                           bool typeFolders)
{
    return std::make_unique<OpenPatchStore>(registry, fs, typeFolders);
}

// ------------------------------------------------------------- patch slots and bank (Patch step FX)
// Per part: the values (floats only, no macros) of every slot file of the part's engine. Built in the main thread, applied by
// a copy in the audio callback. The audio callback reads gLive (a plain pointer, one aligned word) and gBank, and the main
// thread changes gBank only with the audio interrupt disabled.

namespace
{
struct PartBank
{
    int typeId = -1;
    int nParams = 0;
    uint16_t valid = 0;    // bit n: slot n has a file of this engine (kSlot... rows of vals)
    uint8_t cur = 0;       // the slot open in the editor: the live patch holds its values
    int8_t from = -1;      // cur has no file yet: it began as a copy of this slot (-1: it has a file)
    bool applied = false;  // the FX replaced the live values, the edited ones wait in the last row of vals
    float* vals = nullptr; // gBankVals[part]: (kPatchBankSlots + 1) * nParams, the slot rows then the shadow row
};
PartBank gBank[kPatchBankParts];
OpenPatch* volatile gLive[kPatchBankParts];
bool gNamesUnreadable[kPatchBankParts]; // the names file of the part could not be read: it is not rewritten from empty names
// The rows of every bank, in the SDRAM (as the patch scratch), not on the heap: on the device a short heap gave an empty bank and the
// Patch FX did nothing without a word. Worst case, 16 slots plus the shadow row of the engine with the most parameters (kMaxParams):
// 3 x 17 x 84 floats = 17,136 B. A part always uses the first (kPatchBankSlots + 1) * nParams floats of its block.
OPEN_EXTERNAL_RAM float gBankVals[kPatchBankParts][size_t(kPatchBankSlots + 1) * size_t(kMaxParams)];

bool partOk(int part)
{
    return part >= 0 && part < kPatchBankParts;
}
} // namespace

void patchSlotName(int part, int slot, char (&out)[8])
{
    if (slot == kBackupSlot)
    {
        snprintf(out, sizeof(out), "%d-b", part);
    }
    else if (slot > 0)
    {
        snprintf(out, sizeof(out), "%d-%d", part, slot);
    }
    else
    {
        snprintf(out, sizeof(out), "%d", part);
    }
}

// ------------------------------------------------------------- slot names side file (<P>.names, see open_slotnames.h)

bool patchNamesRead(int part, PatchStore& workspace, SlotNames& out)
{
    out = SlotNames();
    if (!partOk(part))
    {
        return false;
    }
    char base[8];
    patchSlotName(part, 0, base);
    auto buf = tryBytes(kSlotNamesFileMax);
    if (!buf)
    {
        return false;
    }
    bool ioError = false;
    const size_t n = static_cast<OpenPatchStore&>(workspace).readRaw(std::string(base) + kSlotNamesExt, buf.get(), kSlotNamesFileMax, &ioError);
    gNamesUnreadable[part] = ioError; // a card error is not an empty file: the names in it are kept (patchNamesWrite)
    return n > 0 && decodeSlotNames(buf.get(), n, out);
}

bool patchNamesWrite(int part, PatchStore& workspace, const SlotNames& in)
{
    if (!partOk(part))
    {
        return false;
    }
    if (gNamesUnreadable[part])
    {
        return false; // the file is there and was not read: empty names must not replace it
    }
    OpenPatchStore& st = static_cast<OpenPatchStore&>(workspace);
    char base[8];
    patchSlotName(part, 0, base);
    const std::string file = std::string(base) + kSlotNamesExt;
    uint8_t probe[1];
    bool probeError = false;
    if (!anySlotName(in) && st.readRaw(file, probe, 1, &probeError) == 0)
    {
        return !probeError; // nothing to say and no file to correct: a project that never named a slot gets no side file
    }
    auto buf = tryBytes(kSlotNamesFileMax); // not on the stack: the audio interrupt runs on the same one
    if (!buf)
    {
        return false;
    }
    const size_t n = encodeSlotNames(in, buf.get(), kSlotNamesFileMax);
    return n > 0 && st.writeRaw(file, buf.get(), n);
}

bool patchNamesPersist(int part, PatchStore& workspace, SlotNames& names)
{
    if (!partOk(part))
    {
        return false;
    }
    if (gNamesUnreadable[part])
    {
        std::unique_ptr<SlotNames> card(new (std::nothrow) SlotNames()); // 512 B, off the shared stack
        if (!card)
        {
            return true; // heap short: still unread, the file stays as it is
        }
        patchNamesRead(part, workspace, *card); // clears the flag when the file reads (or is not there), sets it again on a card error
        if (gNamesUnreadable[part])
        {
            return true; // still unread: the file stays as it is, nothing is written from an empty table
        }
        for (int slot = 0; slot < kSlotNameSlots; slot++)
        {
            if (!names.name[slot][0])
            {
                std::memcpy(names.name[slot], card->name[slot], sizeof(names.name[slot]));
            }
        }
    }
    return patchNamesWrite(part, workspace, names);
}

void patchBankSetLive(int part, Patch* patch)
{
    if (partOk(part))
    {
        OpenPatch* const live = static_cast<OpenPatch*>(patch);
        if (gLive[part] != live)
        {
            gBank[part].applied = false; // the values the FX set aside belong to the patch that goes
        }
        gLive[part] = live;
    }
}

void patchBankPreload(int part, Patch& patch, PatchStore& workspace)
{
    if (!partOk(part))
    {
        return;
    }
    OpenPatch& op = static_cast<OpenPatch&>(patch);
    OpenPatchStore& st = static_cast<OpenPatchStore&>(workspace);
    const EngineDef* def = op.getDef();
    const int n = op.paramCount();
    OPEN_LOCK_AUDIO();
    gBank[part] = PartBank(); // retire the old bank: its rows are rewritten below and the audio callback must not read them meanwhile
    OPEN_UNLOCK_AUDIO();
    PartBank nb;
    nb.typeId = op.typeId();
    nb.nParams = n;
    if (def && n > 0 && n <= kMaxParams)
    {
        nb.vals = gBankVals[part];
    }
    if (nb.vals)
    {
        ScratchLease sc;
        for (int i = 0; sc && i < kPatchBankSlots; i++) // no scratch: an empty bank, the FX does nothing
        {
            char name[8];
            patchSlotName(part, i, name);
            new (&sc.data()) PatchData(); // fresh in place: `data = PatchData()` put a 1.6 KB temporary on the stack
            if (st.readPatchFile(def, nb.typeId, name, sc.buf(), sc.data()) == OpenPatchStore::FileRead::kOk)
            {
                op.valuesFromData(sc.data(), nb.vals + size_t(i) * size_t(n));
                nb.valid |= uint16_t(1u << i);
            }
        }
    }
    OPEN_LOCK_AUDIO();
    gBank[part] = nb;
    OPEN_UNLOCK_AUDIO();
}

void patchBankToSlotZero(int part)
{
    if (!partOk(part))
    {
        return;
    }
    OPEN_LOCK_AUDIO();
    gBank[part].cur = 0;
    gBank[part].from = -1;
    gBank[part].applied = false; // the project's own values are loaded next, the shadow row is stale
    OPEN_UNLOCK_AUDIO();
}

int patchBankCurrent(int part)
{
    return partOk(part) ? gBank[part].cur : 0;
}

namespace
{
// Bank usable with the live patch of the part (same engine and size). Main thread or audio callback.
inline bool bankMatches(const PartBank& b, const OpenPatch* p)
{
    return p && b.vals && b.typeId == p->typeId() && b.nParams == p->paramCount();
}
} // namespace

namespace
{
// The FX replaced the live values of the part: the edited ones (the shadow row) go back. Audio interrupts disabled by the caller.
void bankRestoreLocked(PartBank& b, OpenPatch* p)
{
    if (b.applied && bankMatches(b, p))
    {
        p->setValuesRaw(b.vals + size_t(kPatchBankSlots) * size_t(b.nParams));
    }
    b.applied = false;
}

// The part whose live patch is `p`, -1 for any other patch
int liveOwner(const OpenPatch* p)
{
    for (int part = 0; part < kPatchBankParts; part++)
    {
        if (gLive[part] == p)
        {
            return part;
        }
    }
    return -1;
}

// The row of the open slot is what was written to its file (the values as loading that file gives them). Audio interrupts disabled.
void bankPublishLocked(PartBank& b, const OpenPatch& p, const PatchData& saved)
{
    if (bankMatches(b, &p))
    {
        p.valuesFromData(saved, b.vals + size_t(b.cur) * size_t(b.nParams));
        b.valid |= uint16_t(1u << b.cur);
        b.from = -1;
    }
}
} // namespace

void patchBankRestore(int part)
{
    if (!partOk(part))
    {
        return;
    }
    OPEN_LOCK_AUDIO();
    bankRestoreLocked(gBank[part], gLive[part]);
    OPEN_UNLOCK_AUDIO();
}

void patchBankBeforeEdit(const OpenPatch* p)
{
    const int part = liveOwner(p);
    if (part >= 0)
    {
        bankRestoreLocked(gBank[part], gLive[part]);
    }
}

float patchBankEditedValue(const OpenPatch* p, const float* live, int id)
{
    const int part = liveOwner(p);
    if (part < 0)
    {
        return live[id];
    }
    const PartBank& b = gBank[part];
    const volatile bool& applied = b.applied; // the audio callback flips it
    for (;;)
    {
        const bool was = applied;
        const float v = was && bankMatches(b, p) ? b.vals[size_t(kPatchBankSlots) * size_t(b.nParams) + size_t(id)] : live[id];
        if (applied == was) // one callback runs to its end between two reads, it cannot flip the flag twice unseen
        {
            return v;
        }
    }
}

void patchBankExportLocked(const OpenPatch& p, PatchData& out)
{
    const int part = liveOwner(&p);
    if (part >= 0)
    {
        bankRestoreLocked(gBank[part], gLive[part]);
    }
    p.exportData(out);
}

void patchBankValuesReplaced(const OpenPatch& p)
{
    const int part = liveOwner(&p);
    if (part >= 0)
    {
        gBank[part].applied = false;
    }
}

void patchBankPublish(int part, const OpenPatch& p, const PatchData& saved)
{
    if (!partOk(part))
    {
        return;
    }
    OPEN_LOCK_AUDIO();
    bankPublishLocked(gBank[part], p, saved);
    OPEN_UNLOCK_AUDIO();
}

bool patchSaveOpenSlot(int part, Patch& live, PatchStore& workspace)
{
    if (!partOk(part))
    {
        return false;
    }
    char name[8];
    patchSlotName(part, gBank[part].cur, name);
    return static_cast<OpenPatchStore&>(workspace).saveSnapshot(static_cast<OpenPatch&>(live), name, part);
}

SlotSelect patchBankSelect(int part, int slot, Patch& patch, PatchStore& workspace)
{
    if (!partOk(part) || slot < 0 || slot >= kPatchBankSlots)
    {
        return SlotSelect::kError;
    }
    PartBank& b = gBank[part];
    if (slot == b.cur)
    {
        return SlotSelect::kSame;
    }
    OpenPatch& op = static_cast<OpenPatch&>(patch);
    OpenPatchStore& st = static_cast<OpenPatchStore&>(workspace);
    const EngineDef* def = op.getDef();
    if (!def)
    {
        return SlotSelect::kError;
    }
    char name[8];
    patchSlotName(part, slot, name);
    ScratchLease sc;
    if (!sc)
    {
        return SlotSelect::kError;
    }
    const auto state = st.readPatchFile(def, op.typeId(), name, sc.buf(), sc.data());
    if (state == OpenPatchStore::FileRead::kOther)
    {
        return SlotSelect::kOtherEngine;
    }
    if (state == OpenPatchStore::FileRead::kError)
    {
        return SlotSelect::kError;
    }
    const int8_t origin = b.from >= 0 ? b.from : int8_t(b.cur); // the file a pending copy began from
    // The new slot takes over in one audio-free window. The Patch FX may have run during the file read: its replaced values are not the
    // values of either slot. A loaded file replaces every value (nothing is left to restore), a copy keeps the edited values of the old
    // slot, so the shadow goes back.
    OPEN_LOCK_AUDIO();
    if (state == OpenPatchStore::FileRead::kOk)
    {
        op.importData(sc.data());
        b.applied = false;
    }
    else
    {
        bankRestoreLocked(b, &op);
    }
    b.cur = uint8_t(slot);
    b.from = state == OpenPatchStore::FileRead::kOk ? int8_t(-1) : origin;
    OPEN_UNLOCK_AUDIO();
    return state == OpenPatchStore::FileRead::kOk ? SlotSelect::kLoaded : SlotSelect::kCopied;
}

bool patchBankRevert(int part, Patch& patch, PatchStore& workspace)
{
    if (!partOk(part))
    {
        return false;
    }
    PartBank& b = gBank[part];
    OpenPatch& op = static_cast<OpenPatch&>(patch);
    OpenPatchStore& st = static_cast<OpenPatchStore&>(workspace);
    const EngineDef* def = op.getDef();
    if (!def)
    {
        return false;
    }
    char name[8];
    patchSlotName(part, b.from >= 0 ? b.from : b.cur, name);
    ScratchLease sc;
    if (!sc || st.readPatchFile(def, op.typeId(), name, sc.buf(), sc.data()) != OpenPatchStore::FileRead::kOk)
    {
        return false;
    }
    OPEN_LOCK_AUDIO();
    op.importData(sc.data());
    b.applied = false; // the file's values are the open slot's now, whatever the FX had set aside
    OPEN_UNLOCK_AUDIO();
    return true;
}

namespace
{
enum class PartFile
{
    kOk,       // read and decoded into the scratch data
    kMissing,  // the card says there is no such file
    kUnusable, // there, but damaged, bigger than a patch file may be or of another engine than `typeId`
    kError,    // the card failed: the file may be there
};
// A file of the part in the Workspace ("P.patch", "P-b.patch"), read into the scratch buffer and decoded into the scratch data. `size`
// is the length of the file when it is kOk.
PartFile readPartFile(OpenPatchStore& st, int part, int slot, int typeId, ScratchLease& sc, size_t& size)
{
    char base[8];
    patchSlotName(part, slot, base);
    bool ioError = false;
    const size_t n = st.readRaw(std::string(base) + ".patch", sc.buf(), kPatchFileMax + 1, &ioError);
    size = 0;
    if (ioError)
    {
        return PartFile::kError;
    }
    if (n == 0)
    {
        return PartFile::kMissing; // (an empty file is as good as none)
    }
    if (n > kPatchFileMax || !decodePatch(sc.buf(), n, sc.data()) || typeForEngineId(sc.data().engine) != typeId)
    {
        return PartFile::kUnusable;
    }
    size = n;
    return PartFile::kOk;
}
} // namespace

Backup patchBackupSave(int part, Patch& live, PatchStore& workspace)
{
    if (!partOk(part))
    {
        return Backup::kFailed;
    }
    OpenPatchStore& st = static_cast<OpenPatchStore&>(workspace);
    const OpenPatch& op = static_cast<OpenPatch&>(live);
    const int typeId = op.typeId();
    ScratchLease sc;
    if (!sc)
    {
        return Backup::kFailed;
    }
    size_t n = 0;
    switch (readPartFile(st, part, 0, typeId, sc, n))
    {
        case PartFile::kOk: break;
        case PartFile::kError: return Backup::kFailed; // slot 0 may hold a patch that the next Init write would replace
        default: return Backup::kSkipped;              // no file, or none of this engine: there is nothing to keep
    }
    // An untouched Init is not worth a backup: it would push out the patch of an engine the part had before (A -> B -> C -> A).
    // Untouched = the very bytes a new patch of the engine is written as. The file is at most half the buffer then, the new patch
    // is encoded into the other half. The Init data comes from the engine definition: no second patch (its metadata would be a heap
    // allocation that fails when the heap is short, with the old engine still live).
    constexpr size_t kHalf = kPatchFileMax / 2;
    if (n <= kHalf && op.getDef())
    {
        new (&sc.data()) PatchData();
        initPatchData(*op.getDef(), sc.data());
        const size_t m = encodePatch(sc.data(), sc.buf() + kHalf, kHalf);
        if (m == n && std::memcmp(sc.buf(), sc.buf() + kHalf, n) == 0)
        {
            return Backup::kSkipped;
        }
    }
    char base[8];
    patchSlotName(part, kBackupSlot, base);
    return st.writeRaw(std::string(base) + ".patch", sc.buf(), n) ? Backup::kSaved : Backup::kFailed;
}

bool patchBackupRestore(int part, Patch& live, PatchStore& workspace)
{
    if (!partOk(part))
    {
        return false;
    }
    OpenPatchStore& st = static_cast<OpenPatchStore&>(workspace);
    OpenPatch& op = static_cast<OpenPatch&>(live);
    ScratchLease sc;
    if (!sc)
    {
        return false;
    }
    size_t n = 0;
    char base[8];
    patchSlotName(part, 0, base);
    // A backup that is missing, damaged or of another engine is the normal "nothing to restore". One the card could not read, or slot 0
    // that could not be written, ends the same way (Init) but the backup file is not touched, a later change of engine tries again.
    if (readPartFile(st, part, kBackupSlot, op.typeId(), sc, n) != PartFile::kOk || !st.writeRaw(std::string(base) + ".patch", sc.buf(), n))
    {
        return false;
    }
    OPEN_LOCK_AUDIO();
    op.importData(sc.data());
    gBank[part].applied = false;
    bankPublishLocked(gBank[part], op, sc.data());
    OPEN_UNLOCK_AUDIO();
    return true;
}

bool patchBankApply(int part, int slot)
{
    if (!partOk(part) || slot < 0 || slot >= kPatchBankSlots)
    {
        return false;
    }
    PartBank& b = gBank[part];
    OpenPatch* p = gLive[part];
    if (!bankMatches(b, p))
    {
        return false;
    }
    const size_t n = size_t(b.nParams);
    float* shadow = b.vals + size_t(kPatchBankSlots) * n;
    if (slot == b.cur)
    {
        if (b.applied)
        {
            p->setValuesRaw(shadow); // back to the edited values of the open slot
            b.applied = false;
        }
        return true;
    }
    if (!(b.valid & (1u << slot)))
    {
        return false;
    }
    if (!b.applied)
    {
        p->getValuesRaw(shadow); // keep the edited values of the open slot
        b.applied = true;
    }
    p->setValuesRaw(b.vals + size_t(slot) * n);
    return true;
}

bool patchStale(const Patch& patch)
{
    const OpenPatch* op = static_cast<const OpenPatch*>(&patch);   // every patch of the shim is one (no RTTI on the device)
    const EngineDef* d = op ? op->getDef() : nullptr;
    return d && engineOf(d) && op->paramCount() != d->paramCount;
}

} // namespace open

} // namespace polysynth
