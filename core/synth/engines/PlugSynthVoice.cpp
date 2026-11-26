// Plugin synth engines on the stock Tracker+ / Mini synth stack, see PlugSynthVoice.h (IMX port P7 H5)
#include "PlugSynthVoice.h"

#if defined(FEATURE_SYNTH) && !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX)

#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "PolySynth/sdk/InstrumentVoice.h"
#include "audio/synth_engine.h"   // synth-open/ot: SynthVoice, SynthParams, SynthNote, SynthSlotScratch (the engines' interface)
#include "plug_abi.h"
#include "plug_chord.h"   // PLUG_CHORD_COUNT: the 77 chords of the MIDI Chord FX
#include "../../source/mtTuning.h"   // the project tuning (Project Settings, Scala): the same note pitch as the MK66 shim

#if !defined(PLUS_SYNTH_HARNESS)
#include "plug_imx.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

namespace polysynth
{
namespace plugimx
{
namespace
{
static_assert(kAudioBufferSize == int(ot::audio::kBlock), "one lib block is one engine block");
constexpr uint32_t kLayout = PLUG_SYNTH_LAYOUT(sizeof(ot::audio::SynthVoice), sizeof(ot::audio::SynthParams),
                                               sizeof(ot::audio::SynthNote), sizeof(ot::audio::SynthSlotScratch));
constexpr int kStatesPerSlot = 3;   // one per synth part that plays the engine (kSynthParts)

// What one instrument (part) of the engine shares between its voices: the parameters and the slot scratch of the OpenTracker
// interface. Keyed by the instrument's patch (the voices of one instrument share it), made at the first voice call of that patch
struct PartState
{
    const Patch* patch = nullptr;
    bool prepared = false;   // prepare ran for this block (cleared after the instrument's post process)
    bool valid = false;      // params.start holds a block's values
    ot::audio::SynthSlotScratch scratch;
    ot::audio::SynthParams params;
    float bend = 0.f;   // the instrument's pitch bend in semitones (kPitchBendChange), every voice of the part plays it
};

struct SlotEngine
{
    const plug_ext_synth* ext = nullptr;
    uint32_t gen = 0;   // bumped by every detach: a voice or a type of an earlier engine in this slot plays nothing
    int typeId = -1;
    void* work = nullptr;
    PartState* states[kStatesPerSlot] = {};
    float tempo = 0.f;
    // PLUG_EXT_CHORD (paraphony): the engine's Chord, Chord Mode and Strum parameters (-1 none) and the last value of its Chord enum
    int8_t chord = -1, mode = -1, strum = -1;
    uint8_t last = 0;
};

// A MIDI Chord step of a part at Para (noteChord, the sequencer): the part's next note on of `note` carries the chord and the strum.
// Keyed by the part's MIDI channel, which the lib hands the voice in event.channel (Synth.cpp setMidiChannel(part), probe C0)
struct PendingChord
{
    int16_t note = -1;   // -1: none
    int16_t chord = 0;   // bits 0-7 the chord, bits 8-14 the strum + 64 (open_synth.cpp noteChord)
};
constexpr int kParts = 3;   // kSynthParts: channels 0..2

// The sequencer's note id and end mode of the send in progress (noteTag, open_synth.cpp gNoteTag / gNoteEnd): 0 outside a send.
// The lib hands the event to the voices inside the send (probe N0), so the voice reads them in onMidiEvent
struct NoteTag
{
    uint16_t id = 0;
    uint8_t end = 0;   // 0 Release, 2 Cut, 3 Fade (the New Note Action of the replaced note)
};
// Fade: 120 ms = 41 blocks of 128 frames at 44.1 kHz, linear in amplitude, Cut is its last block (open_synth.cpp kNnaFadeBlocks)
constexpr uint8_t kNnaFadeBlocks = 41;

// MPE in of the Synth play page (mpeNote, partMpe, partNoteBend, partNotePress: open_synth.cpp gMpeChan, setMpe, noteBend, notePress).
// A voice cannot reach the other voices of its part, so the part keeps the last bend and pressure of each member channel and a counter
// of the notes started on it: the voice of the newest note on a channel follows it (render), an older one is a tail and keeps its values.
// The master channel's pressure (channel 1) goes to every voice of the part through a counter of its own
struct MpePart
{
    float rangeSt = 48.0f / 8192.0f;   // semitones of a raw step of the member bend (48 st: the MPE default)
    float pressAmt = 0.0f;             // 0..1, 0: pressure does nothing
    int16_t bend[17] = {};
    uint8_t press[17] = { 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127 };
    uint8_t owner[17] = {};            // bumped by every note on of the channel
    uint8_t pressSeq[17] = {};         // bumped by every pressure of the channel: a voice takes it once, the master's may come in between
    uint8_t master = 127, masterSeq = 0;
};

InstrumentTypeRegistry* g_registry;
// A full pitch bend is this many semitones, the lib's own range for its engines (synthPlayRouter.cpp kLibBendSemis), so the wheel of the
// Synth play page and the Table transpose (tables_run.inc kTableLibBendSemis) move a plugin engine as far as a built-in one
constexpr float kLibBendSemis = 2.0f;
#if defined(PLUS_SYNTH_HARNESS)
// harness only (never in an image): a bit per MidiEvent::Type that reached PlugVoice::onMidiEvent, the probe of what the closed lib forwards
extern "C" uint32_t h_plug_evseen;
uint32_t h_plug_evseen;
extern "C" int h_plug_chan;   // the event.channel of the last note on that reached PlugVoice (-1 none), the part's channel probe
int h_plug_chan = -1;
// the note-off routing probe: note offs that reached a PlugVoice, the ones that found their voice gated on that pitch, note ons, the
// note ons that reused a sounding voice
extern "C" int h_plug_offs, h_plug_offhit, h_plug_ons, h_plug_reused, h_plug_tag;
int h_plug_tag = -1;   // the note id (noteTag) the last note on saw: the lib delivers inside the send when it is the id set around it
int h_plug_offs, h_plug_offhit, h_plug_ons, h_plug_reused;
#endif
#if defined(PLATFORM_IMX) && !defined(UI_SHOT_HOST)
// SRAM_OC on IMX (CPU only): its 96 B of initialised .data sat 184 B under the 0x800 boundary that qh_buffer pins .bss to on the Mini,
// 185 B more would have moved .bss up by 2,048 B (synth-open/tools/imx_sram_cliff.py)
__attribute__((section(".data.$RAM3"))) SlotEngine g_slots[kSlots];
__attribute__((section(".data.$RAM3"))) PendingChord g_pending[kParts];   // 12 B, CPU only, with g_slots out of the DTC
__attribute__((section(".data.$RAM3"))) NoteTag g_tag;
__attribute__((section(".data.$RAM3"))) MpePart g_mpe[kParts];   // 279 B, CPU only
__attribute__((section(".data.$RAM3"))) uint8_t g_mpeChan;        // the member channel (2..16) of the note on or off being sent, 0 outside
#else
SlotEngine g_slots[kSlots];
PendingChord g_pending[kParts];
NoteTag g_tag;
MpePart g_mpe[kParts];
uint8_t g_mpeChan;
#endif
// R of the engine for the voice being rendered (the lib renders the voices one after the other in the audio interrupt)
float g_right[kAudioBufferSize];

PartState* stateFor(SlotEngine& s, const Patch* patch)
{
    for (auto* st : s.states)
        if (st && st->patch == patch) return st;
    for (auto*& st : s.states)
        if (!st || !st->patch)
        {
            if (!st) st = new (std::nothrow) PartState();
            if (!st) return nullptr;
            *st = PartState{};
            st->patch = patch;
            st->params.engine = s.typeId;
            st->params.count = int(std::min<uint32_t>(s.ext->param_count, uint32_t(ot::audio::kSynthParamMax)));
            for (int i = 0; i < st->params.count; i++) st->params.end[i] = st->params.start[i] = s.ext->params[i].def;
            if (s.ext->slot_reset) s.ext->slot_reset(&st->scratch);
            return st;
        }
    return s.states[0];   // more instruments than parts: share the first (never on the unit, 3 parts)
}

class PlugVoice final : public InstrumentVoice
{
  public:
    PlugVoice(int slot, uint32_t gen) : slot(slot), gen(gen)
    {
#if defined(PLUS_SYNTH_HARNESS)
        for (auto*& r : h_voices)
            if (!r) { r = this; break; }
#endif
    }
#if defined(PLUS_SYNTH_HARNESS)
    ~PlugVoice() override
    {
        for (auto*& r : h_voices)
            if (r == this) r = nullptr;
    }
    // the MPE probe (h2_mpe_voice): the sounding voices, as open_synth.cpp's mpevoices poke
    static PlugVoice* h_voices[32];
    int16_t hBend() const { return nbend; }
    uint8_t hNote() const { return note; }
    uint8_t hChan() const { return chan; }
    bool hGate() const { return v.gate; }
    uint8_t hPress() const { return press; }
    bool hOn() const { return on; }
    uint8_t hPart() const { return part; }
#endif

    void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) override
    {
        SlotEngine* s = engine();
        PartState* st = s ? stateFor(*s, getPatch()) : nullptr;
        if (!st) return;
        tempo(*s, ctx.tempo);
#if defined(PLUS_SYNTH_HARNESS)
        h_plug_evseen |= 1u << unsigned(event.type);
        if (event.type == MidiEvent::Type::kNoteOn) h_plug_chan = event.channel;
        if (event.type == MidiEvent::Type::kNoteOn) h_plug_tag = g_tag.id;
        if (event.type == MidiEvent::Type::kNoteOff || (event.type == MidiEvent::Type::kNoteOn && event.data.note.velocity == 0))
        {
            h_plug_offs++;
            h_plug_offhit += on && v.gate && event.data.note.note == note;
        }
        else if (event.type == MidiEvent::Type::kNoteOn)
        {
            h_plug_ons++;
            h_plug_reused += on;
        }
#endif
        if (event.type == MidiEvent::Type::kPitchBendChange)
        {
            // the lib hands the instrument's bend to its voices: kept per part, so a voice started later plays it too (render takes it)
            st->bend = float(event.data.pitchBendChange.value) * (kLibBendSemis * (1.0f / 8192.0f));
            return;
        }
        if (event.type == MidiEvent::Type::kNoteOn && event.data.note.velocity > 0)
        {
            const bool reused = on;
            if (!reused)
            {
                v = ot::audio::SynthVoice{};
                std::memset(v.state, 0, sizeof v.state);
                gain = 0.f;
            }
            ot::audio::SynthNote n;
            n.note = event.data.note.note;
            // The project tuning moves the note's pitch by the key's offset, as the MK66 shim (open_synth.cpp noteOn): PERC and T-Type
            // (0x85) are kits, the note picks the drum, untuned. CHIP reads only the integer note: its offset goes on the bend
            const bool kit = s->typeId == int(InstrumentTypeId::kDrumSynth) || s->typeId == 0x85;
            const float tuneOff = kit ? 0.0f : tuning::synthOffset(n.note);
            n.pitch = float(n.note) + tuneOff;
            tuneSt = s->typeId == int(InstrumentTypeId::kChiptuneSynth) ? tuneOff : 0.0f;
            vel = float(event.data.note.velocity) * (1.0f / 127.0f);
            n.velocity = s->ext->volume_param < 0 ? vel : 1.0f;
            n.reused = reused;
            n.overlap = reused && v.gate;
            // a pending MIDI Chord of the part is for this note on only, any note on of the part clears it (open_synth.cpp noteOn)
            int chord = -1;
            if (event.channel < kParts)
            {
                PendingChord& pc = g_pending[event.channel];
                if (pc.note == n.note) chord = pc.chord;
                pc.note = -1;
            }
            v.slotScratch = &st->scratch;
            id = g_tag.id;
            fadeLeft = 0;
            // MPE: the note takes its member channel's last bend (not on a kit, the note picks the drum) and pressure, the older note of
            // the channel becomes a tail (open_synth.cpp noteOn)
            part = event.channel < kParts ? uint8_t(event.channel) : uint8_t(0);
            MpePart& m = g_mpe[part];
            chan = event.channel < kParts && g_mpeChan >= 2 && g_mpeChan <= 16 ? g_mpeChan : uint8_t(0);
            if (chan) owner = ++m.owner[chan];
            nbend = chan && !kit ? m.bend[chan] : int16_t(0);
            press = chan ? m.press[chan] : uint8_t(127);
            masterSeen = m.masterSeq;
            pressSeen = m.pressSeq[chan];
            const float mpeSt = nbend != 0 ? float(nbend) * (m.rangeSt * tuning::tableScale()) : 0.0f;
            v.note = n.note;
            v.gate = true;
            v.fading = false;
            if (!reused) v.bend = v.bendStart = st->bend + tuneSt + mpeSt;   // a new voice starts on the bend, no ramp (open_synth.cpp noteOn)
            on = true;
            note = n.note;
            if (chord >= 0 && s->chord >= 0 && s->chord < st->params.count)
                chordNoteOn(*s, *st, n, chord);
            else
                s->ext->note_on(&v, &st->params, &n);
        }
        else if ((event.type == MidiEvent::Type::kNoteOff || event.type == MidiEvent::Type::kNoteOn) && on && v.gate &&
                 event.data.note.note == note && (g_tag.id == 0 || id == 0 || id == g_tag.id) && (g_mpeChan == 0 || chan == g_mpeChan))
        {
            if (chan)
            {
                if (followsChan()) g_mpe[part].press[chan] = 127;   // the finger is up: the channel's next note starts at full level
                chan = 0;   // a tail: it keeps the bend and the level it had
            }
            // a note id ends only the voice its note on started (the lib hands a note off to every voice of the pitch, probe N1). A
            // voice without an id (a live note, a strummed chord note) ends on any note off of its pitch, as before
            v.gate = false;
            s->ext->note_off(&v, &st->params);
            if ((g_tag.end == 2 || g_tag.end == 3) && s->ext->max_voices > 1) fadeLeft = g_tag.end == 2 ? uint8_t(1) : kNnaFadeBlocks;
        }
    }

    void onParamChange(int paramId, Patch::optional_paramvalue_t value) override
    {
        SlotEngine* s = engine();
        PartState* st = s ? stateFor(*s, getPatch()) : nullptr;
        if (!st || !value || paramId < 0 || paramId >= st->params.count) return;
        if (paramId == s->ext->volume_param)
        {
            // The Velocity macro (M7) targets Volume per voice: the lib sends this voice's value, the part's params take the value
            // without the per-voice macros (the voice gain applies the velocity, renderVoiceAudio, as the MK66 shim)
            value = getPatch()->getParamFinalValue(paramId);
            if (!value) return;
        }
        if (st->params.end[paramId] != *value)
        {
            st->params.end[paramId] = *value;
            st->params.serial++;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, std::optional<AudioBufferStereoRef>, AudioBufferStereoRef out,
                          std::span<AudioBufferRef>) override
    {
        // The part is mono (the stock mixer takes a stereo patch only with aux sends, synth_PolySynth.cpp): the engine adds into L and
        // a scratch R, the voice output is 0.5 (L + R), the fold of the MK66 shim (open_synth.cpp renderAudioBuffer)
        float* l = out[0].data();
        std::fill(l, l + kAudioBufferSize, 0.0f);
        SlotEngine* s = engine();
        PartState* st = s && on ? stateFor(*s, getPatch()) : nullptr;
        if (!st)
        {
            on = false;
            return;
        }
        tempo(*s, ctx.tempo);
        prepare(*s, *st);
        const int volParam = s->ext->volume_param;
        // The MK66 shim's gain rule (open_synth.cpp renderAudioBuffer): clamp(vol * (1 + k * (vel - 1)), min, max) with k the Velocity
        // macro's depth on Volume (open_patch.h velocityAmount: the sum of its modifiers on Volume, 1 by default, makeMetadata).
        // Written as the MK66 does it: vol * vel is the same value in exact maths but not in float (1 LSB at low velocities)
        float target = 1.0f;
        if (volParam >= 0 && volParam < st->params.count)
        {
            const plug_synth_param& p = s->ext->params[volParam];
            target = std::clamp(st->params.end[volParam] * (1.0f + velocityAmount(volParam) * (vel - 1.0f)), p.min, p.max);
        }
        else
        {
            // No Volume (T-Type, PERC): the voice gain is the note velocity and the engine divides it back out per hit (ttype.cpp render
            // g / vLast), as the MK66 shim does (open_synth.cpp renderAudioBuffer: hasVol false, vol 1, velK 1, range 0..1)
            target = std::clamp(1.0f * (1.0f + 1.0f * (vel - 1.0f)), 0.0f, 1.0f);
        }
        MpePart& m = g_mpe[part];
        if (m.masterSeq != masterSeen)
        {
            masterSeen = m.masterSeq;
            press = m.master;   // the master channel's pressure: every voice of the part
        }
        if (followsChan())
        {
            if (!kitEngine(*s)) nbend = m.bend[chan];
            if (v.gate && m.pressSeq[chan] != pressSeen)
            {
                pressSeen = m.pressSeq[chan];
                press = m.press[chan];   // only a key that is down follows its pressure, a tail keeps its level
            }
        }
        if (m.pressAmt != 0.0f)
        {
            target *= 1.0f + m.pressAmt * (float(press) * (1.0f / 127.0f) - 1.0f);   // MPE pressure: full at 127, 1 - amount at 0
        }
        const uint8_t nnaLeft = fadeLeft;
        if (nnaLeft)
        {
            // New Note Action Cut / Fade: the gain ramps down block by block, 0 in the last one (Cut is only that block)
            target *= float(nnaLeft - 1) * (1.0f / float(kNnaFadeBlocks));
            fadeLeft = uint8_t(nnaLeft - 1);
        }
        const float g0 = gain;
        const float gInc = (target - g0) * (1.0f / float(kAudioBufferSize));
        gain = target;
        v.bendStart = v.bend;
        v.bend = st->bend + tuneSt;
        if (nbend != 0)
        {
            v.bend += float(nbend) * (m.rangeSt * tuning::tableScale());   // MPE member bend, kept by a tail
        }
        std::fill(g_right, g_right + kAudioBufferSize, 0.0f);
        if (!s->ext->render(&v, &st->params, g0, gInc, l, g_right) || nnaLeft == 1) on = false;
        for (int i = 0; i < kAudioBufferSize; i++) l[i] = 0.5f * (l[i] + g_right[i]);
    }

    void renderPostProcessAudio(const InstrumentVoiceContext&, AudioBufferStereoRef out) override
    {
        SlotEngine* s = engine();
        PartState* st = s ? stateFor(*s, getPatch()) : nullptr;
        if (!st) return;
        prepare(*s, *st);
        if ((s->ext->flags & PLUG_SYNTH_SLOT_FX) && s->ext->slot_post)
        {
            // the slot FX (JU6 chorus) runs on the folded mix in both channels and is folded again
            float* l = out[0].data();
            std::memcpy(g_right, l, sizeof(float) * kAudioBufferSize);
            s->ext->slot_post(slot, &st->scratch, &st->params, l, g_right);
            for (int i = 0; i < kAudioBufferSize; i++) l[i] = 0.5f * (l[i] + g_right[i]);
        }
        // the block is done: the next one starts at these values
        std::memcpy(st->params.start, st->params.end, sizeof(float) * size_t(st->params.count));
        st->params.moving = false;
        st->prepared = false;
    }

    bool isActive() override { return on && engine(); }
    bool handlesNote(note_t n) override { return on && v.gate && note == n; }
    bool handlesAnyNote() override { return on && v.gate; }
    tiny_optional<note_t> getLastNote() override { return on ? tiny_optional<note_t>(note) : tiny_optional<note_t>(); }
    tiny_optional<float> getCurrentNoteVelocity() override { return on ? tiny_optional<float>(vel) : tiny_optional<float>(); }

  private:
    SlotEngine* engine() const { return g_slots[slot].ext && g_slots[slot].gen == gen ? &g_slots[slot] : nullptr; }
    // A MIDI Chord step's note on (open_synth.cpp chordNoteOn): the step's chord is the engine's Chord parameter for this note on only
    // (the engine latches it in note_on, PLUG_EXT_CHORD), a Strum FX the engine's Strum parameter when it has one. Put back right after
    void chordNoteOn(const SlotEngine& s, PartState& st, const ot::audio::SynthNote& n, int chord)
    {
        const int strum = (chord >> 8) - 64;
        const int si = (strum != 0 && s.strum >= 0 && s.strum < st.params.count) ? s.strum : s.chord;
        float& e = st.params.end[s.chord];
        float& b = st.params.start[s.chord];
        float& se = st.params.end[si];
        float& sb = st.params.start[si];
        const float e0 = e, b0 = b, se0 = se, sb0 = sb;
        e = b = float(chord & 0xFF);
        if (si != s.chord) se = sb = float(strum);
        s.ext->note_on(&v, &st.params, &n);
        e = e0;
        b = b0;
        se = se0;
        sb = sb0;
    }
    float velocityAmount(int volParam) const
    {
        const auto& macros = getPatch()->getMetadata().macros;
        constexpr size_t kVel = size_t(Patch::SystemMacroId::kVelocity);
        float k = 0.0f;
        if (kVel < macros.size())
            for (const auto& mod : macros[kVel].modifiers)
                if (mod.paramId && *mod.paramId == volParam) k += mod.amount;
        return k;
    }
    static void tempo(SlotEngine& s, float bpm)
    {
        if (bpm != s.tempo && bpm > 0.f && s.ext->set_tempo)
        {
            s.ext->set_tempo(bpm);
            s.tempo = bpm;
        }
    }
    static void prepare(SlotEngine& s, PartState& st)
    {
        if (st.prepared) return;
        if (!st.valid)
        {
            std::memcpy(st.params.start, st.params.end, sizeof(float) * size_t(st.params.count));
            st.valid = true;
        }
        st.params.moving = std::memcmp(st.params.start, st.params.end, sizeof(float) * size_t(st.params.count)) != 0;
        if (s.ext->prepare) s.ext->prepare(&st.scratch, &st.params);
        st.prepared = true;
    }

    int slot;
    uint32_t gen;
    ot::audio::SynthVoice v{};
    bool on = false;
    note_t note = 0;
    float vel = 1.f, gain = 0.f;
    float tuneSt = 0.f;   // CHIP: the tuning offset of the note, on the bend (the engine reads only the integer note)
    uint16_t id = 0;       // the sequencer's note id of the note on (noteTag), 0: none
    uint8_t fadeLeft = 0;  // New Note Action Cut / Fade: blocks of the ramp to 0 that are left (1: the last one), 0: none
    // MPE in: the part (the note on's MIDI channel), the member channel (0: none or a tail), the channel's note counter at the note on, the
    // master and member pressure counters last taken, the pressure (127: full) and the raw member bend of the voice
    uint8_t part = 0, chan = 0, owner = 0, masterSeen = 0, pressSeen = 0, press = 127;
    int16_t nbend = 0;
    bool followsChan() const { return chan != 0 && g_mpe[part].owner[chan] == owner; }
    static bool kitEngine(const SlotEngine& s) { return s.typeId == int(InstrumentTypeId::kDrumSynth) || s.typeId == 0x85; }
};

#if defined(PLUS_SYNTH_HARNESS)
PlugVoice* PlugVoice::h_voices[32];
#endif

class SilentVoice final : public InstrumentVoice
{
  public:
    void onMidiEvent(const InstrumentVoiceContext&, const MidiEvent&) override {}
    void onParamChange(int, Patch::optional_paramvalue_t) override {}
    void renderVoiceAudio(const InstrumentVoiceContext&, std::optional<AudioBufferStereoRef>, AudioBufferStereoRef out,
                          std::span<AudioBufferRef>) override
    {
        std::fill(out[0].begin(), out[0].end(), 0.0f);
        std::fill(out[1].begin(), out[1].end(), 0.0f);
    }
    bool isActive() override { return false; }
    bool handlesNote(note_t) override { return false; }
    bool handlesAnyNote() override { return false; }
    tiny_optional<note_t> getLastNote() override { return {}; }
    tiny_optional<float> getCurrentNoteVelocity() override { return {}; }
};

// The value text of a plugin engine parameter, the MK66 shim's formatValue (open_cold.cpp) for the PLUG_FMT_* formats, character for
// character. No float printf here (the plus sim's guest links nano printf without _printf_float), so the two helpers give printf's
// results by integer means: lround of a float, and "%.Nf" of a float (double(v) * 10^N is exact for N <= 2, ties go to even as printf)
long roundf32(float x)   // std::lround(float): half away from zero
{
    const float a = x < 0.0f ? -x : x;
    long t = long(a);
    if (a - float(t) >= 0.5f) t++;
    return x < 0.0f ? -t : t;
}

int fixedText(char* buf, size_t cap, float v, int decimals, const char* unit)   // snprintf(buf, cap, "%.<decimals>f<unit>", v)
{
    const unsigned long scale = decimals == 2 ? 100u : 10u;   // 32 bit: nano printf has no %llu, the values print below 4e7
    const double x = (v < 0.0f ? -double(v) : double(v)) * double(scale);
    unsigned long i = (unsigned long)x;
    const double frac = x - double(i);
    if (frac > 0.5 || (frac == 0.5 && (i & 1u))) i++;
    return snprintf(buf, cap, "%s%lu.%0*lu%s", std::signbit(v) ? "-" : "", i / scale, decimals, i % scale, unit);
}

// MK66 waveFileShape (open_load.h): ALCO's A File / B File, an index into the card's /Waves list. The IMX host has no wave list (no
// PLUG_EXT_FILES, plug_imx.cpp), the MK66 case of a card without /Waves: the row is not listed and the value reads as with no list
bool waveFileParam(const plug_synth_param& p)
{
    const size_t n = p.name ? std::strlen(p.name) : 0;
    return p.fmt == PLUG_FMT_INT && p.min == 0.0f && p.max == 1024.0f && n > 5 && !std::strcmp(p.name + n - 5, " File");
}

std::string formatValue(const plug_synth_param& d, float v)
{
    char buf[24];
    switch (d.fmt)
    {
        case PLUG_FMT_PCT: snprintf(buf, sizeof(buf), "%ld%%", roundf32(v * 100.0f)); break;
        case PLUG_FMT_PCT_MAX:
        {
            const float m = std::max(d.min < 0.0f ? -d.min : d.min, d.max < 0.0f ? -d.max : d.max);
            snprintf(buf, sizeof(buf), "%ld%%", roundf32(m > 0 ? v / m * 100.0f : 0.0f));
            break;
        }
        case PLUG_FMT_HZ:
            if (v >= 1000.0f)
                fixedText(buf, sizeof(buf), v / 1000.0f, 2, " kHz");
            else
                snprintf(buf, sizeof(buf), "%ld Hz", roundf32(v));
            break;
        case PLUG_FMT_SEC:
            if (v < 1.0f)
                snprintf(buf, sizeof(buf), "%ld ms", roundf32(v * 1000.0f));
            else
                fixedText(buf, sizeof(buf), v, 2, " s");
            break;
        case PLUG_FMT_LFO_HZ: fixedText(buf, sizeof(buf), v, 2, " Hz"); break;
        case PLUG_FMT_ENUM:
        {
            const int idx = std::clamp(int(roundf32(v)), 0, int(d.max));
            return d.enum_names && d.enum_names[idx] ? std::string(d.enum_names[idx]) : std::string();
        }
        case PLUG_FMT_VAL100: snprintf(buf, sizeof(buf), "%ld", roundf32(v * 100.0f)); break;
        case PLUG_FMT_ON_OFF: return v >= 0.5f ? "On" : "Off";
        case PLUG_FMT_SEMI: snprintf(buf, sizeof(buf), "%ld st", roundf32(v)); break;
        case PLUG_FMT_CENTS: snprintf(buf, sizeof(buf), "%ld c", roundf32(v)); break;
        case PLUG_FMT_INT:
            if (waveFileParam(d)) return roundf32(v) <= 0 ? "Sine" : "Missing";   // MK66 waveLabel without a stored path or list
            snprintf(buf, sizeof(buf), "%ld", roundf32(v));
            break;
        case PLUG_FMT_RATIO: fixedText(buf, sizeof(buf), 0.25f * float(std::clamp(int(roundf32(v)), 0, 47) + 1), 2, ""); break;
        case PLUG_FMT_MS10: fixedText(buf, sizeof(buf), v * 0.1f, 2, " ms"); break;
        case PLUG_FMT_DB: fixedText(buf, sizeof(buf), v, 1, " dB"); break;
        case PLUG_FMT_UNIT: fixedText(buf, sizeof(buf), v, 2, ""); break;
        case PLUG_FMT_PAN:
        {
            const long q = roundf32(v * 100.0f);
            if (q == 0) return "0";
            snprintf(buf, sizeof(buf), "%c%ld", q < 0 ? 'L' : 'R', q < 0 ? -q : q);
            break;
        }
        default: snprintf(buf, sizeof(buf), "%ld", roundf32(v)); break;   // past PLUG_FMT_UNIT: the built-in PERC's only, refused at load
    }
    return buf;
}

// The lib's metadata from the engine's tables (strings copied: the slot's text goes when the slot is loaded again)
void makeMetadata(int slot, uint32_t gen, Patch::Metadata& m)
{
    using Param = Patch::Metadata::Param;
    using Layout = Patch::Metadata::EditorLayout;
    const SlotEngine& s = g_slots[slot];
    if (!s.ext || s.gen != gen) return;
    const plug_ext_synth& x = *s.ext;
    const uint32_t n = std::min<uint32_t>(x.param_count, uint32_t(ot::audio::kSynthParamMax));
    m.params.resize(n);
    for (uint32_t i = 0; i < n; i++)
    {
        const plug_synth_param& p = x.params[i];
        Param& q = m.params[i];
        q.displayName = p.name ? p.name : "";
        q.defaultValue = p.def;
        q.minValue = Patch::paramvalue_short_t(p.min);
        q.maxValue = Patch::paramvalue_short_t(p.max);
        q.editCurve = p.curve < PLUG_CURVE_COUNT ? static_cast<Param::Curve>(p.curve) : Param::Curve::kLinear;
        q.isEnum = p.fmt == PLUG_FMT_ENUM;
        // every parameter prints as on the MK66 (units, scaling, decimals): the engine's own float range and format, not the lib's
        // __fp16 copy of the range
        const plug_synth_param* d = &p;
        q.valueFormatter = [d, slot, gen](const Patch&, Patch::paramvalue_t value) -> std::string
        {
            if (!g_slots[slot].ext || g_slots[slot].gen != gen) return {};
            return formatValue(*d, value);
        };
    }
    Layout layout;
    for (uint32_t i = 0; i < x.section_count; i++)
    {
        const plug_synth_section& sec = x.sections[i];
        Layout::EditorSection e;
        e.name = sec.name ? sec.name : "";
        e.shortName = sec.short_name ? sec.short_name : e.name;
        for (uint32_t k = 0; k < sec.count; k++)
            if (sec.ids[k] < n && !waveFileParam(x.params[sec.ids[k]])) e.elements.push_back({ Layout::EditorElement::Type::kParam, int(sec.ids[k]) });
        layout.sections.push_back(std::move(e));
    }
    m.layoutFlat = layout;
    m.layout23 = std::move(layout);
    m.instrumentTypeId = x.type_id;
    m.outputChannels = Patch::Metadata::OutputChannels::kMono;   // see renderVoiceAudio
    m.maxVoices = int(x.max_voices ? x.max_voices : 1);
    const plug_synth_macro* macros = x.macros;
    const uint32_t nm = std::min<uint32_t>(x.macro_count, 6u);
    const int vol = x.volume_param >= 0 && uint32_t(x.volume_param) < n ? int(x.volume_param) : -1;
    m.initDefaultMacros = [macros, nm, n, vol, slot, gen](std::vector<Patch::Metadata::Macro>& out)
    {
        if (!g_slots[slot].ext || g_slots[slot].gen != gen) return;
        for (uint32_t i = 0; i < nm && i < out.size(); i++)
        {
            const plug_synth_macro& mc = macros[i];
            out[i].displayName = mc.name ? mc.name : "";
            out[i].bipolar = mc.bipolar != 0;
            out[i].modifiers.clear();
            for (uint32_t t = 0; t < mc.count && t < 2; t++)
                if (mc.targets[t].param < n) out[i].modifiers.push_back({ int(mc.targets[t].param), mc.targets[t].amount });
        }
        // M7 Velocity on Volume at depth 1, the MK66 default (open_cold.cpp): the user edits the depth on the M7 page, the patch keeps it
        constexpr size_t kVel = size_t(Patch::SystemMacroId::kVelocity);
        if (vol >= 0 && kVel < out.size()) out[kVel].modifiers = { { vol, 1.0f } };
    };
}
}   // namespace

namespace
{
// PLUG_EXT_CHORD: the answer of an attached engine, the checks of plug_synth.cpp checkChord (the IMX loader keeps no text or rw bounds
// of the slot, the pointer is the engine's own): both indices in range and different, Chord an enum or integer from 0 with 68 to 77
// values, Chord Mode an enum, integer or on/off from 0 with Poly and Para. Anything else: the engine plays chords as separate notes
bool chordOk(const plug_ext_chord* c, const plug_ext_synth* x)
{
    if (!c || c->struct_size < offsetof(plug_ext_chord, strum_param)) return false;
    const uint32_t np = std::min<uint32_t>(x->param_count, uint32_t(ot::audio::kSynthParamMax));
    if (c->param_index >= np || c->mode_param >= np || c->param_index == c->mode_param) return false;
    const plug_synth_param& ch = x->params[c->param_index];
    const plug_synth_param& md = x->params[c->mode_param];
    const bool chOk = (ch.fmt == PLUG_FMT_ENUM || ch.fmt == PLUG_FMT_INT) && ch.min == 0.0f &&
                      ch.max >= float(PLUG_CHORD_COUNT_V1 - 1) && ch.max <= float(PLUG_CHORD_COUNT - 1) && ch.max == float(int(ch.max));
    const bool mdOk = (md.fmt == PLUG_FMT_ENUM || md.fmt == PLUG_FMT_INT || md.fmt == PLUG_FMT_ON_OFF) && md.min == 0.0f && md.max >= 1.0f;
    return chOk && mdOk;
}

// The Strum tail of an answer chordOk passed: an integer parameter from -63 to 63, else none (plug_synth.cpp chordStrum)
int8_t chordStrum(const plug_ext_chord* c, const plug_ext_synth* x)
{
    if (c->struct_size < sizeof(plug_ext_chord) || c->strum_param >= x->param_count || c->strum_param >= uint32_t(ot::audio::kSynthParamMax) ||
        c->strum_param == c->param_index || c->strum_param == c->mode_param)
        return -1;
    const plug_synth_param& st = x->params[c->strum_param];
    return st.fmt == PLUG_FMT_INT && st.min == -63.0f && st.max == 63.0f ? int8_t(c->strum_param) : int8_t(-1);
}

void registerSlot(int slot)
{
    const SlotEngine& s = g_slots[slot];
    const uint32_t gen = s.gen;
    auto type = std::make_unique<InstrumentType>();
    type->name = s.ext->name ? s.ext->name : "";
    type->createMetadata = [slot, gen](Patch&, Patch::Metadata& m) { makeMetadata(slot, gen, m); };
    type->createVoice = [slot, gen](int) -> std::unique_ptr<InstrumentVoice>
    {
        if (g_slots[slot].ext && g_slots[slot].gen == gen) return std::make_unique<PlugVoice>(slot, gen);
        return std::make_unique<SilentVoice>();
    };
    g_registry->addInstrumentType(s.typeId, std::move(type));
}
}   // namespace

// A new Synth (one per boot on the unit, the sim's harness can make more) gets the engines that are attached already
void setRegistry(InstrumentTypeRegistry* registry)
{
    g_registry = registry;
    for (int slot = 0; slot < kSlots; slot++)
        if (g_slots[slot].ext) registerSlot(slot);
}

void detach(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    SlotEngine& s = g_slots[slot];
    s.gen++;
    s.ext = nullptr;
    s.typeId = -1;
    s.tempo = 0.f;
    s.chord = s.mode = s.strum = -1;
    s.last = 0;
    ::operator delete(s.work);
    s.work = nullptr;
    for (auto*& st : s.states)
    {
        delete st;
        st = nullptr;
    }
}

int slotType(int slot) { return slot >= 0 && slot < kSlots ? g_slots[slot].typeId : -1; }

int chordInfo(int typeId)
{
    for (const SlotEngine& s : g_slots)
        if (s.ext && s.typeId == typeId && s.chord >= 0)
            return int(uint32_t(uint8_t(s.chord)) | uint32_t(uint8_t(s.mode)) << 8 | uint32_t(uint8_t(s.strum)) << 16 | uint32_t(s.last) << 24);
    return -1;
}

void noteTag(uint16_t id, uint8_t endMode)
{
    g_tag.id = id;
    g_tag.end = endMode;
}

void mpeNote(uint8_t chan) { g_mpeChan = chan; }

void partMpe(int part, float memberRange, float pressAmount)
{
    if (part < 0 || part >= kParts) return;
    g_mpe[part].rangeSt = std::clamp(memberRange, 0.0f, 96.0f) * (1.0f / 8192.0f);
    g_mpe[part].pressAmt = std::clamp(pressAmount, 0.0f, 1.0f);
}

void partNoteBend(int part, uint8_t chan, int raw)
{
    if (part < 0 || part >= kParts || chan < 2 || chan > 16) return;
    g_mpe[part].bend[chan] = int16_t(std::clamp(raw, -8192, 8191));
}

void partNotePress(int part, uint8_t chan, uint8_t press)
{
    if (part < 0 || part >= kParts || chan < 1 || chan > 16) return;
    if (chan == 1)
    {
        g_mpe[part].master = press;
        g_mpe[part].masterSeq++;
        return;
    }
    g_mpe[part].press[chan] = press;
    g_mpe[part].pressSeq[chan]++;
}

#if defined(PLUS_SYNTH_HARNESS)
// harness only: voice i of the probe (h2_mpe_voice), field f: 0 on, 1 part, 2 note, 3 member channel, 4 gate, 5 raw member bend, 6 pressure
int mpeProbe(int i, int f)
{
    PlugVoice* p = i >= 0 && i < 32 ? PlugVoice::h_voices[i] : nullptr;
    if (!p) return f == 0 ? 0 : -1;
    switch (f)
    {
        case 0: return p->hOn();
        case 1: return p->hPart();
        case 2: return p->hNote();
        case 3: return p->hChan();
        case 4: return p->hGate();
        case 5: return p->hBend();
        default: return p->hPress();
    }
}
#endif

void noteChord(int part, uint8_t note, uint8_t chord, int8_t strum)
{
    if (part < 0 || part >= kParts) return;
    g_pending[part].chord = int16_t(chord | ((std::clamp<int>(strum, -63, 63) + 64) << 8));
    g_pending[part].note = note;
}

int attach(int slot, uint32_t entryAddr, const plug_host* host)
{
    if (!g_registry) return -1;
    if (slot < 0 || slot >= kSlots) return -2;
    detach(slot);
    const auto entry = reinterpret_cast<plug_entry_fn>(uintptr_t(entryAddr));
    const plug_desc* d = entry ? entry(host, (uint32_t(PLUG_ABI_MAJOR) << 16) | PLUG_ABI_MINOR) : nullptr;
    if (!d || d->kind != PLUG_KIND_SYNTH || !d->query) return -3;
    alignas(8) static uint8_t inst[16];   // a synth engine keeps its state in its own bss, the instance is unused (plug_desc 8 B)
    if (d->instance_size > sizeof inst) return -4;
    std::memset(inst, 0, sizeof inst);
    if (d->init && d->init(inst, host) != 0) return -6;
    const auto* x = static_cast<const plug_ext_synth*>(d->query(PLUG_EXT_SYNTH));
    if (!x || x->struct_size < offsetof(plug_ext_synth, service) || !x->note_on || !x->note_off || !x->render || !x->params) return -4;
    if (x->layout != kLayout) return -5;
    SlotEngine& s = g_slots[slot];
    if (x->work_bytes)
    {
        s.work = ::operator new(x->work_bytes, std::nothrow);
        if (!s.work) return -7;
        std::memset(s.work, 0, x->work_bytes);
        if (x->set_work) x->set_work(s.work, x->work_bytes);
    }
    s.ext = x;
    s.typeId = x->type_id;
    const auto* c = static_cast<const plug_ext_chord*>(d->query(PLUG_EXT_CHORD));
    if (chordOk(c, x))
    {
        s.chord = int8_t(c->param_index);
        s.mode = int8_t(c->mode_param);
        s.strum = chordStrum(c, x);
        s.last = uint8_t(x->params[c->param_index].max);
    }
    registerSlot(slot);
    return x->type_id;
}

#if !defined(PLUS_SYNTH_HARNESS)
int attachFile(int slot, const uint8_t* file, uint32_t len)
{
    if (slot < 0 || slot >= kSlots) return -2;
    detach(slot);   // the voices of the slot's engine stop before its code is overwritten
    uint32_t entryAddr = 0;
    const plug::Err e = plug::imx::load(file, len, PLUG_KIND_SYNTH, slot, entryAddr);
    if (e != plug::kOk) return -100 - int(e);
    return attach(slot, entryAddr, plug::imx::host());
}
#endif

}   // namespace plugimx
}   // namespace polysynth

#endif
