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

// Values with macros M1..M6 applied (additive, scaled by the param range, clamped).
void open::OpenPatch::effective(float* out) const
{
    std::memcpy(out, values, sizeof(float) * count);
    float add[kMaxParams] = {};
    bool hit[kMaxParams] = {};
    for (int m = 0; m < int(kUserMacroCount); m++)
    {
        const auto& macro = metadata.macros[m];
        const float cc = float(macroCC[m]);
        const float mv = macro.bipolar ? (cc - 63.0f) * (1.0f / 64.0f) : cc * (1.0f / 127.0f);
        for (const auto& mod : macro.modifiers)
        {
            if (!mod.paramId.has_value() || *mod.paramId < 0 || *mod.paramId >= count)
            {
                continue;
            }
            const auto& p = metadata.params[*mod.paramId];
            add[*mod.paramId] += mod.amount * mv * (float(p.maxValue) - float(p.minValue));
            hit[*mod.paramId] = true;
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
};

// Steal order after "free": the quietest released voice, else the oldest.
bool betterVictim(const Voice& c, const Voice& best)
{
    const bool cRel = !c.v.gate, bRel = !best.v.gate;
    return cRel != bRel ? cRel : (cRel ? c.v.level < best.v.level : c.age > best.age);
}

// The shared voice pool as an instrument sees it (implemented by OpenSynth).
class VoiceHost
{
  public:
    virtual int cap() const = 0;      // global cap: budget lowered by the governor
    virtual int sounding() const = 0; // voices on and not fading, all slots
    virtual Voice* victim() = 0;      // best steal candidate over all slots, or nullptr
    virtual void onStop() = 0;        // transport Stop: governor back to the budget
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
        def = patch ? patch->getDef() : nullptr;
        engine = engineOf(def); // nullptr for a missing engine's placeholder: the part is silent
        cutAll();
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
        refreshParams();
        if (tempo && *tempo)
        {
            const auto bpm = (*tempo)();
            if (bpm.has_value())
            {
                engine->setTempo(*bpm);
            }
        }
        engine->prepare(scratch, params);
        // PERC has no Volume: the voice gain is the note velocity (the engine divides it back out per hit)
        const bool hasVol = def->volumeParam >= 0;
        const float volMin = hasVol ? float(patch->getMetadata().params[def->volumeParam].minValue) : 0.0f;
        const float volMax = hasVol ? float(patch->getMetadata().params[def->volumeParam].maxValue) : 1.0f;
        const float velK = hasVol ? patch->velocityAmount() : 1.0f;
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
            if (!engine->render(v.v, params, g0, gInc, L, R) || fade)
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

  private:
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
        patch->effective(eff);
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
                victim->v.fading = true;
            }
            else if (victim)
            {
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

    void noteOn(uint8_t note, uint8_t velocity)
    {
        if (!engine || !patch || voiceCount == 0)
        {
            return;
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
        engine->noteOn(v->v, params, n);
    }

    void noteOff(uint8_t note)
    {
        if (!engine)
        {
            return;
        }
        for (auto& v : voices)
        {
            if (v.on && v.v.gate && v.v.note == note)
            {
                v.v.gate = false;
                engine->noteOff(v.v, params);
            }
        }
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
    float bend = 0.0f; // pitch bend in semitones, applied to every voice (SynthVoice::bend)
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
        governor.evaluate(meter);
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
        meter.reset();
    }
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
        governor.setEnabled(!offline);
        governor.reset();
        meter.reset();
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
bool open::partIdle(int part) { return gActive && gActive->partIdle(part); }
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