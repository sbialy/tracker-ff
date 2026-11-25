#ifndef AUDIOENGINE_MIXERANDFXS_H_
#define AUDIOENGINE_MIXERANDFXS_H_
#include "Audio.h"
#include "stereo_sends.h"

enum struct MixerAndFxChannelType
{
    kLeft,
    kRight,
    kChannelCount
};

template<int voicesCount>
class MixerAndFxs
{
  public:
    enum FxType
    {
        kDelay,
        kReverb,
        kFxCount
    };

    enum TrackAndFxsChannels
    {
        kDryMixChannel,
        kDelayChannel,
        kReverbChannel,
        kInputChannel
    };

    static constexpr int kExternalInputsCount = 1;
#ifndef PLATFORM_IMX
#ifdef FEATURE_SYNTH_OPEN
    // MK66 with the open synth: tracks, external inputs, then one send input per synth part
    static constexpr int kFirstSynthSendInput = 2 * kExternalInputsCount + voicesCount;
    static constexpr int kSendsInputsCount = kFirstSynthSendInput + kSynthParts;
#else
    static constexpr int kFirstSynthSendInput = 2 * kExternalInputsCount + voicesCount;
    static constexpr int kSendsInputsCount = 2 * kExternalInputsCount + voicesCount;
#endif
#else
    // Tracker+ layout: left and right send input per track, external inputs, then the synth parts
    static constexpr int kFirstSynthSendInput = 2 * kExternalInputsCount + 2 * voicesCount;
    static constexpr int kSendsInputsCount = kFirstSynthSendInput + kSynthParts;
#endif
    static constexpr int kTrackMixerOutputsCount = 1;
    static constexpr int kTrackAndSendsMixerInputs =
      kTrackMixerOutputsCount + kExternalInputsCount + FxType::kFxCount;
    static constexpr int kTracksAndSynthVoices = voicesCount + kSynthParts;

    MixerAndFxs();
    AudioMixer<kSendsInputsCount>* getPreFxMixer(FxType type);
#ifdef FW_STEREO_SENDS
    // the side (L - R) / 2 send mixer of an FX (stereo_sends.h)
    AudioMixer<kSendsInputsCount>* getSideMixer(FxType type) { return &sideMixer[type]; }
#endif
    AudioMixer<kTracksAndSynthVoices>* getTracksMixer(MixerAndFxChannelType channel);
    AudioMixer<kTrackAndSendsMixerInputs>* getTracksAndSendsMixer(MixerAndFxChannelType channel);
    AudioEffectShortDelay* getDelay();
    AudioEffectPolyverb* getPolyverb();
    AudioAnalyzeRMS* getDelayRMS(MixerAndFxChannelType channel);
    AudioAnalyzeRMS* getPolyverbRMS(MixerAndFxChannelType channel);
    AudioAnalyzeRMS* getDryMixRMS(MixerAndFxChannelType channel);
#ifdef AUDIO_NULL_IS_SILENCE
    // The Mixer's Mute of the FX (fx_mute_gate.h), audio interrupt before update_all: the FX follow the effective mute (the project's
    // flag, or the USB 8+3 auto mute override). A muted FX rings out its tail (every send into it is 0 already), then it and its send mixer
    // leave update_all. autoMuted: no ring, the fade starts at once.
    void syncFxMute(bool delayMuted, bool reverbMuted, bool autoMuted = false)
    {
        if (shortDelay.isMuted() != delayMuted)
            shortDelay.setMuted(delayMuted);
        if (polyverb.isMuted() != reverbMuted)
            polyverb.setMuted(reverbMuted);
        if (autoMuted)
        {
            shortDelay.cutMuteRing();
            polyverb.cutMuteRing();
        }
    }
#endif
#ifdef FEATURE_SYNTH
    AudioAnalyzeRMS* getSynthRMS(int synthIdx, MixerAndFxChannelType channel);
#endif

  private:
    AudioMixer<kSendsInputsCount> preFxMixer[FxType::kFxCount];
#ifdef FW_STEREO_SENDS
    AudioMixer<kSendsInputsCount> sideMixer[FxType::kFxCount];
#endif
    AudioMixer<kTracksAndSynthVoices>
      tracksAndSynthMixer[(int)MixerAndFxChannelType::kChannelCount];
    AudioMixer<kTrackAndSendsMixerInputs>
      tracksAndSendsMixer[(int)MixerAndFxChannelType::kChannelCount];

    AudioAnalyzeRMS delayRMS[(int)MixerAndFxChannelType::kChannelCount];
    AudioAnalyzeRMS polyverbRMS[(int)MixerAndFxChannelType::kChannelCount];
    AudioAnalyzeRMS dryMixRMS[(int)MixerAndFxChannelType::kChannelCount];
#ifdef FEATURE_SYNTH
    AudioAnalyzeRMS synthRMS[kSynthParts][(int)MixerAndFxChannelType::kChannelCount];
#endif

    AudioEffectShortDelay shortDelay;
    AudioEffectPolyverb polyverb;

    std::vector<AudioConnection*> connections;
};

#endif /* AUDIOENGINE_MIXERANDFXS_H_ */
