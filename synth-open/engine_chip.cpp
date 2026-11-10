// CHIP for the open PolySynth shim on MK66: tracker-ce's own open Chiptune voice
// (core/synth/engines/ChiptuneSynthVoice.cpp, Polyend source in this repository, see NOTICE.md) behind the shim's
// ISynthEngine interface. The parameter table follows that file's createMetadata (same order, so the .patch files of
// Tracker+/Mini CE load, field 1 = 8 = InstrumentTypeId::kChiptuneSynth), the labels and sections as well. Only the
// edit curves and value formats are mapped to the shim's own (ParamDef) instead of the SDK's Patch::Metadata, and the
// default macros are the same six.
//
// Hosting: the voice is an SDK InstrumentVoice. One voice object per shim voice record (keyed by the address of the
// SynthVoice, at most 3 slots x kMaxVoicesPerInstrument voices), created once in slotReset (GUI context, never in the audio path). The
// shim's framework owns note allocation, velocity and Volume as for the other engines, so each object plays one note.
// Parameters are pushed into the voice every block (ChiptuneSynthVoice.h hooks, Volume excluded).
#include "open_engine.h"

#include "PolySynth/MidiEvent.h"
#include "PolySynth/config.h"
#include "PolySynth/sdk/InstrumentVoice.h"
#include "engines/ChiptuneSynthVoice.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace polysynth::open
{
namespace
{
using ot::audio::SynthNote;
using ot::audio::SynthParams;
using ot::audio::SynthVoice;

enum ChipParam : int
{
    kModel = 0, kTune, kFine, kShape, kPwmRate, kPwmDepth, kSubMix, kArpChord, kArpSpeed, kPitchSweep, kSweepTime,
    kVibRate, kVibDepth, kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease, kFilterCutoff, kFilterResonance,
    kFilterEnvAmount, kBitcrush, kDecimate, kPlayMode, kGlideTime, kVolume,
    kParamCount
};

const char* const kModelNames[] = { "2A03 Pulse",   "SID Saw/Tri", "Noise / LFSR", "FM Bell",
                                    "Super Square", "Zap / Laser", "Arp Chord",    "Chip Drum" };
const char* const kArpNames[] = { "Off", "Octave", "Fifth", "Major", "Minor", "Sus4", "Maj7", "Min7" };
const char* const kModeNames[] = { "Always", "Overlap", "Legato" };

using F = Fmt;
using C = CurveKind;

const ParamDef kParams[kParamCount] = {
    { "Model", 0, 0, 7, F::kEnum, C::kLinear, kModelNames },
    { "Tune", 0, -24, 24, F::kSemi, C::kLinear, nullptr },
    { "Fine", 0, -100, 100, F::kCents, C::kLinear, nullptr },
    { "Duty / Shape", 0.35f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "PWM Rate", 2.5f, 0.1f, 40, F::kLfoHz, C::kExp, nullptr },
    { "PWM Depth", 0.2f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Sub Osc", 0.25f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Chord Arp", 0, 0, 7, F::kEnum, C::kLinear, kArpNames },
    { "Arp Speed", 50, 5, 75, F::kHz, C::kLinear, nullptr },
    { "Pitch Sweep", 0, -36, 36, F::kSemi, C::kLinear, nullptr },
    { "Sweep Time", 0.25f, 0.02f, 2, F::kSec, C::kExp, nullptr },
    { "Vibrato Rate", 6, 0.5f, 25, F::kLfoHz, C::kExp, nullptr },
    { "Vibrato Depth", 0, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Attack", 0, 0, 5, F::kSec, C::kExp, nullptr },
    { "Decay", 0.35f, 0.01f, 5, F::kSec, C::kExp, nullptr },
    { "Sustain", 0.4f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Release", 0.15f, 0.01f, 5, F::kSec, C::kExp, nullptr },
    { "Cutoff", 12000, 80, 18000, F::kHz, C::kSteep, nullptr },
    { "Resonance", 0.15f, 0, 1, F::kPct, C::kLinear, nullptr },
    { "Env Amt", 0.2f, -1, 1, F::kPct, C::kLinear, nullptr },
    { "Bitcrush", 8, 2, 16, F::kInt, C::kLinear, nullptr },
    { "Decimate", 1, 1, 32, F::kInt, C::kLinear, nullptr },
    { "Glide Mode", 0, 0, 2, F::kEnum, C::kLinear, kModeNames },
    { "Glide Time", 0, 0, 2, F::kSec, C::kExp, nullptr },
    { "Volume", 1, 0, 2, F::kPct, C::kLinear, nullptr },
};

const uint8_t kOsc[] = { kModel, kTune, kFine, kShape, kPwmRate, kPwmDepth, kSubMix };
const uint8_t kMod[] = { kArpChord, kArpSpeed, kPitchSweep, kSweepTime, kVibRate, kVibDepth };
const uint8_t kAmp[] = { kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease };
const uint8_t kFlt[] = { kFilterCutoff, kFilterResonance, kFilterEnvAmount, kBitcrush, kDecimate };
const uint8_t kVoice[] = { kPlayMode, kGlideTime, kVolume };

const SectionDef kSections[] = {
    { "Oscillator", "Osc", kOsc, sizeof(kOsc) },
    { "Modulation", "Mod", kMod, sizeof(kMod) },
    { "Amplifier", "Amp", kAmp, sizeof(kAmp) },
    { "Filter", "Filter", kFlt, sizeof(kFlt) },
    { "Voice", "Voice", kVoice, sizeof(kVoice) },
};

// The six default macros of ChiptuneSynthVoice::createMetadata
const MacroDef kMacros[] = {
    { "Timbre / PWM", true, 2, { { kShape, 0.7f }, { kPwmDepth, 0.5f } } },
    { "Arp / Chord", false, 1, { { kArpChord, 1.0f } } },
    { "Sweep / Vibrato", true, 2, { { kVibDepth, 0.6f }, { kPitchSweep, 0.5f } } },
    { "Env Decay / Rel", true, 2, { { kAmpDecay, 0.8f }, { kAmpRelease, 0.6f } } },
    { "Crush / Lo-Fi", false, 2, { { kBitcrush, -0.6f }, { kDecimate, 0.4f } } },
    { "Filter Cutoff", true, 2, { { kFilterCutoff, 1.0f }, { kFilterResonance, 0.35f } } },
};

constexpr int kMaxVoiceObjects = ot::audio::kSynthSlots * kMaxVoicesPerInstrument; // 3 slots x the shim's voices per instrument
constexpr float kBlockBpm = 120.0f;

class ChipEngine final : public ot::audio::ISynthEngine
{
  public:
    int32_t id() const override { return static_cast<int32_t>(InstrumentTypeId::kChiptuneSynth); }
    const char* name() const override { return "CHIP"; }

    void slotReset(ot::audio::SynthSlotScratch&) override { createPool(); }
    void setTempo(float bpm) override { ctx.tempo = bpm; }

    void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override
    {
        createPool();
        Entry* e = entryFor(&v, true);
        if (!e)
        {
            return;
        }
        if (!n.reused)
        {
            chipVoiceReset(*e->voice);
        }
        pushParams(*e->voice, p);
        chipVoiceSetBend(*e->voice, v.bend);
        e->voice->onMidiEvent(ctx, noteEvent(MidiEvent::Type::kNoteOn, n.note));
    }

    void noteOff(SynthVoice& v, const SynthParams&) override
    {
        if (Entry* e = entryFor(&v, false))
        {
            // The shim owns polyphony: forget held notes so the voice releases instead of retriggering one
            e->voice->clearPreviousNotes();
            e->voice->onMidiEvent(ctx, noteEvent(MidiEvent::Type::kNoteOff, v.note));
        }
    }

    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override
    {
        Entry* e = entryFor(&v, false);
        if (!e)
        {
            return false;
        }
        pushParams(*e->voice, p);
        chipVoiceSetBend(*e->voice, v.bend);
        AudioBufferStereoRef out{ AudioBufferRef(bufL), AudioBufferRef(bufR) };
        e->voice->renderVoiceAudio(ctx, std::nullopt, out, {});
        float peak = 0.0f;
        float g = g0;
        for (int i = 0; i < kAudioBufferSize; i++)
        {
            const float s = bufL[i];
            peak = std::max(peak, std::fabs(s));
            L[i] += s * g;
            R[i] += s * g;
            g += gInc;
        }
        v.level = std::min(peak, 1.0f);
        return e->voice->isActive();
    }

#ifdef OPEN_HOST_TEST
    void forgetKeys()
    {
        for (auto& e : pool)
        {
            e.key = nullptr;
        }
    }
#endif

  private:
    struct Entry
    {
        const SynthVoice* key = nullptr;
        uint32_t stamp = 0;
        std::unique_ptr<InstrumentVoice> voice;
    };

    void createPool()
    {
        if (!pool[0].voice)
        {
            for (auto& e : pool)
            {
                e.voice = chipVoiceCreate();
            }
        }
    }
    // The entry of a shim voice record. At most 3 slots x kMaxVoicesPerInstrument voices are alive on the firmware, so a record always finds
    // its entry or a free one. After instruments were rebuilt (host tests) the oldest entry is reused.
    Entry* entryFor(const SynthVoice* key, bool allocate)
    {
        Entry* freeEntry = nullptr;
        Entry* idle = nullptr;
        Entry* oldest = nullptr;
        for (auto& e : pool)
        {
            if (e.key == key)
            {
                return &e;
            }
            if (!e.key)
            {
                freeEntry = freeEntry ? freeEntry : &e;
            }
            else if (!idle && e.voice && !e.voice->isActive())
            {
                idle = &e;
            }
            if (!oldest || e.stamp < oldest->stamp)
            {
                oldest = &e;
            }
        }
        if (!allocate)
        {
            return nullptr;
        }
        Entry* pick = freeEntry ? freeEntry : (idle ? idle : oldest);
        if (!pick || !pick->voice)
        {
            return nullptr;
        }
        pick->key = key;
        pick->stamp = ++stampCounter;
        return pick;
    }
    static void pushParams(InstrumentVoice& voice, const SynthParams& p)
    {
        for (int i = 0; i < kParamCount - 1 && i < p.count; i++) // Volume is the last one
        {
            chipVoiceSetParam(voice, i, p.end[i]);
        }
    }
    static MidiEvent noteEvent(MidiEvent::Type type, uint8_t note)
    {
        MidiEvent e{};
        e.type = type;
        e.channel = 0;
        e.data.note.note = note;
        e.data.note.velocity = MidiEvent::Data::Note::kMaxVelocity;
        return e;
    }

    Entry pool[kMaxVoiceObjects];
    uint32_t stampCounter = 0;
    InstrumentVoice::InstrumentVoiceContext ctx{ kBlockBpm };
    float bufL[kAudioBufferSize] = {};
    float bufR[kAudioBufferSize] = {};
};

ChipEngine gChip;

ot::audio::ISynthEngine* chipEngine() { return &gChip; }

} // namespace

#ifdef OPEN_HOST_TEST
// The entries are keyed by the address of a shim voice record. A host test that builds a synth after another one can get the address of an
// older record back from the heap, and the oldest entry (the older record's) is then evicted under a live voice. The firmware has at most
// kMaxVoiceObjects live records and never sees this.
void chipTestForgetVoices()
{
    gChip.forgetKeys();
}
#endif

const EngineDef kChipDef = {
    "CHIP",
    static_cast<int>(InstrumentTypeId::kChiptuneSynth),
    kParams,
    kParamCount,
    kSections,
    static_cast<int>(sizeof(kSections) / sizeof(kSections[0])),
    kMacros,
    static_cast<int>(sizeof(kMacros) / sizeof(kMacros[0])),
    kVolume,
    kMaxVoicesPerInstrument,
    &chipEngine,
    false,
};

} // namespace polysynth::open
