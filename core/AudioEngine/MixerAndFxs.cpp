#include "MixerAndFxs.h"

template<int voicesCount>
MixerAndFxs<voicesCount>::MixerAndFxs()
{

    connections.push_back(new AudioConnection(
      &preFxMixer[FxType::kReverb], 0, &polyverb, (int)MixerAndFxChannelType::kLeft));
#ifdef FW_STEREO_SENDS
    // port 1 of the reverb is the side input, not a second copy of the mid
    connections.push_back(new AudioConnection(&sideMixer[FxType::kReverb], 0, &polyverb, 1));
    connections.push_back(new AudioConnection(&sideMixer[FxType::kDelay], 0, &shortDelay, 1));
#else
    connections.push_back(new AudioConnection(
      &preFxMixer[FxType::kReverb], 0, &polyverb, (int)MixerAndFxChannelType::kRight));
#endif

    connections.push_back(new AudioConnection(&preFxMixer[FxType::kDelay], &shortDelay));
#ifdef AUDIO_NULL_IS_SILENCE
    polyverb.setMuteFeed(&preFxMixer[FxType::kReverb]);
    shortDelay.setMuteFeed(&preFxMixer[FxType::kDelay]);
#ifdef FW_STEREO_SENDS
    polyverb.setMuteFeed2(&sideMixer[FxType::kReverb]);
    shortDelay.setMuteFeed2(&sideMixer[FxType::kDelay]);
#endif
#endif

    for (int channel = 0; channel < (int)MixerAndFxChannelType::kChannelCount; channel++)
    {
        connections.push_back(new AudioConnection(&tracksAndSynthMixer[channel],
                                                  0,
                                                  &tracksAndSendsMixer[channel],
                                                  TrackAndFxsChannels::kDryMixChannel));

        connections.push_back(new AudioConnection(
          &shortDelay, channel, &tracksAndSendsMixer[channel], TrackAndFxsChannels::kDelayChannel));

        connections.push_back(new AudioConnection(
          &polyverb, channel, &tracksAndSendsMixer[channel], TrackAndFxsChannels::kReverbChannel));

        connections.push_back(new AudioConnection(&polyverb, channel, &polyverbRMS[channel], 0));
        connections.push_back(new AudioConnection(&shortDelay, channel, &delayRMS[channel], 0));
        connections.push_back(
          new AudioConnection(&tracksAndSynthMixer[channel], 0, &dryMixRMS[channel], 0));
    }
}

template<int voicesCount>
AudioMixer<MixerAndFxs<voicesCount>::kSendsInputsCount>* MixerAndFxs<voicesCount>::getPreFxMixer(
  FxType type)
{
    return &preFxMixer[type];
}

template<int voicesCount>
AudioMixer<MixerAndFxs<voicesCount>::kTracksAndSynthVoices>*
MixerAndFxs<voicesCount>::getTracksMixer(MixerAndFxChannelType channel)
{
    return &tracksAndSynthMixer[(int)channel];
}

template<int voicesCount>
AudioMixer<MixerAndFxs<voicesCount>::kTrackAndSendsMixerInputs>*
MixerAndFxs<voicesCount>::getTracksAndSendsMixer(MixerAndFxChannelType channel)
{
    return &tracksAndSendsMixer[(int)channel];
}

template<int voicesCount>
AudioEffectShortDelay* MixerAndFxs<voicesCount>::getDelay()
{
    return &shortDelay;
}

template<int voicesCount>
AudioEffectPolyverb* MixerAndFxs<voicesCount>::getPolyverb()
{
    return &polyverb;
}

template<int voicesCount>
AudioAnalyzeRMS* MixerAndFxs<voicesCount>::getDelayRMS(MixerAndFxChannelType channel)
{
    return &delayRMS[(int)channel];
}

template<int voicesCount>
AudioAnalyzeRMS* MixerAndFxs<voicesCount>::getPolyverbRMS(MixerAndFxChannelType channel)
{
    return &polyverbRMS[(int)channel];
}

template<int voicesCount>
AudioAnalyzeRMS* MixerAndFxs<voicesCount>::getDryMixRMS(MixerAndFxChannelType channel)
{
    return &dryMixRMS[(int)channel];
}

#ifdef FEATURE_SYNTH
template<int voicesCount>
AudioAnalyzeRMS* MixerAndFxs<voicesCount>::getSynthRMS(int synthIdx, MixerAndFxChannelType channel)
{
    return &synthRMS[synthIdx][(int)channel];
}
#endif

template class MixerAndFxs<8>;
