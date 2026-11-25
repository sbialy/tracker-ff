// mtAudioEngine.cpp, included where this block was: send ducking of the delay and reverb sends (FEATURE_SEND_DUCK)
#ifdef FEATURE_SEND_DUCK
// The detector's inputs are the limiter sidechain mux's (index = list position - 1): the amplifier of each track (the same tap the
// mux has), line in L and R, the post fader left channel of each synth part (the one the meters show). The returns it pulls down
// are inputs 1 (delay) and 2 (reverb) of the tracks and sends mixers.
void audioEngine::connectSendDuck()
{
    static_assert(MixerAndFxs<kVoices>::TrackAndFxsChannels::kDelayChannel == 1 &&
                    MixerAndFxs<kVoices>::TrackAndFxsChannels::kReverbChannel == 2,
                  "the return inputs of the tracks and sends mixer");
    static_assert(MUX_SIZE == 10 + kSynthParts, "sendDuck takes the inputs of the limiter sidechain mux");
    for (int voiceIdx = 0; voiceIdx < kVoices; voiceIdx++)
    {
#ifdef PLATFORM_IMX
        // two chains per voice: the detector listens to the left one, the tap the limiter sidechain mux of the left output has
        connections.push_back(new AudioConnection(
          instrumentPlayer[voiceIdx].getAudioVoice()->getAmplifier(AudioVoice::VoiceChannels::kLeft), 0, &sendDuck, voiceIdx));
#else
        connections.push_back(
          new AudioConnection(instrumentPlayer[voiceIdx].getAudioVoice()->getAmplifier(), 0, &sendDuck, voiceIdx));
#endif
    }
    constexpr int kLeftInputChannel = 8;
    constexpr int kRightInputChannel = 9;
    connections.push_back(new AudioConnection(
      input.getStereoToMono(), AudioInput::InputChannels::kLeft, &sendDuck, kLeftInputChannel));
    connections.push_back(new AudioConnection(
      input.getStereoToMono(), AudioInput::InputChannels::kRight, &sendDuck, kRightInputChannel));
    for (int part = 0; part < kSynthParts; part++)
    {
        connections.push_back(new AudioConnection(synthVoice.getPolySynth(),
                                                  SynthVoice::kPostFaderLeftChannelOffset + part,
                                                  &sendDuck,
                                                  10 + part));
    }
    sendDuck.setTargets(tracksAndFxsMixer[(int)MixerAndFxChannelType::kLeft],
                        tracksAndFxsMixer[(int)MixerAndFxChannelType::kRight],
                        MixerAndFxs<kVoices>::TrackAndFxsChannels::kDelayChannel,
                        MixerAndFxs<kVoices>::TrackAndFxsChannels::kReverbChannel);
}

void audioEngine::setSendDuck(const DuckParams& params)
{
    sendDuck.set(params);
}

const DuckParams& audioEngine::getSendDuck() const
{
    return sendDuck.params();
}

float audioEngine::getSendDuckGain() const
{
    return sendDuck.lastGain();
}
#endif
