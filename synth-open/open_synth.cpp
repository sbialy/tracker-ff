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
#include "ff_seams.h"
#include "open_load.h"
#include "open_patch.h"
#include "../core/source/mtTuning.h" // pure arithmetic as well
#include "../core/source/tapeFx.h" // pure arithmetic, no firmware includes: the host builds of this file (run_smoke.sh) have no core/source on the path

#include "audio/cpu_meter.h"
#ifdef SYNTH_ENGINE_ACD
#include "audio/engines/acd.h"
#endif
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
#include <bit>
#include <cmath>
#ifdef OPEN_REFRESH_CHECK
#include <cstdio>
#include <cstdlib>
#endif
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
using open::ChordParams;
using open::chordParamsOf;
using open::EngineDef;
using open::kMaxParams;
using open::kMaxVoicesPerInstrument;
using open::kVelocityMacro;
using open::OpenPatch;

namespace
{

// Pitch bend range in semitones for full deflection (+-8192). The tracker's Micro-tune FX (-99..+99
// cents) sends the full 14 bit range, so 1 semitone makes the FX read in cents. Own choice. The Synth play page sets a live
// range per part (open::partBendRange) while it is open and puts this one back when it closes.
constexpr float kBendRange = 1.0f;

// std::bit_cast needs GCC 11, the pinned 10.3 has none: the same bits through memcpy, which compiles to the same register move
template <class To, class From>
inline To bitCast(const From& from)
{
#if defined(__cpp_lib_bit_cast)
    return std::bit_cast<To>(from);
#else
    static_assert(sizeof(To) == sizeof(From));
    To to;
    std::memcpy(&to, &from, sizeof(To));
    return to;
#endif
}

} // namespace

// Values with macros M1..M6 and the automation slots applied (additive, clamped once). Macro depth: Add scales by the param range,
// Scale by the headroom above (amount * macro > 0) or below (< 0) the base value.
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
            else if (c.kind == open::ModKind::kParam && c.dest < count)
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
            if (live.macroScale)
            {
                const float v = macro.amount[t] * mv;
                add[id] += v > 0.0f ? v * (float(p.maxValue) - values[id]) : v * (values[id] - float(p.minValue));
            }
            else
            {
                add[id] += macro.amount[t] * mv * (float(p.maxValue) - float(p.minValue));
            }
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

// The cycle counter of a live block (host.clock(), set by Profiler: platform timer -> clockCycles()) is the DWT cycle counter on the unit. The
// two reads around every voice render skip the 3 calls behind the function pointer there. clk stays the live / not live switch.
#if defined(KINETISK) && defined(__arm__)
inline uint32_t cycleNow(ot::audio::CpuMeter::CycleFn) { return *reinterpret_cast<const volatile uint32_t*>(0xE0001004u); }
#else
inline uint32_t cycleNow(ot::audio::CpuMeter::CycleFn fn) { return fn(); }
#endif

struct Voice
{
    ot::audio::SynthVoice v; // v.fading: stolen by the pool, its last block fades the gain to 0
    bool on = false;
    uint8_t fadeLeft = 0; // New Note Action Cut / Fade: blocks of the shim's ramp to 0 that are left (1: the last one), 0: none (sits in the padding after `on`)
    uint16_t id = 0; // the sequencer's id of the note that started the voice (open::noteTag), 0: none (pad, MIDI in, record)
    float vel = 1.0f;
    float gain = 0.0f;
    uint32_t age = 0;
    float tuneSt = 0.0f; // the project tuning of the note in semitones on the bend, for an engine that reads only the integer note (CHIP), 0 otherwise
    uint32_t onCyc = 0; // live: cycles of the engine's noteOn, added to the first render to learn the part's note-on cost
    // MPE in (open::mpeNote): the member channel of the note (0: none, a plain note), its raw 14 bit bend (-8192..8191, 0 for a plain note, kept
    // when a newer note takes the channel) and its pressure (127: none yet, no change of the level)
    uint8_t chan = 0;
    uint8_t press = 127;
    int16_t nbend = 0;
    // counts for the pool and can be stolen: on, not stolen already, not on the last block of a Cut
    bool live() const { return on && !v.fading && fadeLeft != 1; }
};

// The sequencer's id of the note it is sending (open::noteTag), 0 outside a sequencer send. Read by onMidi, which runs inside the send.
uint16_t gNoteTag = 0;
// The MPE member channel (2..16) of the note-on or note-off that is being sent (open::mpeNote), 0 outside it. Read by onMidi like the tag
uint8_t gMpeChan = 0;
// How the note-off of that send ends the note (open::noteTag): 0 Release, 2 Cut, 3 Fade (the New Note Action of the replaced note's part)
uint8_t gNoteEnd = 0;
// Fade: 120 ms = 41 blocks of 128 frames at 44.1 kHz, linear in amplitude. Cut is the last block of it (fadeLeft 1)
constexpr uint8_t kNnaFadeBlocks = 41;

// Note-on admission and the cost model (live blocks only, OpenSynth::admit). Cycles at 180 MHz.
constexpr uint32_t kCyclesPerUs = 180;
constexpr uint32_t admitCyclesFor(uint32_t permille) { return uint32_t(uint64_t(ot::audio::CpuMeter::kBudgetCycles) * permille / 1000u); }
// Seeds of the learned costs before a part played a note (device: PLK note-on about 150 us, a running voice about 32 us,
// the governor notes): a fresh part is judged as a PLK part until its own numbers come in
constexpr uint32_t kSeedAttackCycles = 150 * kCyclesPerUs;
constexpr uint32_t kSeedVoiceCycles = 32 * kCyclesPerUs;
// A released victim quieter than this (amplitude envelope, about -48 dB) is freed at once instead of fading over a block
constexpr float kFastKillLevel = 1.0f / 256.0f;
static_assert(open::kPageVoicesPerPart == kMaxVoicesPerInstrument, "the page cap lift is the voice records of an instrument");
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
    // chord: the MIDI Chord value the note-on carries to a paraphonic engine (-1: none), it rides with a queued note-on
    // id: the note id of the note-on (0: none)
    virtual bool admit(OpenInstrument& inst, uint8_t note, uint8_t velocity, int16_t chord, uint16_t id) = 0;
    virtual bool pendingOff(OpenInstrument& inst, uint8_t note, uint16_t id, uint8_t chan) = 0;   // a queued note-on of this note (and id) gets its note-off too
    virtual void flushPending(OpenInstrument& inst) = 0;               // start the queued note-ons of this part now (Stop)
    virtual void dropPending(OpenInstrument& inst) = 0;                // forget them (the part's voices were cut)
    virtual ot::audio::CpuMeter::CycleFn clock() const = 0;            // live cycle counter, nullptr when not live
    virtual void noteStolen(bool fastKill) = 0;                        // counters
    // The sequencer tempo of this block (0: none), asked once per block for all parts (OpenSynth::newBlock opens the next one)
    virtual float blockTempo(const TempoProvider* provider) = 0;
};

class OpenInstrument final : public Instrument
{
  public:
    void releaseGated() { releaseAll(); } // New Note Action D11: Stop releases every gated voice of the part
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
        volCached = false;
        velCached = false;
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
            volCached = false;
            velCached = false;
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
    float testBend() const { return bend; }   // ui_shot only (gen_overlay.py hostPart): the live bend in semitones and its range
    float testBendRange() const { return bendRange; }
    int testMpe(char* buf, size_t n) const
    {
        int at = snprintf(buf, n, "n=");
        int cnt = 0, k = 0;
        char list[256] = "";
        int la = 0;
        for (const auto& v : voices)
        {
            if (v.on)
            {
                la += snprintf(list + la, sizeof(list) - size_t(la), " %d:%d:%d:%d:%d:%d:%d", k, int(v.v.note), int(v.chan), int(v.v.gate), int(v.nbend), int(v.press),
                               int(v.gain * 1000.0f));
                cnt++;
            }
            k++;
        }
        at += snprintf(buf + at, n - size_t(at), "%d [%s ]", cnt, list);
        return at;
    }
    float testGain() const { return morphGain; }   // ui_shot only (gen_overlay.py hostGain): the morph factor and the gain of the first voice that is on
    float testVoiceGain() const
    {
        for (const Voice& v : voices)
        {
            if (v.on)
            {
                return v.gain;
            }
        }
        return -1.0f;
    }
    bool testCap(EngineDef& copy, int maxVoices) { if (!def) return false; copy = *def; copy.maxVoices = maxVoices; def = &copy; return true; }   // ui_shot only (gen_overlay.py hostCapSim)
    // The engine adds into L only (EngineDef::monoOut): render() leaves R alone, the caller need not clear R or fold L / R
    bool monoOut() const { return def && def->monoOut; }

    // Nothing to render this block: no engine or patch, no voices, nothing sounding and no slot effect tail. Also what
    // PolySynth::update asks (open::partIdle) to skip a whole part without allocating or sending blocks.
    bool idle()
    {
        if (!engine || !patch || voiceCount == 0 || (!anyActive() && !OPEN_SLOT_FX_TAIL))
        {
            paramsValid = false; // nothing sounds: next note starts with a clean ramp
            if (tapeReq.mode != tapefx::kOff && --tapeReq.blocks == 0)
            {
                tapeReq = TapeRequest{}; // a Tape FX on a part that sounds nothing and gets no note (a block or two grace for a queued note-on)
            }
            return true;
        }
        return false;
    }

    // Adds this instrument's voices into L / R (kAudioBufferSize frames). busyKnown: idle() was just asked (OpenSynth::partIdle, the answer was
    // "busy") and nothing ran since, so the second call is left out.
    void render(float* L, float* R, bool busyKnown = false)
    {
        if (!busyKnown && idle())
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
        const float bpmNow = host.blockTempo(tempo);
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
        if (hasVol)
        {
            // the Volume range is the patch's own metadata (fixed from the patch's creation, read again when a patch comes in), the velocity
            // slope follows the live macros: it is made again when anything effective() reads changed (effSerial)
            if (!volCached)
            {
                volMinC = float(patch->getMetadata().params[def->volumeParam].minValue);
                volMaxC = float(patch->getMetadata().params[def->volumeParam].maxValue);
                volCached = true;
            }
            if (!velCached || velSerC != patch->effSerial())
            {
                velKC = patch->velocityAmount();
                velSerC = patch->effSerial();
                velCached = true;
            }
        }
        const float volMin = hasVol ? volMinC : 0.0f;
        const float volMax = hasVol ? volMaxC : 1.0f;
        const float velK = hasVol ? velKC : 1.0f;
        const auto clk = host.clock(); // live: every voice render is timed to learn this part's costs (the audio is not touched)
        float tapeSt = 0.0f, tapeGain = 1.0f;
        if (tapeReq.mode != tapefx::kOff)
        {
            tapeBegin(); // voices sound and no note-on of the step came: the run takes them
        }
        if (tapeMode != tapefx::kOff)
        {
            const float r = tapefx::ratio(tapeMode, tapePos + tapefx::kBlockFrames, tapeTotal);
            tapeSt = tapefx::semitonesFor(r);
            tapeGain = tapefx::gainFor(r);
        }
        for (auto& v : voices)
        {
            if (!v.on)
            {
                continue;
            }
            const float vol = hasVol ? params.end[def->volumeParam] : 1.0f;
            const bool fade = v.v.fading;
            float target =
              fade ? 0.0f : std::clamp(vol * (1.0f + velK * (v.vel - 1.0f)), volMin, volMax) * tapeGain * morphGain * tableGain;
            if (pressAmt != 0.0f)
            {
                target *= 1.0f + pressAmt * (float(v.press) * (1.0f / 127.0f) - 1.0f); // MPE pressure: full at 127, 1 - amount at 0
            }
            const uint8_t nnaLeft = v.fadeLeft;
            if (nnaLeft)
            {
                // New Note Action Cut / Fade: the gain ramps down block by block, 0 in the last one (Cut is only that block)
                target *= float(nnaLeft - 1) * (1.0f / float(kNnaFadeBlocks));
                v.fadeLeft = uint8_t(nnaLeft - 1);
            }
            v.v.bendStart = v.v.bend;
            v.v.bend = bend + tapeSt + tableSt + v.tuneSt;
            if (v.nbend != 0)
            {
                v.v.bend += float(v.nbend) * mpeStep; // MPE member bend (open::partNoteBend), kept by a tail the channel's next note left behind
            }
            const float g0 = v.gain;
            const float gInc = (target - g0) * (1.0f / float(kAudioBufferSize));
            v.gain = target;
            v.age++;
            const uint32_t t0 = clk ? cycleNow(clk) : 0;
            const bool alive = engine->render(v.v, params, g0, gInc, L, R);
            if (clk)
            {
                const uint32_t cyc = cycleNow(clk) - t0;
                learn(v, cyc);
                blockVoices++;
                blockRenderCyc += cyc;
            }
            if (!alive || fade || nnaLeft == 1)
            {
                v.on = false;
            }
        }
        tapeAdvance();
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
#ifdef OPEN_REFRESH_CHECK
        ot::audio::SynthParams ref = params;
        std::memcpy(ref.start, ref.end, sizeof(float) * ref.count);
        ref.moving = false;
#endif
        if (!startIsEnd)
        {
            std::memcpy(params.start, params.end, sizeof(float) * params.count);
            startIsEnd = true;
        }
        params.moving = false;
#ifdef OPEN_REFRESH_CHECK
        refreshCheck(ref, true);
#endif
    }

    // Note-on admission: the learned cycles of a note-on of this part (over a running voice) and of one running voice
    uint32_t attackCost() const { return attackCyc; }
    uint32_t voiceCost() const { return voiceCyc; }
    // A queued note-on starts (OpenSynth, start of the next block) and its note-off when one came while it waited
    void startQueued(uint8_t note, uint8_t velocity, bool off, int16_t chord, uint16_t id, uint8_t chan)
    {
        if (!engine || !patch || voiceCount == 0)
        {
            return;
        }
        noteOnNow(note, velocity, chord, id, chan);
        if (off)
        {
            noteOff(note, id, chan);
        }
    }
    // Whether a note-on now would take a new record of the shared pool (not mono legato on a held voice)
    bool takesPoolVoice() const
    {
        const int n = std::clamp(voiceCount, 1, def ? open::pageVoiceCap(def->maxVoices) : 1);
        return !(n == 1 && voices[0].on && !voices[0].v.fading);
    }
    bool owns(const Voice* v) const { return v >= voices && v < voices + kMaxVoicesPerInstrument; }
    // Paraphony (PLUG_EXT_CHORD): the engine has Chord and Chord Mode parameters and the patch is at Para. The sequencer then sends a
    // MIDI Chord step as one note-on (noteChord, then the note-on of `note`) instead of one per chord note.
    bool chordPara(uint8_t chord) const
    {
        if (!engine || !patch || voiceCount == 0)
        {
            return false;
        }
        const ChordParams cp = chordParamsOf(def);
        return cp.chord >= 0 && cp.mode >= 0 && chord <= cp.last && patch->values_()[cp.mode] >= 0.5f;
    }
    // The next note-on of `note` carries `chord` (0..76) to the engine's Chord parameter and a Strum FX (units, + up, - down, 0 none)
    // to its Strum parameter when it has one. Same thread as the note-on that follows.
    void noteChord(uint8_t note, uint8_t chord, int8_t strum)
    {
        chordNote = note;
        chordIdx = int16_t(chord | ((std::clamp<int>(strum, -63, 63) + 64) << 8));
    }
    // The Synth play page (open::partBendRange): the bend range of the part in semitones, 0..24
    void setBendRange(float semitones) { bendRange = std::clamp(semitones, 0.0f, 24.0f); }
    // MPE in (open::partMpe, partNoteBend, partNotePress). The kits (PERC, T-Type: the note picks the drum) take no member bend
    bool mpeKit() const { return def && (def->typeId == int(polysynth::InstrumentTypeId::kDrumSynth) || def->typeId == 0x85); }
    void setMpe(float memberRange, float pressAmount)
    {
        mpeRangeSt = std::clamp(memberRange, 0.0f, 96.0f) * (1.0f / 8192.0f);
        mpeStep = mpeRangeSt * tuning::tableScale();
        pressAmt = std::clamp(pressAmount, 0.0f, 1.0f);
    }
    // chan 2..16: the voices of that member channel (a note that was released is a tail, no longer on it). The raw value is kept for the next note on it
    void noteBend(uint8_t chan, int raw)
    {
        if (chan < 2 || chan > 16 || mpeKit())
        {
            return;
        }
        mpeBend[chan] = int16_t(std::clamp(raw, -8192, 8191));
        mpeStep = mpeRangeSt * tuning::tableScale(); // the member bend is in key steps of the tuning (1.0 for 12 notes), taken here so the render path stays as it was
        for (auto& v : voices)
        {
            if (v.on && v.chan == chan)
            {
                v.nbend = mpeBend[chan];
            }
        }
    }
    // chan 2..16: only a key that is down follows its pressure (a tail keeps the level its finger left it at). chan 1, the master: every voice of the part
    void notePress(uint8_t chan, uint8_t press)
    {
        if (chan < 1 || chan > 16)
        {
            return;
        }
        if (chan > 1)
        {
            mpePress[chan] = press;
        }
        for (auto& v : voices)
        {
            if (v.on && (chan == 1 || (v.chan == chan && v.v.gate)))
            {
                v.press = press;
            }
        }
    }
    void setMorphGain(float g) { morphGain = std::clamp(g, 0.0f, 1.0f); }
    // the table of the track that plays the part (core/source/tables.h): transpose in semitones and a gain factor, on every voice
    void setTable(float semis, float gain)
    {
        tableSt = semis * tuning::tableScale(); // a key step of the tuning, 1.0 for 12 notes of 1200 cents
        tableGain = std::clamp(gain, 0.0f, 1.0f);
    }
    bool active() const { return anyActive(); } // a voice is on, a releasing one too
    int soundingVoices() const
    {
        int n = 0;
        for (const Voice& v : voices)
        {
            n += v.live();
        }
        return n;
    }
    // The Tape Stop / Tape Start step FX (audio callback, before this block renders). The run starts with the note-on of the step, or at the
    // next render when voices sound, whichever comes first. Without either it is dropped after three idle blocks.
    void tape(bool start, uint32_t frames)
    {
        tapeReq.mode = start ? tapefx::kStart : tapefx::kStop;
        tapeReq.frames = frames < 1 ? 1 : frames;
        tapeReq.blocks = 3;
    }
    // The block's timed voice renders, taken once per block by OpenSynth::blockDone
    void takeBlock(uint32_t& voicesOut, uint32_t& cycOut, uint32_t& movingOut)
    {
        voicesOut += blockVoices;
        cycOut += blockRenderCyc;
        movingOut += blockMoving;
        blockVoices = blockRenderCyc = 0;
        blockMoving = false;
    }

    int waveRef(int param, char* out, size_t cap) const
    {
        return patch && engine ? patch->waveRef(param, out, cap) : -1;
    }
    const EngineDef* livePatch(const float** v, int* n) const
    {
        if (!patch || !engine) return nullptr;
        *v = patch->rawValues();
        *n = patch->paramCount();
        return def;
    }

#if defined(OPEN_HOST_TEST) || defined(UI_SHOT_HOST)   // read only test accessors: the host tests and ui_shot
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

  public:
    // Part destinations of the automation (open_mod.h kPart): the output of every live part slot in the audio engine's units, summed per
    // destination into v[open::kModParts]. Returns the mask of the destinations that have a slot (bit = open::ModPart), 0 for none. Audio
    // interrupt, once per block (audioEngine::updateSynthParams), reads the outputs of the last render.
    uint8_t partAuto(float* v)
    {
        if (!patch || !patch->live().partOn)
        {
            return 0;
        }
        modCheck();
        const open::SlotCfg& c = patch->live();
        uint8_t mask = 0;
        for (int s = 0; s < open::kModSlots; s++)
        {
            if (c.partOn & (1u << s))
            {
                const int d = c.mod[s].dest;
                if (!(mask & (1u << d)))
                {
                    v[d] = 0.0f;
                    mask = uint8_t(mask | (1u << d));
                }
                v[d] += modOut[s] * open::kModPartRange[d];
            }
        }
        return mask;
    }

  private:
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

    // The request of a Tape step FX becomes the run (a voice starts, or the part renders with voices sounding)
    void tapeBegin()
    {
        tapeMode = tapeReq.mode;
        tapeTotal = tapeReq.frames;
        tapePos = 0;
        tapeReq = TapeRequest{};
    }
    // The tape speed at the start of a voice: a voice that starts inside a run-up begins slow
    float tapeBendNow() const
    {
        return tapeMode == tapefx::kOff ? 0.0f : tapefx::semitonesFor(tapefx::ratio(tapeMode, tapePos, tapeTotal));
    }
    // After a rendered block: the run moves on a block, a finished run-down silences the part (its level reached 0 in this block)
    void tapeAdvance()
    {
        if (tapeMode == tapefx::kOff)
        {
            return;
        }
        tapePos += tapefx::kBlockFrames;
        if (tapePos >= tapeTotal)
        {
            if (tapeMode == tapefx::kStop)
            {
                for (auto& v : voices)
                {
                    v.on = false;
                }
#if OPEN_SLOT_FX
                fxTail = false;
#endif
            }
            tapeMode = tapefx::kOff;
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
        tapeMode = tapefx::kOff;
        tapeReq = TapeRequest{};
        for (auto& v : voices)
        {
            v.on = false;
        }
#if OPEN_SLOT_FX
        fxTail = false;
#endif
    }

    // The parameters of this block: params.end = the patch's effective values, params.start = the block's start values, serial and moving as
    // before. The SDRAM is slow (the instrument lives there: blocks of 1 KB and more come from the SDRAM arena, platforms/mk66/.../new.cpp), so a
    // block with nothing changed reads each value twice (patch, params.end) and writes nothing: startIsEnd says that params.start already equals
    // params.end bit for bit (set at the end of every render), then a value that did not change cannot make the parameters move.
#ifdef OPEN_REFRESH_CHECK
    // Test build only (synth-open/tools/run_refresh_equiv.sh): the refresh as it was before the fast paths, run on a copy of the parameters,
    // and the bit for bit comparison of the two results
    void refreshParamsRef(ot::audio::SynthParams& pr, bool valid)
    {
        float eff[kMaxParams];
        patch->effective(eff, modOut);
        pr.count = patch->paramCount();
        bool changed = false;
        for (int i = 0; i < pr.count; i++)
        {
            if (!valid)
            {
                pr.start[i] = eff[i];
            }
            if (pr.end[i] != eff[i] || !valid)
            {
                changed = true;
            }
            pr.end[i] = eff[i];
        }
        pr.moving = false;
        for (int i = 0; i < pr.count; i++)
        {
            if (pr.start[i] != pr.end[i])
            {
                pr.moving = true;
                break;
            }
        }
        if (changed)
        {
            pr.serial++;
        }
    }
    void refreshCheck(const ot::audio::SynthParams& pr, bool atRenderEnd)
    {
        bool ok = pr.count == params.count && pr.serial == params.serial && pr.moving == params.moving;
        for (int i = 0; ok && i < params.count; i++)
        {
            ok = bitCast<uint32_t>(pr.start[i]) == bitCast<uint32_t>(params.start[i]) &&
                 bitCast<uint32_t>(pr.end[i]) == bitCast<uint32_t>(params.end[i]);
        }
        if (!ok)
        {
            std::fprintf(stderr, "refreshParams differs from the reference (%s)\n", atRenderEnd ? "render end" : "refresh");
            std::abort();
        }
    }
#endif
    void refreshParams()
    {
#ifdef OPEN_REFRESH_CHECK
        ot::audio::SynthParams ref = params;
        refreshParamsRef(ref, paramsValid);
#endif
        refreshParamsFast();
#ifdef OPEN_REFRESH_CHECK
        refreshCheck(ref, false);
#endif
    }
    void refreshParamsFast()
    {
        // Nothing the patch feeds effective() with changed since the refresh that found params.end already equal to it (OpenPatch::effSerial),
        // and params.start equals params.end: the result of the loops below is no change, not moving
        // With the automation on, the outputs of its slots feed effective() as well: all of them must be the bits they were then
        if (seenOk && paramsValid && startIsEnd && patch->effSerial() == seenSer &&
            (patch->effStatic() || std::memcmp(modOut, seenMod, sizeof modOut) == 0))
        {
            params.moving = false;
            return;
        }
        float eff[kMaxParams];
        const float* src = eff;
        if (patch->plainValues(true))
        {
            src = patch->values_(); // effective() would copy exactly these
        }
        else
        {
            patch->effective(eff, modOut);
        }
        params.count = patch->paramCount();
        const int n = params.count;
        bool changed = false;
        bool moving = false;
        bool same = true; // params.start == params.end bit for bit, after this refresh
        if (!paramsValid)
        {
            changed = true;
            for (int i = 0; i < n; i++)
            {
                const float v = src[i];
                params.start[i] = v;
                params.end[i] = v;
                moving |= v != v; // start != end only for a NaN
            }
        }
        else if (startIsEnd)
        {
            for (int i = 0; i < n; i++)
            {
                const float v = src[i];
                const float e = params.end[i]; // start[i] has the bits of e
                if (e != v)
                {
                    changed = true;
                    moving = true;
                }
                if (bitCast<uint32_t>(e) != bitCast<uint32_t>(v))
                {
                    params.end[i] = v;
                    same = false;
                }
                else
                {
                    moving |= v != v; // a NaN: start != end
                }
            }
        }
        else
        {
            for (int i = 0; i < n; i++)
            {
                const float v = src[i];
                changed |= params.end[i] != v;
                params.end[i] = v;
                moving |= params.start[i] != v;
                same &= bitCast<uint32_t>(params.start[i]) == bitCast<uint32_t>(v);
            }
        }
        params.moving = moving;
        startIsEnd = same;
        if (changed)
        {
            params.serial++;
        }
        paramsValid = true;
        seenOk = same && !changed && !moving;
        seenSer = patch->effSerial();
        std::memcpy(seenMod, modOut, sizeof seenMod);
    }

    void onMidi(const MidiEvent& e)
    {
        if (e.type == MidiEvent::Type::kStop)
        {
            host.flushPending(*this); // a note-on waiting for the next block starts and is released like the others
            releaseAll();
            bend = 0.0f;
            tapeMode = tapefx::kOff;
            tapeReq = TapeRequest{};
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
                    noteOff(e.data.note.note, gNoteTag, gMpeChan);
                }
                else
                {
                    noteOn(e.data.note.note, e.data.note.velocity, gNoteTag);
                }
                break;
            case MidiEvent::Type::kNoteOff: noteOff(e.data.note.note, gNoteTag, gMpeChan); break;
            case MidiEvent::Type::kPitchBendChange:
                bend = float(e.data.pitchBendChange.value) * (bendRange * (1.0f / 8192.0f));
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
        const int n = std::clamp(voiceCount, 1, def ? open::pageVoiceCap(def->maxVoices) : 1);
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

    void noteOn(uint8_t note, uint8_t velocity, uint16_t id)
    {
        int16_t chord = -1;
        if (chordNote == note)
        {
            chord = chordIdx;
        }
        chordNote = -1;
        if (!engine || !patch || voiceCount == 0)
        {
            return;
        }
        if (!host.admit(*this, note, velocity, chord, id))
        {
            return; // queued: starts at the start of the next block
        }
        noteOnNow(note, velocity, chord, id, gMpeChan);
    }

    void noteOnNow(uint8_t note, uint8_t velocity, int16_t chord, uint16_t id, uint8_t chan)
    {
        if (chan > 16)
        {
            chan = 0;
        }
        if (patch->modActive())
        {
            modNoteOnAll();
        }
        refreshParams();
        if (tapeReq.mode != tapefx::kOff)
        {
            tapeBegin(); // the note of the step takes the run
        }
        else
        {
            tapeMode = tapefx::kOff; // any other note plays at normal speed
        }
        bool reused;
        Voice* v = pickVoice(reused);
        if (!v)
        {
            return;
        }
        ot::audio::SynthNote n;
        n.note = note;
        // The project tuning (core/source/mtTuning.h): the engine's note pitch moves by the key's offset. PERC and T-Type are kits, the note picks the drum:
        // untuned. CHIP reads only the integer note: its offset goes on the bend. Zero for the default tuning, so the sound is what it was
        const int typeId = def ? def->typeId : -1;
        const bool kit = typeId == int(polysynth::InstrumentTypeId::kDrumSynth) || typeId == 0x85;
        const float tuneOff = kit ? 0.0f : tuning::synthOffset(note);
        n.pitch = float(note) + tuneOff;
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
        v->id = id; // a reused (mono legato) voice is the new note's now: the earlier note's off no longer matches it
        v->v.gate = true;
        v->v.fading = false;
        v->fadeLeft = 0;
        v->tuneSt = typeId == int(polysynth::InstrumentTypeId::kChiptuneSynth) ? tuneOff : 0.0f;
        // MPE: a newer note on the member channel takes it over (a note that was not released is a tail now, as a released one is: it keeps its bend and level)
        for (auto& o : voices)
        {
            if (chan != 0 && o.chan == chan)
            {
                o.chan = 0;
            }
        }
        v->chan = chan;
        v->nbend = chan != 0 && !kit ? mpeBend[chan] : int16_t(0);
        v->press = chan != 0 ? mpePress[chan] : uint8_t(127);
        v->v.bend = v->v.bendStart = bend + tapeBendNow() + v->tuneSt + float(v->nbend) * mpeStep;
        v->vel = float(velocity) * (1.0f / 127.0f);
        v->on = true;
        const auto clk = host.clock();
        const uint32_t t0 = clk ? clk() : 0;
        if (chord >= 0)
        {
            chordNoteOn(*v, n, chord);
        }
        else
        {
            engine->noteOn(v->v, params, n);
        }
        v->onCyc = clk ? clk() - t0 : 0;
    }

    // A MIDI Chord step's note-on: the step's chord is the engine's Chord parameter for this note-on only (the engine latches it in
    // note_on, PLUG_EXT_CHORD), a Strum FX the engine's Strum parameter when it has one (else the chord sounds unstrummed). The
    // block's values are put back right after. chord: bits 0-7 the chord, bits 8-14 the strum + 64.
    void chordNoteOn(Voice& v, const ot::audio::SynthNote& n, int16_t chord)
    {
        const ChordParams cp = chordParamsOf(def);
        if (cp.chord < 0 || cp.chord >= params.count)
        {
            engine->noteOn(v.v, params, n);
            return;
        }
        const int strum = (chord >> 8) - 64;
        const int si = (strum != 0 && cp.strum >= 0 && cp.strum < params.count) ? cp.strum : cp.chord;
        float& e = params.end[cp.chord];
        float& s = params.start[cp.chord];
        float& se = params.end[si];
        float& ss = params.start[si];
        const float e0 = e, s0 = s, se0 = se, ss0 = ss;
        e = s = float(chord & 0xFF);
        if (si != cp.chord)
        {
            se = ss = float(strum);
        }
        engine->noteOn(v.v, params, n);
        se = se0;
        ss = ss0;
        e = e0;
        s = s0;
    }

    // id: the note id of the note-off (0: none, matches by pitch alone as before). With an id only the voice that note started is released.
    // Out of line: inlined into the MIDI callback (two call sites) the id compare cost 1,000 B of flash, called it costs 100
    __attribute__((noinline)) void noteOff(uint8_t note, uint16_t id, uint8_t chan)
    {
        if (!engine)
        {
            return;
        }
        host.pendingOff(*this, note, id, chan);
        // New Note Action: a Cut or a Fade needs a record to ramp out while the new note starts on another one. A mono part (Voices 1, an
        // engine with one voice such as PERC and T-Type) has none, it releases as before
        const uint8_t end = voiceCount > 1 && def && def->maxVoices > 1 && (gNoteEnd == 2 || gNoteEnd == 3) ? gNoteEnd : uint8_t(0);
        for (auto& v : voices)
        {
            if (v.on && v.v.gate && v.v.note == note && (id == 0 || v.id == id) && (chan == 0 || v.chan == chan))
            {
                if (v.chan != 0)
                {
                    mpePress[v.chan] = 127; // the finger is up: the next note on the channel starts at full level until its own pressure comes
                    v.chan = 0;             // and the voice is a tail: it keeps the bend and level it had (the bend that comes with the next note is not its)
                }
                v.v.gate = false;
                engine->noteOff(v.v, params);
                if (end)
                {
                    v.fadeLeft = end == 2 ? uint8_t(1) : kNnaFadeBlocks;
                }
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
    bool seenOk = false;      // the last refresh found params.end equal to effective(), params.start equal to params.end and nothing moving
    uint32_t seenSer = 0;     // OpenPatch::effSerial() at that refresh
    // render(): the Volume range and the velocity slope of the patch (assignPatch and engineRestart drop them)
    bool volCached = false;
    bool velCached = false;
    float volMinC = 0.0f;
    float volMaxC = 1.0f;
    float velKC = 0.0f;
    uint32_t velSerC = 0;
    float seenMod[open::kModSlots] = {}; // modOut at that refresh (read only when the live config has the automation on)
    bool startIsEnd = false; // params.start equals params.end bit for bit over params.count (refreshParams, render)
    open::ModState modSt[open::kModSlots];
    float modOut[open::kModSlots] = {};
    uint8_t modSerial = 0;
    uint8_t cfgSerial = 0;
    float bend = 0.0f; // pitch bend in semitones, applied to every voice (SynthVoice::bend)
    // MPE in (the Synth play page): the member bend range as semitones per raw step, the pressure amount 0..1 (0: pressure does nothing),
    // and per member channel the last raw bend and pressure received (the next note on the channel starts from them)
    float mpeRangeSt = 48.0f / 8192.0f; // the member range, in semitones per raw unit
    float mpeStep = 48.0f / 8192.0f;   // the same in key steps of the tuning (mpeRangeSt x tuning::tableScale())
    float pressAmt = 0.0f;
    int16_t mpeBend[17] = {};
    uint8_t mpePress[17] = { 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127 };
    float bendRange = kBendRange; // semitones for a full deflection of the bend message, open::partBendRange
    float morphGain = 1.0f;       // the A/B page: factor on the voice gain target, open::partGain
    float tableSt = 0.0f;         // the row of a running table (open::partTable): semitones on the bend of every voice
    float tableGain = 1.0f;       // ... and its V column as a factor on the voice gain target
    // Tape Stop / Tape Start step FX (core/source/tapeFx.h): the tape speed of the part. It adds semitones to the pitch bend of every voice
    // and a level to its gain, both stepped once per block, and moves on only in rendered blocks.
    tapefx::Mode tapeMode = tapefx::kOff;
    uint32_t tapePos = 0;     // frames run so far
    uint32_t tapeTotal = 0;   // frames of the whole run
    struct TapeRequest
    {
        tapefx::Mode mode = tapefx::kOff; // a step FX that waits for its note-on (or the next render of a sounding part)
        uint32_t frames = 0;
        uint8_t blocks = 0;               // blocks it may still wait
    } tapeReq;
    uint32_t attackCyc = kSeedAttackCycles; // learned (live renders): a note-on over a running voice
    uint32_t voiceCyc = kSeedVoiceCycles;   // learned: one running voice
    uint32_t blockVoices = 0, blockRenderCyc = 0;
    bool blockMoving = false;
    int16_t chordNote = -1; // noteChord: the note whose next note-on carries chordIdx (-1: none)
    int16_t chordIdx = 0;   // the chord in bits 0-7, the strum + 64 in bits 8-14
    // The voice records (4 x 176 B), read and written every block, are a block of their own in the SRAM heap (704 B, under the 1 KB cut of
    // operator new): the rest of the instrument is over 1 KB and lives in the SDRAM arena, where the scattered reads cost about 30 cycles a word
    // (L2). The heap new traps when heap and arena are both full, so the pointer is never null.
    struct VoiceBank
    {
        Voice v[kMaxVoicesPerInstrument];
    };
    std::unique_ptr<VoiceBank> voiceBank{ new VoiceBank };
    Voice (&voices)[kMaxVoicesPerInstrument] = voiceBank->v;
};
#if defined(__arm__)
// The object must stay at or over 1 KB: below it operator new takes it from the SRAM heap and the whole instrument moves there
static_assert(sizeof(OpenInstrument) >= 1024, "OpenInstrument is no longer an SDRAM arena object");
#endif

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
    // not inlined into setBudget (the Synth play page calls it on the device now): 250 B of flash
    __attribute__((noinline)) int sounding() const override
    {
        int n = 0;
        for (const auto& i : instruments)
        {
            if (i)
            {
                for (const Voice& v : static_cast<OpenInstrument&>(*i).voiceSpan())
                {
                    n += v.live();
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
                    if (v.live() && (!best || betterVictim(v, *best)))
                    {
                        best = &v;
                    }
                }
            }
        }
        return best;
    }
    __attribute__((noinline)) bool stealOne() override
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
    float blockTempo(const TempoProvider* provider) override
    {
        if (!bpmFresh)
        {
            bpmBlock = 0.0f;
            if (provider && *provider)
            {
                const auto bpm = (*provider)();
                if (bpm.has_value())
                {
                    bpmBlock = *bpm;
                }
            }
            bpmFresh = true;
        }
        return bpmBlock;
    }
    // PolySynth::update opens every block (open::synthBlock). A part rendered with a lower or equal index than the last one starts a block as
    // well, for a caller that renders its parts without that call (the host tests and benches).
    void newBlock() { bpmFresh = false; }
    void noteStolen(bool fastKill) override
    {
        stats.steals++;
        stats.fastKills += fastKill;
    }
    bool admit(OpenInstrument& inst, uint8_t note, uint8_t velocity, int16_t chord, uint16_t id) override
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
            // a step block runs longer before the synth render than the last block did (the rest of the sequencer step, Patch and
            // CC FX, the graph before the synth): stepPre, learned from earlier step blocks, is added
            const uint32_t guess = std::max(now, prevPre);
            if (preGuess == 0)
            {
                preGuess = guess;
            }
            const uint32_t est = guess + stepPre + rest + extra + cost;
            shot.estMax = std::max(shot.estMax, est);
            // the next block: a block without note-ons as the last one was, plus what already waits for it and this note-on.
            // When it would be over the limit too and is no cheaper, the note-on starts now: waiting would only move the load
            const uint32_t steady = prevTotal > prevExtra ? prevTotal - prevExtra : 0;
            const uint32_t estNext = steady + queuedCost + cost;
            if (est <= admitCycles || (estNext > admitCycles && est < estNext))
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
        pend[pendN++] = Pending{&inst, note, velocity, false, chord, c(), id, gMpeChan};
        queuedCost += cost;
        stats.deferred++;
        shot.deferred++;
        if (++deferredNow > stats.deferMaxBlock)
        {
            stats.deferMaxBlock = deferredNow;
        }
        return false;
    }
    bool pendingOff(OpenInstrument& inst, uint8_t note, uint16_t id, uint8_t chan) override
    {
        bool hit = false;
        for (int i = 0; i < pendN; i++)
        {
            if (pend[i].inst == &inst && pend[i].note == note && (id == 0 || pend[i].id == id) && pend[i].chan == chan)
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
            mine[i].inst->startQueued(mine[i].note, mine[i].vel, mine[i].off, mine[i].chord, mine[i].id, mine[i].chan);
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
        queuedCost = 0;
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
            q[i].inst->startQueued(q[i].note, q[i].vel, q[i].off, q[i].chord, q[i].id, q[i].chan);
        }
        shot.flushed = uint32_t(n);
    }
    void setAdmit(uint32_t permille) { admitCycles = admitCyclesFor(std::clamp<uint32_t>(permille, 500, 1000)); }
    uint32_t admit() const
    {
        return uint32_t((uint64_t(admitCycles) * 1000u + ot::audio::CpuMeter::kBudgetCycles / 2) / ot::audio::CpuMeter::kBudgetCycles);
    }
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
        s.stepPreUs = stepPre / kCyclesPerUs;
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
            if (preGuess != 0)
            {
                // how much later the synth render started than the first admission check guessed (decaying max over step blocks)
                const uint32_t miss = pre > preGuess ? pre - preGuess : 0;
                stepPre = std::max(miss, stepPre - (stepPre >> 4));
                preGuess = 0;
            }
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
    // Stop with a New Note Action in use: every part starts its queued note-ons and releases every gated voice (a Continue note whose parked
    // off did not come cannot hang). Fading voices keep their ramp
    void releaseAllParts()
    {
        for (auto& p : instruments)
        {
            if (p)
            {
                auto& oi = static_cast<OpenInstrument&>(*p);
                flushPending(oi);
                oi.releaseGated();
            }
        }
    }
    OpenInstrument* instrumentAt(int part)
    {
        return part >= 0 && part < int(instruments.size()) && instruments[part] ? &static_cast<OpenInstrument&>(*instruments[part]) : nullptr;
    }
    const EngineDef* partPatch(int part, const float** v, int* n)
    {
        if (part < 0 || part >= int(instruments.size()) || !instruments[part]) return nullptr;
        return static_cast<OpenInstrument&>(*instruments[part]).livePatch(v, n);
    }
    int partWaveRef(int part, int param, char* out, size_t cap)
    {
        if (part < 0 || part >= int(instruments.size()) || !instruments[part]) return -1;
        return static_cast<OpenInstrument&>(*instruments[part]).waveRef(param, out, cap);
    }
    uint8_t partAuto(int part, float* v)
    {
        if (part < 0 || part >= int(instruments.size()) || !instruments[part])
        {
            return 0;
        }
        return static_cast<OpenInstrument&>(*instruments[part]).partAuto(v);
    }
    bool partIdle(int part)
    {
        if (part < 0 || part >= int(instruments.size()) || !instruments[part])
        {
            return false;
        }
        const bool idle = static_cast<OpenInstrument&>(*instruments[part]).idle();
        busyPart = idle ? -1 : part; // the render of this part right after skips its own idle() call
        return idle;
    }
    void setOffline(bool offline)
    {
        busyPart = -1;
        // a note-on still waiting starts now: the offline render admits every note-on and must see none in a queue
        Pending q[kPendingMax];
        const int n = pendN;
        std::copy_n(pend, n, q);
        pendN = 0;
        for (int i = 0; i < n; i++)
        {
            q[i].inst->startQueued(q[i].note, q[i].vel, q[i].off, q[i].chord, q[i].id, q[i].chan);
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
        bpmFresh = false;
        lastPart = -1;
        clear(buffer);
        // Mono engines (EngineDef::monoOut) add into L only: they render first, R takes a copy (both channels hold the same sums
        // then), the stereo engines add to both after that
        bool anyMono = false;
        for (auto& i : instruments)
        {
            if (i && static_cast<OpenInstrument&>(*i).monoOut())
            {
                static_cast<OpenInstrument&>(*i).render(buffer[0].data(), buffer[1].data());
                anyMono = true;
            }
        }
        if (anyMono)
        {
            std::memcpy(buffer[1].data(), buffer[0].data(), sizeof(float) * kAudioBufferSize);
        }
        for (auto& i : instruments)
        {
            if (i && !static_cast<OpenInstrument&>(*i).monoOut())
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
        // The aux buffers (the send taps of a stereo patch that renders aux sends) stay as they are: no open patch has rendersAuxSends
        // (open_cold.cpp never sets it), so PolySynth::update never reads them, and clearing 2 x 512 B per part and block cost 5 us.
        (void)bufferAux;
        float* l = bufferMain[0].data();
        float* r = bufferMain[1].data();
        OpenInstrument* inst = (instrumentId >= 0 && instrumentId < int(instruments.size()) && instruments[instrumentId])
                                 ? static_cast<OpenInstrument*>(instruments[instrumentId].get())
                                 : nullptr;
        // A mono engine (EngineDef::monoOut) adds into L only: R is neither cleared nor written nor read, and mono output needs no fold
        const bool mono = inst && inst->monoOut();
        clearChannel(l);
        if (!mono)
        {
            clearChannel(r);
        }
        const bool busyKnown = instrumentId == busyPart;
        busyPart = -1;
        if (instrumentId <= lastPart)
        {
            bpmFresh = false;
        }
        lastPart = instrumentId;
        if (inst)
        {
            inst->render(l, r, busyKnown);
            FF_HOST_PART_RENDERED(instrumentId, l, mono ? l : r);   // a mono engine leaves R alone: its R is L
            if (!mono)
            {
                // mono output: tracker-ce reads the left buffer only
#pragma GCC unroll 8
                for (int i = 0; i < kAudioBufferSize; i++)
                {
                    l[i] = 0.5f * (l[i] + r[i]);
                }
            }
        }
    }

    void updateAudio() override {}
    void updateMain() override {}

  private:
    static void clearChannel(float* p)
    {
        // 8 stores per turn (std::fill compiled to one store per loop turn)
        static_assert(kAudioBufferSize % 8 == 0);
        for (int i = 0; i < kAudioBufferSize; i += 8)
        {
            p[i] = 0.0f;
            p[i + 1] = 0.0f;
            p[i + 2] = 0.0f;
            p[i + 3] = 0.0f;
            p[i + 4] = 0.0f;
            p[i + 5] = 0.0f;
            p[i + 6] = 0.0f;
            p[i + 7] = 0.0f;
        }
    }
    static void clear(AudioBufferStereoRef buffer)
    {
        for (auto& channel : buffer)
        {
            clearChannel(channel.data());
        }
    }

    InstrumentTypeRegistry registry;
    std::unique_ptr<FileSystem> fileSystems[4]; // by FileSystemId
    std::vector<std::shared_ptr<Instrument>> instruments;
    TempoProvider tempo;
    ot::audio::CpuMeter meter;
    int busyPart = -1; // partIdle(part) found the part busy and its render has not come yet
    bool bpmFresh = false;  // bpmBlock holds the tempo of this block
    float bpmBlock = 0.0f;
    int lastPart = -1;      // the part rendered last (renderAudioBuffer)

    // note-on admission state (audio interrupt)
    struct Pending
    {
        OpenInstrument* inst;
        uint8_t note;
        uint8_t vel;
        bool off;
        int16_t chord;  // the MIDI Chord value (and strum) for a paraphonic engine, -1 none
        uint32_t asked; // cycle counter when the note-on came
        uint16_t id;    // the note id of the note-on (open::noteTag), 0 none
        uint8_t chan;   // the MPE member channel of the note-on (open::mpeNote), 0 none
    };
    Pending pend[kPendingMax];
    int pendN = 0;
    uint32_t queuedCost = 0;        // estimated cycles of the note-ons queued in this block (they start in the next)
    int pendSlots = 0;              // queued note-ons of this block that take a free pool voice (no victim faded for them)
    ot::audio::CpuMeter::CycleFn clk = nullptr;
    bool inBlock = false;
    uint32_t blockStart = 0;
    uint32_t pre = 0;               // this block: cycles from its start to the synth render
    uint32_t extra = 0;             // this block: estimated cycles of the note-ons started in it
    uint32_t carry = 0;             // note-ons started between blocks, counted in the next
    uint32_t deferredNow = 0;
    uint32_t prevTotal = 0, prevPre = 0, prevExtra = 0;
    uint32_t preGuess = 0;          // this block: the render start the first admission check assumed (0: no check yet)
    uint32_t stepPre = 0;           // learned: how much later than that the render starts in a block with note-ons
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

void open::synthBlock()
{
    if (gActive)
    {
        gActive->newBlock();
    }
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
uint8_t open::partAuto(int part, float* v) { return gActive ? gActive->partAuto(part, v) : 0; }
open::ChordParams open::chordParamsOf(const EngineDef* def)
{
#ifdef SYNTH_ENGINE_ACD
    if (def == &kAcdDef)
    {
        return ChordParams{int8_t(ot::audio::acd::kChord), int8_t(ot::audio::acd::kChordMode), int8_t(ot::audio::acd::kStrum)};
    }
#endif
    return plugChordParams(def);
}
#ifndef FEATURE_PLUG_HOST
open::ChordParams open::plugChordParams(const EngineDef*) { return ChordParams{}; }
bool open::waveListName(int, char*, size_t) { return false; }
bool open::waveFilesAbsent() { return false; }
bool open::waveListHas(const char*) { return true; }
#endif
bool open::partChordPara(int part, uint8_t chord)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i && i->chordPara(chord);
}
void open::partTape(int part, bool start, uint32_t frames)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->tape(start, frames);
    }
}
void open::noteTag(uint16_t id, uint8_t endMode)
{
    gNoteTag = id;
    gNoteEnd = endMode;
}
void open::mpeNote(uint8_t chan)
{
    gMpeChan = chan;
}
void open::partMpe(int part, float memberRange, float pressAmount)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->setMpe(memberRange, pressAmount);
    }
}
void open::partNoteBend(int part, uint8_t chan, int raw)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->noteBend(chan, raw);
    }
}
void open::partNotePress(int part, uint8_t chan, uint8_t press)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->notePress(chan, press);
    }
}
void open::partBendRange(int part, float semitones)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->setBendRange(semitones);
    }
}
void open::partTable(int part, int semis, float gain)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->setTable(float(semis), gain);
    }
}
void open::partGain(int part, float gain)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->setMorphGain(gain);
    }
}
bool open::partActive(int part)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i && i->active();
}
int open::partSounding(int part)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i ? i->soundingVoices() : 0;
}
void open::partNoteChord(int part, uint8_t note, uint8_t chord, int8_t strum)
{
    if (auto* i = gActive ? gActive->instrumentAt(part) : nullptr)
    {
        i->noteChord(note, chord, strum);
    }
}
int open::partWaveRef(int part, int param, char* out, size_t cap)
{
    return gActive ? gActive->partWaveRef(part, param, out, cap) : -1;
}

const open::EngineDef* open::partPatch(int part, const float** values, int* count)
{
    return gActive ? gActive->partPatch(part, values, count) : nullptr;
}
#if defined(OPEN_HOST_TEST) || defined(UI_SHOT_HOST)   // read only test accessors: the host tests and ui_shot
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
bool open::testVoice(int part, int slot, bool& gate, int& note, int& id)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i || slot < 0 || slot >= int(i->voiceSpan().size()) || !i->voiceSpan()[slot].on)
    {
        return false;
    }
    const Voice& v = i->voiceSpan()[slot];
    gate = v.v.gate;
    note = v.v.note;
    id = v.id;
    return true;
}
#endif
void open::releaseAllParts()
{
    if (gActive)
    {
        gActive->releaseAllParts();
    }
}
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

#ifdef UI_SHOT_HOST
// The simulator's read only views of a part (ui_shot, host_poke.cpp): they read the Voice records of this file
namespace polysynth::open { int hostVoices(int part, char* buf, unsigned n); }
// ui_shot only (host_poke.cpp `poke voices`, synth_nna_sim.sh): the voices of part `part` that are on, "cap C n=N [slot:gate:note:id:fade:nnaLeft ...]", -1 for no such part
int polysynth::open::hostVoices(int part, char* buf, unsigned n)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    int at = 0, cnt = 0;
    char list[200] = {};
    int k = 0;
    for (const Voice& v : i->voiceSpan())
    {
        if (v.on && at < 170)
        {
            at += snprintf(list + at, sizeof(list) - at, " %d:%d:%d:%d:%d:%d", k, int(v.v.gate), int(v.v.note), int(v.id), int(v.v.fading), int(v.fadeLeft));
            cnt++;
        }
        k++;
    }
    return snprintf(buf, n, "n=%d [%s ]", cnt, list);
}
namespace polysynth::open { int hostPart(int part, float* bend, float* range, int* voiceCount); }
// ui_shot only (synth_play_sim.sh): the live pitch bend (semitones), the bend range and the Voices of part `part`, -1 for no such part
int polysynth::open::hostPart(int part, float* bend, float* range, int* voiceCount)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    *bend = i->testBend();
    *range = i->testBendRange();
    *voiceCount = i->getVoiceCount();
    return 0;
}
namespace polysynth::open { int hostMpeVoices(int part, char* buf, size_t n); }
// ui_shot only (synth_mpe_sim.sh, "poke mpevoices"): the voices of part `part` that are on as " slot:note:chan:gate:nbend:press:gain" (gain x 1000 of the last block), -1 for no such part
int polysynth::open::hostMpeVoices(int part, char* buf, size_t n)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    return i->testMpe(buf, n);
}
namespace polysynth::open { int hostGain(int part, float* gain, float* voiceGain); }
// ui_shot only (synth_play_sim.sh A/B rows, "poke abstate"): the morph factor of part `part` and the gain of its first voice that is on (-1: none)
int polysynth::open::hostGain(int part, float* gain, float* voiceGain)
{
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    if (!i)
        return -1;
    *gain = i->testGain();
    *voiceGain = i->testVoiceGain();
    return 0;
}
namespace polysynth::open { int hostCapSim(int part, int maxVoices); }
// ui_shot only (synth_play_sim.sh P14, "poke capsim"): part `part` runs on a copy of its engine definition that declares `maxVoices` voices,
// what a plugin engine with max_voices 4 is (the real tables are read only), -1 for no such part
int polysynth::open::hostCapSim(int part, int maxVoices)
{
    static EngineDef copy[3];
    auto* i = gActive ? gActive->instrumentAt(part) : nullptr;
    return i && part >= 0 && part < 3 && i->testCap(copy[part], maxVoices) ? 0 : -1;
}
#endif
