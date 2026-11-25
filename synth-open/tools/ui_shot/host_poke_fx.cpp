// host_poke.cpp helper: the delay and reverb the audio engine holds after audioEngine::init and after a project load. Host only, NOT firmware.
// The private members of the engine, the delay and the reverb are read through #define private public.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#define private public
#include "mtAudioEngine.h"
#undef private
#include "mtStructs.h"

// "masterfx FEEDBACK DAMP": put the project values FEEDBACK (percent, 0..100) and DAMP (0..1) in mtProject, let the project load path set the
// delay and the reverb (fProjEdit.cpp: engine.setDelayFeedback, engine.setReverbDamp), read the feedback byte of the delay and the low pass
// coefficient of the reverb, scramble both, run the init path (engine.beginDelay, engine.beginReverb, what audioEngine::init calls) and
// read them again. The line ends with "same" when the two states are equal, "DIFFER" when not. The load state is put back at the end.
extern "C" void hostPokeMasterFx(unsigned feedback, float damp, std::string* out)
{
    audioEngine& e = engine;
    mtProject.values.delayFeedback = (uint8_t)feedback;
    mtProject.values.reverb.damp = damp;

    e.setDelayFeedback(mtProject.values.delayFeedback);
    e.setReverbDamp(mtProject.values.reverb.damp);
    const unsigned loadFb = e.delay->feedback;
    const float loadLp = e.polyverb->reverb.lp_;

    e.delay->setFeedback(0.0f);
    e.polyverb->setDamp(0.123f);
    e.beginDelay();
    e.beginReverb();
    const unsigned initFb = e.delay->feedback;
    const float initLp = e.polyverb->reverb.lp_;

    e.setDelayFeedback(mtProject.values.delayFeedback);
    e.setReverbDamp(mtProject.values.reverb.damp);

    char b[256];
    snprintf(b, sizeof b, "masterfx feedback=%u damp=%.3f load: delay byte %u reverb lp %.6f, init: delay byte %u reverb lp %.6f, %s", feedback,
             (double)damp, loadFb, (double)loadLp, initFb, (double)initLp,
             (loadFb == initFb && loadLp == initLp) ? "same" : "DIFFER");
    *out = b;
}

// "audiostat tap TRACK": an extra stream on the output of the sample voice (play memory channel 0) of a track, so the Tape FX proof
// reads the level and the pitch of the voice itself, before the track's amplifier, mixers and the master chain. Host only.
#include "AudioStream.h"
#include "play_memory.h"
#include <cmath>
namespace
{
struct TapBlock { float rms; int zc; };
std::vector<TapBlock> g_tap;
class SampleTap;
SampleTap* g_tapStream;
class SampleTap final : public AudioStream
{
  public:
    SampleTap() : AudioStream(1, queue) {}
    void update() override
    {
        audio_block_t* b = receiveReadOnly(0);
        if (!b) { g_tap.push_back({0.0f, 0}); return; }
        double sum = 0; int zc = 0, prev = 0;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            const int v = b->data[i];
            sum += double(v) * v;
            if (i > 0 && ((prev < 0 && v >= 0) || (prev >= 0 && v < 0))) zc++;
            prev = v;
        }
        float rms = float(std::sqrt(sum / AUDIO_BLOCK_SAMPLES));
        if (ampTrack >= 0)   // "audiostat tapamp N": the level after the track's amplifier (its gain), the pitch of the voice
            rms *= float(instrumentPlayer[ampTrack].ampPtr[0]->gainMultiplier()) / AudioAmplifier::kUnityGain;
        g_tap.push_back({rms, zc});
        release(b);
    }
    int ampTrack = -1;
  private:
    audio_block_t* queue[1];
};
}
// 1 when the sample voice (play memory channel 0) of a track is playing: the IMX round 4 flows count its starts (host_poke.cpp UISHOT_TRACE_FFSTATE)
extern "C" int hostTrackPlaying(int track)
{
    return track >= 0 && track < 16 && !instrumentPlayer[track].playMemPtr.empty() && instrumentPlayer[track].playMemPtr[0]->isPlaying() ? 1 : 0;
}
extern "C" void hostTapAttach(int track)
{
    static SampleTap* tap = new SampleTap();
    static AudioConnection* c = nullptr;
    delete c;
    c = new AudioConnection(instrumentPlayer[track].playMemPtr[0], 0, tap, 0);
    tap->ampTrack = -1;
    g_tapStream = tap;
}
// "audiostat tapamp N": the same tap after the voice's amplifier (the level the step volume and the volume FX set), tables_sim.sh
extern "C" void hostTapAttachAmp(int track)
{   // the sample chain after the voice is silent on the host, so the tap stays on the voice and takes the amplifier's gain per block
    hostTapAttach(track);
    g_tapStream->ampTrack = track;
}
extern "C" void hostTapClear() { g_tap.clear(); }
extern "C" size_t hostTapCount() { return g_tap.size(); }
extern "C" void hostTapGet(size_t i, float* rms, int* zc) { *rms = g_tap[i].rms; *zc = g_tap[i].zc; }

// "fxmute": the Mixer's Mute of delay and reverb (fx_mute_gate.h): the project flags, then per FX whether its gate is muted and asleep and
// whether the FX and its send mixer are in update_all, and how many updates the FX ran since the last fxmute (hostFxUpdates)
extern "C" void hostPokeFxMute(std::string* out)
{
#ifndef FEATURE_SYNTH_OPEN
    *out = "n/a (stock synth stack)";
#else
    audioEngine& e = engine;
    auto* m = e.getMixerAndFx();
    char b[320];
    snprintf(b, sizeof b, "fxmute flags delay %d reverb %d | delay muted %d asleep %d active %d sendmix %d | reverb muted %d asleep %d active %d sendmix %d",
             mtProject.values.delayMute, mtProject.values.reverbMute, (int)e.delay->isMuted(), (int)e.delay->isMuteAsleep(),
             (int)e.delay->isActive(), (int)m->getPreFxMixer(MixerAndFxs<audioEngine::kVoices>::FxType::kDelay)->isActive(),
             (int)e.polyverb->isMuted(), (int)e.polyverb->isMuteAsleep(), (int)e.polyverb->isActive(),
             (int)m->getPreFxMixer(MixerAndFxs<audioEngine::kVoices>::FxType::kReverb)->isActive());
    *out = b;
#endif
}

// "fxauto": the USB 8+3 auto mute (fx_auto_mute.h): the setting, the override, what the platform reports, the project flags, the gates and the
// send gains the Mixer holds (the delay and reverb send of track 1, so a restored state shows)
#include "fx_auto_mute.h"
extern "C" void hostPokeFxAuto(std::string* out)
{
#ifndef FEATURE_SYNTH_OPEN
    *out = "n/a (stock synth stack)";
#else
    audioEngine& e = engine;
    auto* m = e.getMixerAndFx();
    using Fx = MixerAndFxs<audioEngine::kVoices>::FxType;
    char b[420];
    snprintf(b, sizeof b,
             "fxauto enabled %d active %d streaming %d | flags delay %d reverb %d | delay muted %d asleep %d active %d | reverb muted %d asleep %d active %d | "
             "sendmix %d %d",
             (int)fxAutoMute.enabled(), (int)fxAutoMute.active(), (int)e.usbStreamPlatform->usbAudioStreaming(), mtProject.values.delayMute,
             mtProject.values.reverbMute, (int)e.delay->isMuted(), (int)e.delay->isMuteAsleep(), (int)e.delay->isActive(), (int)e.polyverb->isMuted(),
             (int)e.polyverb->isMuteAsleep(), (int)e.polyverb->isActive(), (int)m->getPreFxMixer(Fx::kDelay)->isActive(),
             (int)m->getPreFxMixer(Fx::kReverb)->isActive());
    *out = b;
#endif
}
