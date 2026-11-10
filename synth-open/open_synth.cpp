// Open PolySynth shim for MK66: Synth, Instrument, Patch, PatchStore and the
// registry factories, hosting OpenTracker's ISynthEngine (ACD) directly.
//
// Written only from the open headers in synth/include/PolySynth and OpenTracker's Apache-2.0
// engine interface (synth-open/ot). Nothing here is derived from libPolySynth/libPolyDSP.
//
// Voice load: all synth slots share ONE voice pool (default 4, max 8, as OpenTracker 2.2.4) and an
// ot::audio::Governor lowers the pool cap when the audio block load gets close to the block budget
// (load from Profiler::stopMeasuringAudio via open_load.h). Sample voices are never touched.
//
//
// Split: everything that does not run per audio block (value text, curves, registry factories and
// metadata, OpenPatch GUI API, patch store) is in open_cold.cpp, built at -Os. This file stays at -O2.
//
// Threading: tracker-ce runs every call into the synth (patch edits, MIDI events, voice count)
// inside DISABLE_AUDIO_INTERRUPTS, so the render (audio interrupt) never overlaps them and this
// file takes no locks. The governor runs in the audio interrupt too (end of the block).
#include "open_engine.h"
#include "open_load.h"
#include "open_patch.h"

#include "audio/cpu_meter.h"
#include "audio/governor.h"

#include "PolySynth/GridMapperStore.h"
#include "PolySynth/Instrument.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/Mixer.h"
#include "PolySynth/PatchPool.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/SampleStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/sdk/InstrumentType.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

#if OPEN_SLOT_FX
#define OPEN_SLOT_FX_TAIL fxTail
#else
#define OPEN_SLOT_FX_TAIL false
#endif

namespace polysynth
{
using open::EngineDef;
using open::kMaxParams;
using open::kMaxVoicesPerInstrument;
using open::kVelocityMacro;
using open::OpenPatch;

namespace
{

// Pitch bend range in semitones for full deflection (+-8192). The tracker's Micro-tune FX (-99..+99
// cents) sends the full 14 bit range, so 1 semitone makes the FX read in cents. Own choice.
constexpr float kBendRange = 1.0f;

} // namespace

// Values with macros M1..M6 and the automation slots applied (additive, scaled by the param range, clamped once).
void open::OpenPatch::effective(float* out, const float* modOut) const
{
    std::memcpy(out, values, sizeof(float) * count);
    float add[kMaxParams] = {};
    bool hit[kMaxParams] = {};
    float macroMod[open::kModMacros] = {};
    const open::SlotCfg& live = *cfg; // the open slot's macros and automation, or those of the slot the Patch FX has on
    if (modOut && live.modOn)
    {
        for (int s = 0; s < open::kModSlots; s++)
        {
            const open::ModRun& c = live.mod[s];
            if (c.type == open::ModType::kOff || c.dest < 0)
            {
                continue;
            }
            if (c.kind == open::ModKind::kMacro)
            {
                macroMod[c.dest] += modOut[s];
            }
            else if (c.dest < count)
            {
                const auto& p = metadata.params[c.dest];
                add[c.dest] += modOut[s] * (float(p.maxValue) - float(p.minValue));
                hit[c.dest] = true;
            }
        }
    }
    for (int m = 0; m < int(kUserMacroCount); m++)
    {
        const open::MacroRun& macro = live.macro[m];
        const float cc = float(macroCC[m]);
        float mv = macro.bipolar ? (cc - 63.0f) * (1.0f / 64.0f) : cc * (1.0f / 127.0f);
        if (m < open::kModMacros && macroMod[m] != 0.0f)
        {
            mv = std::clamp(mv + macroMod[m], macro.bipolar ? -1.0f : 0.0f, 1.0f); // as far as the knob goes
        }
        for (int t = 0; t < macro.count; t++) // the targets that name a parameter of this engine (SlotCfg is built with that filter)
        {
            const int id = macro.param[t];
            const auto& p = metadata.params[id];
            add[id] += macro.amount[t] * mv * (float(p.maxValue) - float(p.minValue));
            hit[id] = true;
        }
    }
    for (int i = 0; i < count; i++)
    {
        if (hit[i])
        {
            out[i] = std::clamp(out[i] + add[i],
                                float(metadata.params[i].minValue),
                                float(metadata.params[i].maxValue));
        }
    }
}

namespace
{

// -------------------------------------------------------------- instrument

struct Voice
{
    ot::audio::SynthVoice v; // v.fading: stolen by the pool, its last block fades the gain to 0
    bool on = false;
    float vel = 1.0f;
    float gain = 0.0f;
    uint32_t age = 0;
    uint32_t onCyc = 0; // live: cycles of the engine's noteOn, added to the first render to learn the part's note-on cost
};

// Note-on admission and the cost model (live blocks only, OpenSynth::admit). Cycles at 180 MHz.
constexpr uint32_t kCyclesPerUs = 180;
constexpr uint32_t admitCyclesFor(uint32_t permille) { return uint32_t(uint64_t(ot::audio::CpuMeter::kBudgetCycles) * permille / 1000u); }
// Seeds of the learned costs before a part played a note (device: PLK note-on about 150 us, a running voice about 32 us,
// ref/cpu-governor.md section 3): a fresh part is judged as a PLK part until its own numbers come in
constexpr uint32_t kSeedAttackCycles = 150 * kCyclesPerUs;
constexpr uint32_t kSeedVoiceCycles = 32 * kCyclesPerUs;
// A released victim quieter than this (amplitude envelope, about -48 dB) is freed at once instead of fading over a block
constexpr float kFastKillLevel = 1.0f / 256.0f;
constexpr int kPendingMax = 8;

// Steal order after "free": the quietest released voice, else the oldest.
bool betterVictim(const Voice& c, const Voice& best)
{
    const bool cRel = !c.v.gate, bRel = !best.v.gate;
    return cRel != bRel ? cRel : (cRel ? c.v.level < best.v.level : c.age > best.age);
}

class OpenInstrument;

// The shared voice pool as an instrument sees it (implemented by OpenSynth).
class VoiceHost
{
  public:
    virtual int cap() const = 0;      // global cap: budget lowered by the governor
    virtual int sounding() const = 0; // voices on and not fading, all slots
    virtual Voice* victim() = 0;      // best steal candidate over all slots, or nullptr
    virtual void onStop() = 0;        // transport Stop: governor back to the budget
    // Note-on admission (live only). False: the note-on was queued for the start of the next block (it is not started now).
    virtual bool admit(OpenInstrument& inst, uint8_t note, uint8_t velocity) = 0;
    virtual bool pendingOff(OpenInstrument& inst, uint8_t note) = 0;   // a queued note-on of this note gets its note-off too
    virtual void flushPending(OpenInstrument& inst) = 0;               // start the queued note-ons of this part now (Stop)
    virtual void dropPending(OpenInstrument& inst) = 0;                // forget them (the part's voices were cut)
    virtual ot::audio::CpuMeter::CycleFn clock() const = 0;            // live cycle counter, nullptr when not live
    virtual void noteStolen(bool fastKill) = 0;                        // counters
};

class OpenInstrument final : public Instrument
{
  public:
    OpenInstrument(int id, VoiceHost& host)
      : host(host)
      , id(id)
    {
    }

    void assignPatch(std::shared_ptr<Patch> newPatch) override
    {
        patch = std::static_pointer_cast<OpenPatch>(std::move(newPatch));
        const EngineDef* oldDef = def;
        def = patch ? patch->getDef() : nullptr;
        engine = engineOf(def); // nullptr for a missing engine's placeholder: the part is silent
        if (def != oldDef)
        {
            attackCyc = kSeedAttackCycles; // another engine: its costs are learned again
            voiceCyc = kSeedVoiceCycles;
        }
        cutAll();
        modResetAll();
        std::memset(scratch.bytes, 0, sizeof(scratch.bytes));
        if (engine)
        {
            engine->slotReset(scratch);
        }
#if OPEN_SLOT_FX
        fxTail = false;
#endif
        paramsValid = false;
    }

    // plug_synth.cpp detaches the plugin engine of def (open::engineGone): nothing may call into it from here on
    void engineGone(const EngineDef* d)
    {
        if (def == d && engine)
        {
            cutAll();
            engine = nullptr;
        }
    }

    // plug_synth.cpp starts the engine of def again (open::engineRestartLocked): no voice and no slot state of the old run is left
    void engineRestart(const EngineDef* d)
    {
        if (def == d && engine)
        {
            cutAll();
            std::memset(scratch.bytes, 0, sizeof(scratch.bytes));
            paramsValid = false;
        }
    }
    void engineRestarted(const EngineDef* d)
    {
        if (def == d && engine)
        {
            engine->slotReset(scratch);
        }
    }

    void connect(MidiSource& midiSource) override
    {
        callback = std::make_shared<MidiSource::MidiEventCallback>(
          [this](const MidiEvent& event, int) { onMidi(event); });
        midiSource.subscribeMidiEvents(id, callback);
    }

    void setMidiChannel(int newChannel) override { channel = newChannel; }
    int getMidiChannel() const override { return channel; }
    void setLocalMode(LocalMode mode) override { localMode = mode; }
    LocalMode getLocalMode() const override { return localMode; }
    int getVoiceCount() const override { return voiceCount; }
    void setVoiceCount(int count) override
    {
        voiceCount = std::clamp(count, 0, kMaxVoicesPerInstrument);
        if (voiceCount == 0)
        {
            cutAll();
        }
    }

    void setTempoProvider(const TempoProvider* provider) { tempo = provider; }
    std::span<Voice> voiceSpan() { return voices; }

    // Nothing to render this block: no engine or patch, no voices, nothing sounding and no slot effect tail. Also what
    // PolySynth::update asks (open::partIdle) to skip a whole part without allocating or sending blocks.
    bool idle()
    {
        if (!engine || !patch || voiceCount == 0 || (!anyActive() && !OPEN_SLOT_FX_TAIL))
        {
            paramsValid = false; // nothing sounds: next note starts with a clean ramp
            return true;
        }
        return false;
    }

    // Adds this instrument's voices into L / R (kAudioBufferSize frames).
    void render(float* L, float* R)
    {
        if (idle())
        {
            return;
        }
        // An engine with a slot effect (JU6's chorus) gets a private bus for its voices: the effect works on this slot
        // only, the result is added to the mix at the end. The effect's tail keeps the slot rendering after the last voice.
#if OPEN_SLOT_FX
        float busL[kAudioBufferSize];
        float busR[kAudioBufferSize];
        float* const outL = L;
        float* const outR = R;
        const bool slotFx = def && def->slotFx;
        if (slotFx)
        {
            std::fill_n(busL, kAudioBufferSize, 0.0f);
            std::fill_n(busR, kAudioBufferSize, 0.0f);
            L = busL;
            R = busR;
        }
#endif
        // the same tempo feeds the automation (before the params of the block are made) and the engine
        float bpmNow = 0.0f;
        if (tempo && *tempo)
        {
            const auto bpm = (*tempo)();
            if (bpm.has_value())
            {
                bpmNow = *bpm;
            }
        }
        modAdvance(bpmNow);
        refreshParams();
        if (bpmNow > 0.0f)
        {
            engine->setTempo(bpmNow);
        }
        engine->prepare(scratch, params);
        blockMoving |= params.moving;
        // PERC has no Volume: the voice gain is the note velocity (the engine divides it back out per hit)
        const bool hasVol = def->volumeParam >= 0;
        const float volMin = hasVol ? float(patch->getMetadata().params[def->volumeParam].minValue) : 0.0f;
        const float volMax = hasVol ? float(patch->getMetadata().params[def->volumeParam].maxValue) : 1.0f;
        const float velK = hasVol ? patch->velocityAmount() : 1.0f;
        const auto clk = host.clock(); // live: every voice render is timed to learn this part's costs (the audio is not touched)
        for (auto& v : voices)
        {
            if (!v.on)
            {
                continue;
            }
            const float vol = hasVol ? params.end[def->volumeParam] : 1.0f;
            const bool fade = v.v.fading;
            const float target =
              fade ? 0.0f : std::clamp(vol * (1.0f + velK * (v.vel - 1.0f)), volMin, volMax);
            v.v.bendStart = v.v.bend;
            v.v.bend = bend;
            const float g0 = v.gain;
            const float gInc = (target - g0) * (1.0f / float(kAudioBufferSize));
            v.gain = target;
            v.age++;
            const uint32_t t0 = clk ? clk() : 0;
            const bool alive = engine->render(v.v, params, g0, gInc, L, R);
            if (clk)
            {
                const uint32_t cyc = clk() - t0;
                learn(v, cyc);
                blockVoices++;
                blockRenderCyc += cyc;
            }
            if (!alive || fade)
            {
                v.on = false;
            }
        }
#if OPEN_SLOT_FX
        if (slotFx)
        {
            fxTail = engine->slotPost(id, scratch, params, busL, busR);
            for (int i = 0; i < kAudioBufferSize; i++)
            {
                outL[i] += busL[i];
                outR[i] += busR[i];
            }
        }
#endif
        std::memcpy(params.start, params.end, sizeof(float) * params.count);
        params.moving = false;
    }

    // Note-on admission: the learned cycles of a note-on of this part (over a running voice) and of one running voice
    uint32_t attackCost() const { return attackCyc; }
    uint32_t voiceCost() const { return voiceCyc; }
    // A queued note-on starts (OpenSynth, start of the next block) and its note-off when one came while it waited
    void startQueued(uint8_t note, uint8_t velocity, bool off)
    {
        if (!engine || !patch || voiceCount == 0)
        {
            return;
        }
        noteOnNow(note, velocity);
        if (off)
        {
            noteOff(note);
        }
    }
    // Whether a note-on now would take a new record of the shared pool (not mono legato on a held voice)
    bool takesPoolVoice() const
    {
        const int n = std::clamp(voiceCount, 1, def ? def->maxVoices : 1);
        return !(n == 1 && voices[0].on && !voices[0].v.fading);
    }
    bool owns(const Voice* v) const { return v >= voices && v < voices + kMaxVoicesPerInstrument; }
    // The block's timed voice renders, taken once per block by OpenSynth::blockDone
    void takeBlock(uint32_t& voicesOut, uint32_t& cycOut, uint32_t& movingOut)
    {
        voicesOut += blockVoices;
        cycOut += blockRenderCyc;
        movingOut += blockMoving;
        blockVoices = blockRenderCyc = 0;
        blockMoving = false;
    }

#ifdef OPEN_HOST_TEST
    const ot::audio::SynthParams& testParams() const { return params; }
    float testModOut(int s) const { return modSt[s].out; }
#endif

  private:
    // ---- automation (open_mod.h): one state per slot of the patch, advanced once per rendered block
    void modResetAll()
    {
        for (int s = 0; s < open::kModSlots; s++)
        {
            open::modReset(modSt[s], s);
            modOut[s] = 0.0f;
        }
        modSerial = patch ? patch->modSerial() : 0;
        cfgSerial = patch ? patch->cfgSerial() : 0;
    }

    // The Patch FX put another slot's macros and automation on (or took them off): keep the state of the slots whose type stays (an LFO
    // goes on from its phase, an envelope from its stage and level, so a fast rhythmic patch change neither retriggers nor clicks) and
    // start the slots whose type changes (modSync). A slot that went Off is cleared. An envelope that starts under a held note goes to
    // its attack, otherwise it would sit idle until the next note.
    __attribute__((noinline)) void modSwitch()
    {
        cfgSerial = patch->cfgSerial();
        const bool held = anyGate();
        for (int s = 0; s < open::kModSlots; s++)
        {
            const open::ModRun& c = patch->live().mod[s];
            open::modSync(c, modSt[s]);
            if (c.type == open::ModType::kEnv && held && modSt[s].stage == open::EnvStage::kIdle)
            {
                modSt[s].stage = open::EnvStage::kAttack;
            }
            modOut[s] = modSt[s].out;
        }
    }

    void modCheck()
    {
        if (patch->modSerial() != modSerial)
        {
            modResetAll(); // another file came in (slot select, load): its modulators start fresh
        }
        else if (patch->cfgSerial() != cfgSerial)
        {
            modSwitch();
        }
    }

    void modAdvance(float bpm)
    {
        modCheck();
        if (!patch->modActive())
        {
            return;
        }
        for (int s = 0; s < open::kModSlots; s++)
        {
            open::modStep(patch->live().mod[s], modSt[s], bpm);
            modOut[s] = modSt[s].out;
        }
    }

    bool anyGate() const
    {
        for (const auto& v : voices)
        {
            if (v.on && v.v.gate)
            {
                return true;
            }
        }
        return false;
    }

    // Note and gate events of the automation: once per MIDI event, not per block, kept out of line (the MIDI lambda inlines the rest)
    __attribute__((noinline)) void modNoteOnAll()
    {
        modCheck();
        const bool afterSilence = !anyActive();
        for (int s = 0; s < open::kModSlots; s++)
        {
            open::modNoteOn(patch->live().mod[s], modSt[s], afterSilence);
            modOut[s] = modSt[s].out;
        }
    }

    __attribute__((noinline)) void modGateEnded()
    {
        if (patch && patch->modActive() && !anyGate())
        {
            modCheck();
            for (int s = 0; s < open::kModSlots; s++)
            {
                open::modGateOff(patch->live().mod[s], modSt[s]);
            }
        }
    }

    bool anyActive() const
    {
        for (const auto& v : voices)
        {
            if (v.on)
            {
                return true;
            }
        }
        return false;
    }

    void cutAll()
    {
        host.dropPending(*this);
        for (auto& v : voices)
        {
            v.on = false;
        }
#if OPEN_SLOT_FX
        fxTail = false;
#endif
    }

    void refreshParams()
    {
        float eff[kMaxParams];
        patch->effective(eff, modOut);
        params.count = patch->paramCount();
        bool changed = false;
        for (int i = 0; i < params.count; i++)
        {
            if (!paramsValid)
            {
                params.start[i] = eff[i];
            }
            if (params.end[i] != eff[i] || !paramsValid)
            {
                changed = true;
            }
            params.end[i] = eff[i];
        }
        params.moving = false;
        for (int i = 0; i < params.count; i++)
        {
            if (params.start[i] != params.end[i])
            {
                params.moving = true;
                break;
            }
        }
        if (changed)
        {
            params.serial++;
        }
        paramsValid = true;
    }

    void onMidi(const MidiEvent& e)
    {
        if (e.type == MidiEvent::Type::kStop)
        {
            host.flushPending(*this); // a note-on waiting for the next block starts and is released like the others
            releaseAll();
            bend = 0.0f;
            host.onStop();
            return;
        }
        if (e.channel != channel && e.channel != MidiEvent::kChannelOmni)
        {
            return;
        }
        switch (e.type)
        {
            case MidiEvent::Type::kNoteOn:
                if (e.data.note.velocity == 0)
                {
                    noteOff(e.data.note.note);
                }
                else
                {
                    noteOn(e.data.note.note, e.data.note.velocity);
                }
                break;
            case MidiEvent::Type::kNoteOff: noteOff(e.data.note.note); break;
            case MidiEvent::Type::kPitchBendChange:
                bend = float(e.data.pitchBendChange.value) * (kBendRange / 8192.0f);
                break;
            case MidiEvent::Type::kControlChange:
            {
                const int cc = e.data.controlChange.id;
                if (cc == MidiEvent::Data::ControlChange::kAllNotesOffCC || cc == 120)
                {
                    host.flushPending(*this);
                    releaseAll();
                }
                else if (patch && cc >= Patch::Metadata::Macro::kMacroCCStart &&
                         cc < Patch::Metadata::Macro::kMacroCCStart + int(kUserMacroCount))
                {
                    patch->setMacroCC(cc - Patch::Metadata::Macro::kMacroCCStart,
                                      e.data.controlChange.value);
                }
                break;
            }
            default: break;
        }
    }

    // A voice for a new note, or nullptr (global cap 0). A free record (off or fading) only
    // sounds if the shared pool has room, else the best global victim goes: in another slot it
    // fades out, in this one its record is taken over. Own cap reached: steal among own voices.
    Voice* pickVoice(bool& reused)
    {
        reused = false;
        const int n = std::clamp(voiceCount, 1, def ? def->maxVoices : 1);
        Voice* mine = nullptr;
        if (n == 1)
        {
            if (voices[0].on && !voices[0].v.fading)
            {
                reused = true; // mono legato: the held voice continues, the count is unchanged
                return &voices[0];
            }
            mine = &voices[0];
        }
        else
        {
            for (int i = 0; i < n && !mine; i++)
            {
                if (!voices[i].on || voices[i].v.fading)
                {
                    mine = &voices[i];
                }
            }
        }
        if (mine)
        {
            if (host.sounding() < host.cap())
            {
                return mine;
            }
            Voice* victim = host.victim();
            if (victim && (victim < voices || victim >= voices + kMaxVoicesPerInstrument))
            {
                // live: a released victim that is already near silence is freed at once, so this block does not render
                // it next to the new note (offline render keeps the fade, the export stays as it was)
                const bool kill = host.clock() && !victim->v.gate && victim->age > 0 && victim->v.level < kFastKillLevel;
                if (kill)
                {
                    victim->on = false;
                }
                else
                {
                    victim->v.fading = true;
                }
                host.noteStolen(kill);
            }
            else if (victim)
            {
                host.noteStolen(false);
                mine = victim;
            }
            else
            {
                mine = nullptr;
            }
            return mine;
        }
        Voice* best = &voices[0];
        for (int i = 1; i < n; i++)
        {
            if (betterVictim(voices[i], *best))
            {
                best = &voices[i];
            }
        }
        return best;
    }

    // Learns this part's costs from a timed voice render (live only): the first render of a note carries the burst of most
    // engines (PLK makes it there), so it plus the engine's noteOn over a running voice is the note-on cost (a decaying max,
    // the estimate errs high), the other renders give the running voice cost (average of 16)
    void learn(Voice& v, uint32_t cyc)
    {
        if (v.age == 1)
        {
            const uint32_t a = cyc + v.onCyc > voiceCyc ? cyc + v.onCyc - voiceCyc : 0;
            attackCyc = std::max(a, attackCyc - (attackCyc >> 5));
        }
        else if (!v.v.fading)
        {
            voiceCyc = uint32_t(int32_t(voiceCyc) + (int32_t(cyc) - int32_t(voiceCyc)) / 16);
        }
    }

    void noteOn(uint8_t note, uint8_t velocity)
    {
        if (!engine || !patch || voiceCount == 0)
        {
            return;
        }
        if (!host.admit(*this, note, velocity))
        {
            return; // queued: starts at the start of the next block
        }
        noteOnNow(note, velocity);
    }

    void noteOnNow(uint8_t note, uint8_t velocity)
    {
        if (patch->modActive())
        {
            modNoteOnAll();
        }
        refreshParams();
        bool reused;
        Voice* v = pickVoice(reused);
        if (!v)
        {
            return;
        }
        ot::audio::SynthNote n;
        n.note = note;
        n.pitch = float(note);
        n.velocity = def && def->volumeParam < 0 ? float(velocity) * (1.0f / 127.0f) : 1.0f;
        n.reused = reused;
        n.overlap = reused && v->v.gate;
        if (!reused)
        {
            v->v = ot::audio::SynthVoice{};
            std::memset(v->v.state, 0, sizeof(v->v.state));
            v->gain = 0.0f;
            v->age = 0;
        }
        v->v.slotScratch = &scratch;
        v->v.slot = uint8_t(id);
        v->v.note = note;
        v->v.gate = true;
        v->v.fading = false;
        v->v.bend = v->v.bendStart = bend;
        v->vel = float(velocity) * (1.0f / 127.0f);
        v->on = true;
        const auto clk = host.clock();
        const uint32_t t0 = clk ? clk() : 0;
        engine->noteOn(v->v, params, n);
        v->onCyc = clk ? clk() - t0 : 0;
    }

    void noteOff(uint8_t note)
    {
        if (!engine)
        {
            return;
        }
        host.pendingOff(*this, note);
        for (auto& v : voices)
        {
            if (v.on && v.v.gate && v.v.note == note)
            {
                v.v.gate = false;
                engine->noteOff(v.v, params);
            }
        }
        modGateEnded();
    }

    void releaseAll()
    {
        if (!engine)
        {
            return;
        }
        for (auto& v : voices)
        {
            if (v.on && v.v.gate)
            {
                v.v.gate = false;
                engine->noteOff(v.v, params);
            }
        }
        modGateEnded();
    }

    VoiceHost& host;
    int id;
    int channel = 0;
    LocalMode localMode = LocalMode::kOn;
    int voiceCount = 0;
    std::shared_ptr<OpenPatch> patch;
    const EngineDef* def = nullptr;
    ot::audio::ISynthEngine* engine = nullptr;
#if OPEN_SLOT_FX
    bool fxTail = false; // the engine's slot effect still has a tail after the last voice
#endif
    std::shared_ptr<MidiSource::MidiEventCallback> callback;
    const TempoProvider* tempo = nullptr;
    ot::audio::SynthSlotScratch scratch;
    ot::audio::SynthParams params;
    bool paramsValid = false;
    open::ModState modSt[open::kModSlots];
    float modOut[open::kModSlots] = {};
    uint8_t modSerial = 0;
    uint8_t cfgSerial = 0;
    float bend = 0.0f; // pitch bend in semitones, applied to every voice (SynthVoice::bend)
    uint32_t attackCyc = kSeedAttackCycles; // learned (live renders): a note-on over a running voice
    uint32_t voiceCyc = kSeedVoiceCycles;   // learned: one running voice
    uint32_t blockVoices = 0, blockRenderCyc = 0;
    bool blockMoving = false;
    Voice voices[kMaxVoicesPerInstrument];
};

// ------------------------------------------------------------------- synth

class OpenSynth final
  : public Synth
  , public VoiceHost
  , public ot::audio::IVoicePool
{
  public:
    explicit OpenSynth(const Allocator& alloc)
    {
        InstrumentTypeRegistry::registerInstrumentTypes(registry, alloc);
        governor.setPool(this);
        governor.setBudget(open::kDefaultVoiceBudget);
    }

    // ---- voice pool (VoiceHost, IVoicePool)
    int cap() const override { return governor.cap(); }
    int sounding() const override
    {
        int n = 0;
        for (const auto& i : instruments)
        {
            if (i)
            {
                for (const Voice& v : static_cast<OpenInstrument&>(*i).voiceSpan())
                {
                    n += v.on && !v.v.fading;
                }
            }
        }
        return n;
    }
    Voice* victim() override
    {
        Voice* best = nullptr;
        for (const auto& i : instruments)
        {
            if (i)
            {
                for (Voice& v : static_cast<OpenInstrument&>(*i).voiceSpan())
                {
                    if (v.on && !v.v.fading && (!best || betterVictim(v, *best)))
                    {
                        best = &v;
                    }
                }
            }
        }
        return best;
    }
    bool stealOne() override
    {
        Voice* v = victim();
        if (v)
        {
            v->v.fading = true; // last block: the gain falls to 0, the record is freed after it
        }
        return v != nullptr;
    }
    void onStop() override { governor.reset(); }

    // ---- note-on admission (live blocks only: a cycle source, governor on)
    ot::audio::CpuMeter::CycleFn clock() const override { return governor.enabled() ? clk : nullptr; }
    void noteStolen(bool fastKill) override
    {
        stats.steals++;
        stats.fastKills += fastKill;
    }
    bool admit(OpenInstrument& inst, uint8_t note, uint8_t velocity) override
    {
        const auto c = clock();
        if (!c)
        {
            return true;
        }
        const uint32_t cost = inst.attackCost() + inst.voiceCost();
        if (!inBlock)
        {
            carry += cost; // outside the audio block (main thread with the audio interrupt off): it renders in the next block
            return true;
        }
        stats.admitted++;
        bool queued = false; // the order of a part's notes is kept: after one waits, the next waits too
        for (int i = 0; i < pendN && !queued; i++)
        {
            queued = pend[i].inst == &inst;
        }
        if (!queued)
        {
            const uint32_t now = c() - blockStart;
            // the rest of the block from the synth render on, as the last block took it without its note-ons
            const uint32_t used = prevPre + prevExtra;
            const uint32_t rest = prevTotal > used ? prevTotal - used : 0;
            // the synth render starts after the sequencer: at the last block's point or later when this block's step work ran longer
            const uint32_t est = std::max(now, prevPre) + rest + extra + cost;
            shot.estMax = std::max(shot.estMax, est);
            if (est <= admitCycles)
            {
                extra += cost;
                shot.admitted++;
                return true;
            }
        }
        if (pendN == kPendingMax)
        {
            stats.queueFull++; // never dropped: it starts now
            shot.admitted++;
            extra += cost;
            return true;
        }
        // the voice it will take: when the pool is full a victim in another part fades in this block (it renders here
        // anyway), so the block the note starts in does not render both
        if (inst.takesPoolVoice() && sounding() + pendSlots >= cap())
        {
            Voice* v = victim();
            if (v && !inst.owns(v))
            {
                v->v.fading = true;
                stats.preSteals++;
            }
        }
        else if (inst.takesPoolVoice())
        {
            pendSlots++;
        }
        pend[pendN++] = Pending{&inst, note, velocity, false, c()};
        stats.deferred++;
        shot.deferred++;
        if (++deferredNow > stats.deferMaxBlock)
        {
            stats.deferMaxBlock = deferredNow;
        }
        return false;
    }
    bool pendingOff(OpenInstrument& inst, uint8_t note) override
    {
        bool hit = false;
        for (int i = 0; i < pendN; i++)
        {
            if (pend[i].inst == &inst && pend[i].note == note)
            {
                pend[i].off = true;
                hit = true;
            }
        }
        return hit;
    }
    void flushPending(OpenInstrument& inst) override
    {
        Pending mine[kPendingMax];
        int n = 0, k = 0;
        for (int i = 0; i < pendN; i++)
        {
            if (pend[i].inst == &inst)
            {
                mine[n++] = pend[i];
            }
            else
            {
                pend[k++] = pend[i];
            }
        }
        pendN = k;
        for (int i = 0; i < n; i++)
        {
            mine[i].inst->startQueued(mine[i].note, mine[i].vel, mine[i].off);
        }
    }
    void dropPending(OpenInstrument& inst) override
    {
        int k = 0;
        for (int i = 0; i < pendN; i++)
        {
            if (pend[i].inst != &inst)
            {
                pend[k++] = pend[i];
            }
        }
        pendN = k;
    }
    // Start of a live block: the note-ons queued in the block before start first, their cost counts in this block
    void blockStart_(uint32_t start, ot::audio::CpuMeter::CycleFn fn)
    {
        clk = fn;
        blockStart = start;
        inBlock = true;
        extra = carry;
        carry = 0;
        pre = 0;
        deferredNow = 0;
        pendSlots = 0;
        if (pendN == 0 || !clock())
        {
            return;
        }
        Pending q[kPendingMax];
        const int n = pendN;
        std::copy_n(pend, n, q);
        pendN = 0;
        const uint32_t t = fn();
        for (int i = 0; i < n; i++)
        {
            const uint32_t us = (t - q[i].asked) / kCyclesPerUs;
            stats.deferMaxUs = std::max(stats.deferMaxUs, us);
            extra += q[i].inst->attackCost() + q[i].inst->voiceCost();
            q[i].inst->startQueued(q[i].note, q[i].vel, q[i].off);
        }
        shot.flushed = uint32_t(n);
    }
    void setAdmit(uint32_t permille) { admitCycles = admitCyclesFor(std::clamp<uint32_t>(permille, 500, 1000)); }
    uint32_t admit() const { return uint32_t(uint64_t(admitCycles) * 1000u / ot::audio::CpuMeter::kBudgetCycles); }
    void shots(open::BlockShot& w, open::BlockShot& o) const
    {
        w = worst;
        o = lastOver;
    }
    void renderStarts()
    {
        if (inBlock && clk && pre == 0) // the first part rendered in this block
        {
            pre = clk() - blockStart;
        }
    }
    void resetStats()
    {
        stats = open::LoadStats{};
        worst = lastOver = shot = open::BlockShot{};
        cutsAtReset = governor.cuts();
        govStealsAtReset = governor.steals();
        meter.reset();
        peak = 0;
    }
    void fillStats(open::LoadStats& s)
    {
        s = stats;
        s.blocks = meter.blocks();
        s.overruns = meter.overruns();
        s.peakPermille = peak;
        s.avgPermille = meter.avg();
        s.cutsTotal = governor.cuts();
        s.cuts = governor.cuts() - cutsAtReset;
        s.govSteals = governor.steals() - govStealsAtReset;
        s.cap = governor.cap();
        s.budget = governor.budget();
        s.sounding = sounding();
        for (int p = 0; p < 3; p++)
        {
            auto* i = p < int(instruments.size()) && instruments[p] ? &static_cast<OpenInstrument&>(*instruments[p]) : nullptr;
            s.attackUs[p] = i ? i->attackCost() / kCyclesPerUs : 0;
            s.voiceUs[p] = i ? i->voiceCost() / kCyclesPerUs : 0;
        }
    }

    void setBudget(int voices)
    {
        governor.setBudget(std::clamp(voices, 1, open::kMaxVoiceBudget));
        for (int n = sounding(); n > governor.cap() && stealOne(); n = sounding())
        {
        }
    }
    void blockDone(uint32_t cycles)
    {
        meter.addBlock(cycles);
        peak = std::max(peak, meter.last());
        governor.evaluate(meter);
        if (inBlock)
        {
            prevTotal = cycles;
            prevPre = pre;
            prevExtra = extra;
            inBlock = false;
            shot.cycles = cycles;
            shot.pre = pre;
            shot.extra = extra;
            for (const auto& i : instruments)
            {
                if (i)
                {
                    static_cast<OpenInstrument&>(*i).takeBlock(shot.voices, shot.renderCyc, shot.moving);
                }
            }
            if (cycles > worst.cycles)
            {
                worst = shot;
            }
            if (meter.last() >= ot::audio::CpuMeter::kOverrunPermille)
            {
                lastOver = shot;
            }
        }
        shot = open::BlockShot{};
    }
    void engineGone(const EngineDef* d)
    {
        for (const auto& i : instruments)
        {
            if (i)
            {
                static_cast<OpenInstrument&>(*i).engineGone(d);
            }
        }
    }
    void engineRestart(const EngineDef* d, bool done)
    {
        for (const auto& i : instruments)
        {
            if (i)
            {
                auto& oi = static_cast<OpenInstrument&>(*i);
                if (done)
                {
                    oi.engineRestarted(d);
                }
                else
                {
                    oi.engineRestart(d);
                }
            }
        }
    }
    void projectLoaded()
    {
        for (const auto& i : instruments)
        {
            if (i)
            {
                for (Voice& v : static_cast<OpenInstrument&>(*i).voiceSpan())
                {
                    v.on = false;
                }
            }
        }
        governor.reset();
        pendN = 0;
        resetStats();
    }
#ifdef OPEN_HOST_TEST
    OpenInstrument* instrumentAt(int part)
    {
        return part >= 0 && part < int(instruments.size()) && instruments[part] ? &static_cast<OpenInstrument&>(*instruments[part]) : nullptr;
    }
#endif
    bool partIdle(int part)
    {
        if (part < 0 || part >= int(instruments.size()) || !instruments[part])
        {
            return false;
        }
        return static_cast<OpenInstrument&>(*instruments[part]).idle();
    }
    void setOffline(bool offline)
    {
        // a note-on still waiting starts now: the offline render admits every note-on and must see none in a queue
        Pending q[kPendingMax];
        const int n = pendN;
        std::copy_n(pend, n, q);
        pendN = 0;
        for (int i = 0; i < n; i++)
        {
            q[i].inst->startQueued(q[i].note, q[i].vel, q[i].off);
        }
        governor.setEnabled(!offline);
        governor.reset();
        meter.reset();
        inBlock = false;
        carry = 0;
    }
    ~OpenSynth() override;
    ot::audio::Governor governor;

    std::unique_ptr<PatchStore> createPatchStore(FileSystemId id, bool typeFolders) override
    {
        return open::makePatchStore(registry, fileSystems[int(id)].get(), typeFolders);
    }
    std::unique_ptr<PatchPool> createPatchPool(
      std::shared_ptr<PatchStore>,
      int,
      std::function<tiny::optional<int>(const std::string&)>) override
    {
        return nullptr;
    }
    std::shared_ptr<Instrument> createInstrument(int instrumentId) override
    {
        return std::make_shared<OpenInstrument>(instrumentId, *this);
    }
    std::unique_ptr<Mixer> createMixer(int, int) override { return nullptr; }
    std::unique_ptr<Grid> createGrid() override { return nullptr; }
    std::unique_ptr<GridMapperStore> createGridMapperStore(FileSystemId) override
    {
        return nullptr;
    }
    std::unique_ptr<GridMapper> createGridMapper() override { return nullptr; }
    std::unique_ptr<SampleStore> createSampleStore(FileSystemId) override { return nullptr; }
    std::shared_ptr<Sequencer> createSequencer() override { return nullptr; }

    void addMidiSource(const std::string&, std::shared_ptr<MidiSource>) override {}
    void addMidiSink(const std::string&, std::unique_ptr<MidiSink>) override {}
    void addFileSystem(FileSystemId id, std::unique_ptr<FileSystem> fileSystem) override
    {
        fileSystems[int(id)] = std::move(fileSystem);
    }
    void initSamplePool(size_t, std::shared_ptr<SampleStore>) override {}

    std::string getInstrumentName(int typeId) override
    {
        auto type = registry.getInstrumentType(typeId);
        return type ? type->name : std::string{};
    }
    std::vector<std::string_view> getInstrumentTypeNames() override { return {}; }
    std::vector<int> getInstrumentTypeIds() override { return {}; }

    void setup(std::span<std::shared_ptr<Instrument>> newInstruments,
               std::span<std::shared_ptr<Instrument>>,
               std::shared_ptr<Mixer>,
               TempoProvider tempoProvider) override
    {
        tempo = std::move(tempoProvider);
        pendN = 0; // queued note-ons point at the old instruments
        instruments.assign(newInstruments.begin(), newInstruments.end());
        for (auto& i : instruments)
        {
            if (i)
            {
                static_cast<OpenInstrument&>(*i).setTempoProvider(&tempo);
            }
        }
    }
    void disconnectInstrument(int) override {}

    void renderAudioBuffer(AudioBufferStereoRef buffer) override
    {
        renderStarts();
        clear(buffer);
        for (auto& i : instruments)
        {
            if (i)
            {
                static_cast<OpenInstrument&>(*i).render(buffer[0].data(), buffer[1].data());
            }
        }
    }
    void renderAudioBuffer(AudioBufferStereoRef bufferMain,
                           std::span<AudioBufferRef> bufferAux,
                           int instrumentId) override
    {
        renderStarts();
        clear(bufferMain);
        for (auto& aux : bufferAux)
        {
            std::fill(aux.begin(), aux.end(), 0.0f);
        }
        if (instrumentId >= 0 && instrumentId < int(instruments.size()) && instruments[instrumentId])
        {
            float* l = bufferMain[0].data();
            float* r = bufferMain[1].data();
            static_cast<OpenInstrument&>(*instruments[instrumentId]).render(l, r);
            // mono output: tracker-ce reads the left buffer only
            for (int i = 0; i < kAudioBufferSize; i++)
            {
                l[i] = 0.5f * (l[i] + r[i]);
            }
        }
    }

    void updateAudio() override {}
    void updateMain() override {}

  private:
    static void clear(AudioBufferStereoRef buffer)
    {
        for (auto& channel : buffer)
        {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
    }

    InstrumentTypeRegistry registry;
    std::unique_ptr<FileSystem> fileSystems[4]; // by FileSystemId
    std::vector<std::shared_ptr<Instrument>> instruments;
    TempoProvider tempo;
    ot::audio::CpuMeter meter;

    // note-on admission state (audio interrupt)
    struct Pending
    {
        OpenInstrument* inst;
        uint8_t note;
        uint8_t vel;
        bool off;
        uint32_t asked; // cycle counter when the note-on came
    };
    Pending pend[kPendingMax];
    int pendN = 0;
    int pendSlots = 0;              // queued note-ons of this block that take a free pool voice (no victim faded for them)
    ot::audio::CpuMeter::CycleFn clk = nullptr;
    bool inBlock = false;
    uint32_t blockStart = 0;
    uint32_t pre = 0;               // this block: cycles from its start to the synth render
    uint32_t extra = 0;             // this block: estimated cycles of the note-ons started in it
    uint32_t carry = 0;             // note-ons started between blocks, counted in the next
    uint32_t deferredNow = 0;
    uint32_t prevTotal = 0, prevPre = 0, prevExtra = 0;
    uint32_t peak = 0;
    uint32_t cutsAtReset = 0, govStealsAtReset = 0;
    open::LoadStats stats{};
    uint32_t admitCycles = admitCyclesFor(open::kAdmitPermille);
    open::BlockShot shot{}, worst{}, lastOver{};
};

OpenSynth* gActive = nullptr; // the one Synth of the firmware (the latest created in host tests)

OpenSynth::~OpenSynth()
{
    if (gActive == this)
    {
        gActive = nullptr;
    }
}

} // namespace

std::shared_ptr<Synth> Synth::createSynth(Allocator largeBuffersAlloc)
{
    auto synth = std::make_shared<OpenSynth>(largeBuffersAlloc);
    gActive = synth.get();
    return synth;
}

void open::reportBlockCycles(uint32_t cycles)
{
    if (gActive)
    {
        gActive->blockDone(cycles);
    }
}
void open::reportBlockStart(uint32_t startCycles, uint32_t (*cycles)())
{
    if (gActive)
    {
        gActive->blockStart_(startCycles, cycles);
    }
}
void open::loadStats(LoadStats& s)
{
    if (gActive)
    {
        gActive->fillStats(s);
    }
    else
    {
        s = LoadStats{};
    }
}
void open::setAdmitPermille(uint32_t permille)
{
    if (gActive)
    {
        gActive->setAdmit(permille);
    }
}
uint32_t open::admitPermille() { return gActive ? gActive->admit() : kAdmitPermille; }
void open::loadShots(BlockShot& w, BlockShot& o)
{
    if (gActive)
    {
        gActive->shots(w, o);
    }
    else
    {
        w = o = BlockShot{};
    }
}
void open::resetLoadStats()
{
    if (gActive)
    {
        gActive->resetStats();
    }
}
bool open::partIdle(int part) { return gActive && gActive->partIdle(part); }
#ifdef OPEN_HOST_TEST
const ot::audio::SynthParams* open::testParams(int part)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i ? &i->testParams() : nullptr;
}
float open::testModOut(int part, int slot)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i ? i->testModOut(slot) : 0.0f;
}
#endif
void open::setOffline(bool offline)
{
    if (gActive)
    {
        gActive->setOffline(offline);
    }
}
void open::engineGone(const EngineDef* def)
{
    if (gActive)
    {
        gActive->engineGone(def);
    }
}
void open::engineRestartLocked(const EngineDef* def)
{
    if (gActive)
    {
        gActive->engineRestart(def, false);
    }
}
void open::engineRestartedLocked(const EngineDef* def)
{
    if (gActive)
    {
        gActive->engineRestart(def, true);
    }
}
void open::resetOnProjectLoad()
{
    if (gActive)
    {
        gActive->projectLoaded();
    }
}
void open::setVoiceBudget(int voices)
{
    if (gActive)
    {
        gActive->setBudget(voices);
    }
}
int open::voiceBudget() { return gActive ? gActive->governor.budget() : 0; }
int open::voiceCap() { return gActive ? gActive->governor.cap() : 0; }
int open::voicesSounding() { return gActive ? gActive->sounding() : 0; }
uint32_t open::governorCuts() { return gActive ? gActive->governor.cuts() : 0; }

} // namespace polysynth