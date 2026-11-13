// Synth automation of the open shim: 4 slots per patch, each Off, an LFO or an envelope on one engine parameter or one user macro.
// Config (ModSlot, open_patchdata.h) lives in the OpenPatch and in .patch field 64, the run time state (ModState) in the instrument.
// Everything here is block rate and per part, never per voice: a modulated value goes through SynthParams::end like a macro, so the
// engines' serial / moving caches keep working. Header only and pure (no PolySynth, no core include), host tested
// (synth-open/tools/host_smoke_test.cpp). Rates and the time law are the sample automation page's (core/AudioEngine
// tempoSyncRatesOthers, core/source/mtStructs.h ATTACK_MAX / ENVELOPE_MICRO_*), copied so this header builds alone.
#pragma once

#include "open_engine.h"
#include "open_patchdata.h"

#include <cstdint>
#include <cstring>

namespace polysynth
{
class Patch;
}

namespace polysynth::open
{

constexpr float kModBlockMs = 128.0f * 1000.0f / 44117.647f;   // one audio block, 2.9013 ms
constexpr int kModMacros = 6;                                  // M1..M6
constexpr int kModPickMax = kMaxParams + kModMacros;           // picker entries at most
constexpr int16_t kModPickMacro = 0x100;                       // picker code of macro m: kModPickMacro + m, a param is its index

// Steps of the period per speed, 128 steps .. 1/64 step (tempoSyncRatesOthers)
constexpr float kModRate[kLfoSpeeds] = { 128,    96,       64,    48,       32,     24,       16,      12,       8,        6,
                                         4,      3,        2,     1.5,      1,      0.75,     0.5,     0.375,    0.333333, 0.25,
                                         0.1875, 0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125, 0.020833, 0.015625 };

// LFO period in ms at a tempo: one step is a 16th (15000 / bpm ms)
inline float modLfoPeriodMs(float bpm, int speed)
{
    if (!(bpm > 1.0f))
    {
        bpm = 120.0f;
    }
    return 15000.0f / bpm * kModRate[speed < 0 ? 0 : (speed >= kLfoSpeeds ? kLfoSpeeds - 1 : speed)];
}

// Envelope time law of the sample page: 190 steps, 0..1 s in 10 ms steps, then 1..10 s in 100 ms steps
constexpr int kModTimeSteps = 190;
inline int modTimeToStep(int ms)
{
    ms = ms < 0 ? 0 : (ms > kModTimeMax ? kModTimeMax : ms);
    return ms <= 1000 ? (ms + 5) / 10 : 100 + (ms - 1000 + 50) / 100;
}
inline int modStepToTime(int step)
{
    step = step < 0 ? 0 : (step > kModTimeSteps ? kModTimeSteps : step);
    return step <= 100 ? step * 10 : 1000 + (step - 100) * 100;
}

// LFO shape on phase p in [0, 1), bipolar -1..1. `held` is the random sample and hold value.
inline float modShape(int shape, float p, float held)
{
    switch (shape)
    {
        case 0: return 1.0f - 2.0f * p;                                      // Rev Saw
        case 1: return 2.0f * p - 1.0f;                                      // Saw
        case 2: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f); // Triangle, starts at 0 rising
        case 3: return p < 0.5f ? 1.0f : -1.0f;                              // Square
        default: return held;                                                // Random
    }
}

inline uint32_t modRandom(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline float modRandomValue(uint32_t& s) { return float(modRandom(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

enum class EnvStage : uint8_t
{
    kIdle,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
};

// ---- the configuration the render path reads: macros and automation slots of one patch, compact and trivial (no constructors, so a table of
// them may sit in the SDRAM). A patch keeps one of its own (the open slot's) and the Patch FX bank one per slot file. What the render path
// reads is the one that is live (OpenPatch::liveCfg): the Patch FX changes the pointer, never the open slot's own data.
struct MacroRun
{
    uint8_t bipolar;
    uint8_t count;                       // live targets, the first `count` entries
    int8_t param[kMacroTargets];         // engine parameter index (0..kMaxParams - 1)
    float amount[kMacroTargets];
};

struct ModRun
{
    ModType type;
    ModKind kind;
    uint8_t shape;
    uint8_t speed;
    int16_t dest;                        // run time destination, -1 none (ModSlot::dest)
    int8_t amount;
    uint8_t sustain;
    uint16_t attack;
    uint16_t decay;
    uint16_t release;
};

struct SlotCfg
{
    MacroRun macro[kPatchMacros];        // M1..M6, Velocity, Pressure
    ModRun mod[kModSlots];
    uint8_t modOn;                       // some slot is not Off
    uint8_t macroScale;                  // macro depth mode of the patch file: 1 Scale (headroom), 0 Add (range)
};

// Run time state of one slot (instrument, internal RAM, touched every block)
struct ModState
{
    float phase = 0.0f;       // LFO phase [0, 1), envelope: the release slope per block
    float level = 0.0f;       // envelope level 0..1, LFO: the random sample and hold value
    float out = 0.0f;         // this block's contribution: amount * shape, -1..1 (0 when Off or no destination)
    uint32_t rng = 0x9E3779B9u;
    ModType type = ModType::kOff; // the type the state belongs to: a change of type in the editor restarts the slot
    EnvStage stage = EnvStage::kIdle;
};

inline void modReset(ModState& st, int slot)
{
    st = ModState{};
    st.rng = 0x9E3779B9u + uint32_t(slot) * 0x85EBCA6Bu;
}

// The editor changed the type of a slot: its state starts over for the new type
template <class C> // ModSlot or ModRun: the run time fields have the same names
inline void modSync(const C& c, ModState& st)
{
    if (c.type != st.type)
    {
        const uint32_t rng = st.rng;
        st = ModState{};
        st.rng = rng;
        st.type = c.type;
        if (c.type == ModType::kLfo)
        {
            st.level = modRandomValue(st.rng);
        }
    }
}

// One block of one slot: writes st.out for this block, then moves on
template <class C> // ModSlot or ModRun: the run time fields have the same names
inline void modStep(const C& c, ModState& st, float bpm)
{
    modSync(c, st);
    float v = 0.0f;
    if (c.type == ModType::kLfo)
    {
        v = modShape(c.shape, st.phase, st.level);
        st.phase += kModBlockMs / modLfoPeriodMs(bpm, c.speed);
        if (st.phase >= 1.0f)
        {
            st.phase -= float(int(st.phase));
            st.level = modRandomValue(st.rng);
        }
    }
    else if (c.type == ModType::kEnv)
    {
        const float sus = float(c.sustain > 100 ? 100 : c.sustain) * 0.01f;
        switch (st.stage)
        {
            case EnvStage::kAttack:
                st.level += c.attack ? kModBlockMs / float(c.attack) : 1.0f;
                if (st.level >= 1.0f)
                {
                    st.level = 1.0f;
                    st.stage = EnvStage::kDecay;
                }
                break;
            case EnvStage::kDecay:
                st.level -= c.decay ? (1.0f - sus) * kModBlockMs / float(c.decay) : 1.0f;
                if (st.level <= sus)
                {
                    st.level = sus;
                    st.stage = EnvStage::kSustain;
                }
                break;
            case EnvStage::kSustain: st.level = sus; break;
            case EnvStage::kRelease:
                st.level -= st.phase;
                if (st.level <= 0.0f)
                {
                    st.level = 0.0f;
                    st.stage = EnvStage::kIdle;
                }
                break;
            default: break;
        }
        v = st.level;
    }
    st.out = c.dest >= 0 ? v * float(c.amount) * 0.01f : 0.0f;
}

// A note starts. `afterSilence`: no voice of the part was sounding. LFOs restart their phase only then, envelopes restart the attack on
// every note from where they are (no click). After silence an envelope starts from 0.
template <class C> // ModSlot or ModRun: the run time fields have the same names
inline void modNoteOn(const C& c, ModState& st, bool afterSilence)
{
    modSync(c, st);
    if (c.type == ModType::kLfo && afterSilence)
    {
        st.phase = 0.0f;
        st.level = modRandomValue(st.rng);
        st.out = c.dest >= 0 ? modShape(c.shape, 0.0f, st.level) * float(c.amount) * 0.01f : 0.0f;
    }
    else if (c.type == ModType::kEnv)
    {
        if (afterSilence)
        {
            st.level = 0.0f;
        }
        st.stage = EnvStage::kAttack;
    }
}

// The last held voice of the part was released: envelopes go to release, which takes the Release time from the level they are at
template <class C> // ModSlot or ModRun: the run time fields have the same names
inline void modGateOff(const C& c, ModState& st)
{
    if (c.type == ModType::kEnv && c.type == st.type && st.stage != EnvStage::kIdle && st.stage != EnvStage::kRelease)
    {
        st.stage = EnvStage::kRelease;
        st.phase = c.release ? st.level * kModBlockMs / float(c.release) : 1.0f;
    }
}

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

// Picker order: wave group (engine order), Macro 1..6, every other eligible param. Returns the count.
inline int modPicker(const EngineDef* def, int16_t* out)
{
    int n = 0;
    const int count = def ? (def->paramCount < kMaxParams ? def->paramCount : kMaxParams) : 0;
    for (int i = 0; i < count; i++)
    {
        if (modEligible(def->params[i]) && isWaveParam(def->params[i].name))
        {
            out[n++] = int16_t(i);
        }
    }
    for (int m = 0; m < kModMacros; m++)
    {
        out[n++] = int16_t(kModPickMacro + m);
    }
    for (int i = 0; i < count; i++)
    {
        if (modEligible(def->params[i]) && !isWaveParam(def->params[i].name))
        {
            out[n++] = int16_t(i);
        }
    }
    return n;
}

// The picker code of a slot's resolved destination, -1 when none
inline int modPickCode(const ModSlot& s)
{
    return s.dest < 0 ? -1 : (s.kind == ModKind::kMacro ? kModPickMacro + s.dest : s.dest);
}

// Sets the identity of a slot from a picker code (the editor picked a destination)
inline void modSetDest(const EngineDef& def, ModSlot& s, int code)
{
    std::memset(s.name, 0, sizeof(s.name));
    s.nth = 0;
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
    if (uint8_t(s.kind) > uint8_t(ModKind::kMacro))
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

#ifdef OPEN_HOST_TEST
// Host test hooks (open_synth.cpp): the params the engine got last block and a slot's output, of the latest synth's part
const ot::audio::SynthParams* testParams(int part);
float testModOut(int part, int slot);
#endif

} // namespace polysynth::open
