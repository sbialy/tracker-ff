// Synth automation of the open shim: 4 slots per patch, each Off, an LFO or an envelope on one engine parameter or one user macro.
// Config (ModSlot, open_patchdata.h) lives in the OpenPatch and in .patch field 64, the run time state (ModState) in the instrument.
// Everything here is block rate and per part, never per voice: a modulated value goes through SynthParams::end like a macro, so the
// engines' serial / moving caches keep working. Header only and pure (no PolySynth, no core include), host tested
// (synth-open/tools/host_smoke_test.cpp). Rates and the time law are the sample automation page's (core/AudioEngine
// tempoSyncRatesOthers, core/source/mtStructs.h ATTACK_MAX / ENVELOPE_MICRO_*), copied so this header builds alone.
#pragma once

#include "open_engine.h"
#include "open_load.h"
#include "open_modrun.h"
#include "open_patchdata.h"

#include <cstdint>
#include <cstring>

namespace polysynth
{
class Patch;
}

namespace polysynth::open
{

constexpr int kModPickMax = kMaxParams + kModMacros + kModParts; // picker entries at most

// ---- destinations

// A param a slot may drive: continuous or stepped numbers, never an enum, a switch or an unused slot (min == max)
inline bool modEligible(const ParamDef& d)
{
    return d.fmt != Fmt::kEnum && d.fmt != Fmt::kOnOff && d.min != d.max && d.name && d.name[0];
}

inline char modLower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

// Wave and position like names, listed first: a word of the name starts with one of these (case-insensitive)
inline bool isWaveParam(const char* name)
{
    static const char* const kWords[] = { "shape", "wave", "pos", "pw", "timbre", "morph", "table", "duty", "xmod", "ymod" };
    for (const char* w = name; w && *w;)
    {
        while (*w && !((*w >= 'A' && *w <= 'Z') || (*w >= 'a' && *w <= 'z') || (*w >= '0' && *w <= '9')))
        {
            w++;
        }
        const char* e = w;
        while ((*e >= 'A' && *e <= 'Z') || (*e >= 'a' && *e <= 'z') || (*e >= '0' && *e <= '9'))
        {
            e++;
        }
        if (e > w)
        {
            for (const char* k : kWords)
            {
                int i = 0;
                while (k[i] && w + i < e && modLower(w[i]) == k[i])
                {
                    i++;
                }
                if (!k[i])
                {
                    return true;
                }
            }
        }
        w = e;
    }
    return false;
}

inline bool waveFileShape(const ParamDef& d) { return waveFileShape(d.name, d.fmt == Fmt::kInt, d.min, d.max); }

inline bool modNameIs(const char* a, const char* stored) { return a && std::strncmp(a, stored, kModNameMax) == 0; }

// Which occurrence of its name (as stored, cut to kModNameMax) param i is
inline int modOccurrence(const EngineDef& def, int i)
{
    int n = 0;
    for (int k = 0; k < i; k++)
    {
        n += modNameIs(def.params[k].name, def.params[i].name);
    }
    return n;
}

// Picker order: wave group (engine order), Macro 1..6, every other eligible param. Returns the count. noFiles: /Waves has no wave, the wave file
// parameters (waveFileShape) are left out like they are in the parameter list.
inline int modPicker(const EngineDef* def, int16_t* out, bool noFiles = false)
{
    int n = 0;
    const int count = def ? (def->paramCount < kMaxParams ? def->paramCount : kMaxParams) : 0;
    for (int i = 0; i < count; i++)
    {
        if (modEligible(def->params[i]) && isWaveParam(def->params[i].name) && !(noFiles && waveFileShape(def->params[i])))
        {
            out[n++] = int16_t(i);
        }
    }
    for (int m = 0; m < kModMacros; m++)
    {
        out[n++] = int16_t(kModPickMacro + m);
    }
    for (int p = 0; p < kModParts; p++)
    {
        out[n++] = int16_t(kModPickPart + p);
    }
    for (int i = 0; i < count; i++)
    {
        if (modEligible(def->params[i]) && !isWaveParam(def->params[i].name) && !(noFiles && waveFileShape(def->params[i])))
        {
            out[n++] = int16_t(i);
        }
    }
    return n;
}

// The two flags of SlotCfg from the resolved slots: modOn (some param or macro slot is not Off, what effective() and the refresh skip read)
// and partOn (bit s: slot s is on with a part destination). A part slot never counts into modOn, so a patch with only part slots keeps the
// parameter refresh skip and an old patch gets exactly the flags it had.
inline void modFlags(const ModSlot* slots, uint8_t& modOn, uint8_t& partOn)
{
    modOn = 0;
    partOn = 0;
    for (int i = 0; i < kModSlots; i++)
    {
        if (slots[i].type == ModType::kOff)
        {
            continue;
        }
        if (slots[i].kind == ModKind::kPart)
        {
            partOn = uint8_t(partOn | (slots[i].dest >= 0 ? 1u << i : 0u));
        }
        else
        {
            modOn = 1;
        }
    }
}

// Sets the identity of a slot from a picker code (the editor picked a destination)
inline void modSetDest(const EngineDef& def, ModSlot& s, int code)
{
    std::memset(s.name, 0, sizeof(s.name));
    s.nth = 0;
    if (code >= kModPickPart)
    {
        s.kind = ModKind::kPart;
        s.index = int16_t(code - kModPickPart);
        s.dest = s.index;
        return;
    }
    if (code >= kModPickMacro)
    {
        s.kind = ModKind::kMacro;
        s.index = int16_t(code - kModPickMacro);
        s.dest = s.index;
        return;
    }
    s.kind = ModKind::kParam;
    s.index = int16_t(code);
    s.dest = int16_t(code);
    std::strncpy(s.name, def.params[code].name, kModNameMax);
    s.nth = uint8_t(modOccurrence(def, code));
}

// Resolution of a stored identity against the engine of the patch (import): index when name and occurrence still match, else the
// nth param of that name (a plugin update reordered its table), else -1 (unresolved: shown as "? name", drives nothing, written back
// as read). A placeholder engine (no params, plugin not attached) resolves nothing and keeps the slot as read.
inline int16_t modResolve(const EngineDef* def, const ModSlot& s)
{
    if (s.type == ModType::kOff)
    {
        return -1;
    }
    if (s.kind == ModKind::kMacro)
    {
        return s.index >= 0 && s.index < kModMacros ? s.index : -1;
    }
    if (s.kind == ModKind::kPart)
    {
        return s.index >= 0 && s.index < kModParts ? s.index : -1; // the part is there for every engine, a placeholder too
    }
    const int count = def ? (def->paramCount < kMaxParams ? def->paramCount : kMaxParams) : 0;
    if (count == 0 || s.kind != ModKind::kParam || !s.name[0])
    {
        return -1;
    }
    if (s.index >= 0 && s.index < count && modNameIs(def->params[s.index].name, s.name) && modOccurrence(*def, s.index) == s.nth)
    {
        return modEligible(def->params[s.index]) ? s.index : -1;
    }
    int seen = 0;
    for (int i = 0; i < count; i++)
    {
        if (modNameIs(def->params[i].name, s.name) && seen++ == s.nth)
        {
            return modEligible(def->params[i]) ? int16_t(i) : int16_t(-1);
        }
    }
    return -1;
}

// Range checks of a slot read from a file (import): out of range values go back to the editor's defaults
inline void modSanitize(ModSlot& s)
{
    const ModSlot d;
    if (uint8_t(s.type) >= kModTypes)
    {
        s = d;
        return;
    }
    if (uint8_t(s.kind) > uint8_t(ModKind::kPart))
    {
        s.type = ModType::kOff;
    }
    if (s.shape >= kLfoShapes)
    {
        s.shape = d.shape;
    }
    if (s.speed >= kLfoSpeeds)
    {
        s.speed = d.speed;
    }
    if (s.sustain > 100)
    {
        s.sustain = 100;
    }
    s.attack = uint16_t(modStepToTime(modTimeToStep(s.attack)));
    s.decay = uint16_t(modStepToTime(modTimeToStep(s.decay)));
    s.release = uint16_t(modStepToTime(modTimeToStep(s.release)));
    s.name[kModNameMax] = 0;
}

// The editor's access (open_cold.cpp, cold). `p` is a patch of this shim. modSlotSet needs audio interrupts disabled by the caller.
const ModSlot& modSlotOf(const Patch& p, int s);
void modSlotSet(Patch& p, int s, const ModSlot& v);
const EngineDef* modEngineOf(const Patch& p);
const char* modDestName(const Patch& p, int code); // the param's name or the macro's display name, "" for none

// Patch Vary (open_cold.cpp, cold, editor only). One loop over the patch's parameter array, ranges and curves from the engine definition,
// so it works for every engine including plugin engines. `percent` 1..100 is the share of each parameter's knob range a value may move
// (+- percent, clamped, in the knob curve), 100 draws every parameter anew inside its range. The volume parameter stays. A discrete
// parameter (enum, on/off) stays below 50 percent, from 50 it is drawn anew with a probability of percent / 100. `rng` is an xorshift32
// state (non zero). `prev` (patchParamCount values) gets the values before, for patchSetValues (the undo). Audio interrupts disabled by
// the caller, like every parameter write. Returns how many parameters changed.
int patchParamCount(const Patch& p);
int patchVary(Patch& p, int percent, uint32_t& rng, float* prev);
void patchSetValues(Patch& p, const float* values, int n);   // the undo: every value back through the normal setter (clamped)

#if defined(OPEN_HOST_TEST) || defined(UI_SHOT_HOST)
// Host test hooks (open_synth.cpp): the params the engine got last block and a slot's output, of the latest synth's part
const ot::audio::SynthParams* testParams(int part);
float testModOut(int part, int slot);
// Voice record `slot` of a part: false when it is not on. note, id (open::noteTag, 0 none) and the gate of the voice (synth_nna_sim.sh, host_smoke_test.cpp)
bool testVoice(int part, int slot, bool& gate, int& note, int& id);
#endif

} // namespace polysynth::open
