// A built-in style synth engine (an open::EngineDef plus its ot::audio::ISynthEngine) as a PLUG_KIND_SYNTH plugin. MIT licence,
// see synth-open/plug/README.md. One plugin source is three lines:
//
//   #include "synth_plug.h"
//   namespace polysynth::open { extern const EngineDef kJu6Def; }
//   PLUG_SYNTH_PLUGIN("JU6", polysynth::open::kJu6Def, 7, 0x81)   // name, definition, stored number, .patch engine id
//
// The engine and definition sources are linked as they are (engine_ju6.cpp, ot-synths/ju6/ju6.cpp), built with the firmware's
// own flags so the plugin sounds bit identical to the built-in engine. plug_ext_synth is filled by init() (the definition is in
// another translation unit, a constant initializer would be a static constructor): it lives in the rw segment.
#pragma once

#include "plug_abi.h"
#include "open_engine.h"

#include <stddef.h>

extern "C" const plug_host* plug_rt_host;

// The descriptor's author line: a plugin of an engine that is not OpenTracker's defines its own before the include
#ifndef PLUG_SYNTH_AUTHOR
#define PLUG_SYNTH_AUTHOR "OpenTracker synths (MIT)"
#endif   // plug_rt.c

namespace plugsynth
{
using namespace ot::audio;
using polysynth::open::EngineDef;

static_assert(sizeof(polysynth::open::ParamDef) == sizeof(plug_synth_param), "ParamDef is plug_synth_param");
static_assert(sizeof(polysynth::open::SectionDef) == sizeof(plug_synth_section), "SectionDef is plug_synth_section");
static_assert(sizeof(polysynth::open::MacroDef) == sizeof(plug_synth_macro), "MacroDef is plug_synth_macro");
static_assert(offsetof(polysynth::open::ParamDef, enumNames) == offsetof(plug_synth_param, enum_names), "ParamDef layout");
static_assert(offsetof(polysynth::open::MacroDef, targets) == offsetof(plug_synth_macro, targets), "MacroDef layout");

inline ISynthEngine* g_eng;
inline plug_ext_synth g_ext;

inline void prepare(void* sc, const void* p) { g_eng->prepare(*static_cast<SynthSlotScratch*>(sc), *static_cast<const SynthParams*>(p)); }
inline void slotReset(void* sc) { g_eng->slotReset(*static_cast<SynthSlotScratch*>(sc)); }
inline void setTempo(float bpm) { g_eng->setTempo(bpm); }
inline void noteOn(void* v, const void* p, const void* n)
{
    g_eng->noteOn(*static_cast<SynthVoice*>(v), *static_cast<const SynthParams*>(p), *static_cast<const SynthNote*>(n));
}
inline void noteOff(void* v, const void* p) { g_eng->noteOff(*static_cast<SynthVoice*>(v), *static_cast<const SynthParams*>(p)); }
inline uint32_t render(void* v, const void* p, float g0, float gInc, float* l, float* r)
{
    return g_eng->render(*static_cast<SynthVoice*>(v), *static_cast<const SynthParams*>(p), g0, gInc, l, r);
}
inline uint32_t slotPost(int32_t slot, void* sc, const void* p, float* l, float* r)
{
    return g_eng->slotPost(slot, *static_cast<SynthSlotScratch*>(sc), *static_cast<const SynthParams*>(p), l, r);
}
inline uint32_t sendTap(int32_t slot, float* d, float* rv, float gd, float gr) { return g_eng->setSendTap(slot, d, rv, gd, gr); }

inline int32_t init(void*, const plug_host*) { return g_eng ? 0 : -1; }
inline void process(void*, float* const*, uint32_t, uint32_t) {}   // a synth renders through PLUG_EXT_SYNTH only
inline const void* query(uint32_t id) { return id == PLUG_EXT_SYNTH && g_eng ? &g_ext : nullptr; }

// Fills the extension from the definition. Called by plug_entry: the host asks for it right after.
inline void fill(const char* name, const EngineDef& d, int32_t stored, int32_t patchEngine, uint32_t workBytes = 0,
                 void (*setWork)(void*, uint32_t) = nullptr)
{
    g_eng = d.engine();
    plug_ext_synth& x = g_ext;
    x.struct_size = sizeof(plug_ext_synth);
    x.layout = PLUG_SYNTH_LAYOUT(sizeof(SynthVoice), sizeof(SynthParams), sizeof(SynthNote), sizeof(SynthSlotScratch));
    x.stored = stored;
    x.patch_engine = patchEngine;
    x.type_id = d.typeId;
    x.name = name;
    x.params = reinterpret_cast<const plug_synth_param*>(d.params);
    x.param_count = uint32_t(d.paramCount);
    x.sections = reinterpret_cast<const plug_synth_section*>(d.sections);
    x.section_count = uint32_t(d.sectionCount);
    x.macros = reinterpret_cast<const plug_synth_macro*>(d.macros);
    x.macro_count = uint32_t(d.macroCount);
    x.volume_param = d.volumeParam;
    x.max_voices = uint32_t(d.maxVoices);
    x.flags = d.slotFx ? PLUG_SYNTH_SLOT_FX : 0;
    x.prepare = prepare;
    x.slot_reset = slotReset;
    x.set_tempo = setTempo;
    x.note_on = noteOn;
    x.note_off = noteOff;
    x.render = render;
    x.slot_post = d.slotFx ? slotPost : nullptr;
    x.set_send_tap = sendTap;
    x.work_bytes = setWork ? workBytes : 0;
    x.set_work = setWork;
}
}   // namespace plugsynth

// The engines' virtual destructors put a deleting destructor in the vtable. Nothing deletes an engine (a static object): the
// operator it names is never called, it only has to link (the plain one is defined too, -Wsized-deallocation asks for the
// pair). WORK_BYTES and SET_WORK: the engine's working RAM from the host (PLUG_EXT_SYNTH work_bytes, set_work), 0 and
// nullptr for none.
#define PLUG_SYNTH_PLUGIN_WORK(NAME, DEF, STORED, PATCH_ENGINE, WORK_BYTES, SET_WORK)                                          \
    void operator delete(void*) noexcept {}                                                                                    \
    void operator delete(void*, unsigned int) noexcept {}                                                                      \
    namespace                                                                                                                  \
    {                                                                                                                          \
    PLUG_DESCRIPTOR const plug_desc kDesc = {                                                                                  \
        sizeof(plug_desc), PLUG_KIND_SYNTH, NAME, PLUG_SYNTH_AUTHOR, 0, nullptr, 8, plugsynth::init, nullptr,                  \
        nullptr,           plugsynth::process, plugsynth::query,                                                               \
    };                                                                                                                         \
    }                                                                                                                          \
    extern "C" PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)                                     \
    {                                                                                                                          \
        if ((abi >> 16) != PLUG_ABI_MAJOR || !host || !PLUG_HAS(host, plug_host, log2f)) return nullptr;                       \
        plug_rt_host = host;                                                                                                   \
        plugsynth::fill(NAME, DEF, STORED, PATCH_ENGINE, WORK_BYTES, SET_WORK);                                                \
        return &kDesc;                                                                                                         \
    }
#define PLUG_SYNTH_PLUGIN(NAME, DEF, STORED, PATCH_ENGINE) PLUG_SYNTH_PLUGIN_WORK(NAME, DEF, STORED, PATCH_ENGINE, 0, nullptr)
