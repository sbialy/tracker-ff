#include "AudioOutput.h"

#ifdef FEATURE_PLUG_HOST
AudioEffectPlug* g_plugInsert;
AudioEffectPlug* plugInsert() { return g_plugInsert; }
#endif

AudioOutput::AudioOutput()
{
    constexpr int kLimiterSidechainChannelIdx = 1;

#ifndef PLATFORM_IMX
    connections.push_back(new AudioConnection(
      &limiterSidechainMux, 0, &limiter[OutputChannels::kLeft], kLimiterSidechainChannelIdx));
    connections.push_back(new AudioConnection(
      &limiterSidechainMux, 0, &limiter[OutputChannels::kRight], kLimiterSidechainChannelIdx));
#else
    connections.push_back(new AudioConnection(&limiterSidechainMux[OutputChannels::kLeft],
                                              0,
                                              &limiter[OutputChannels::kLeft],
                                              kLimiterSidechainChannelIdx));
    connections.push_back(new AudioConnection(&limiterSidechainMux[OutputChannels::kRight],
                                              0,
                                              &limiter[OutputChannels::kRight],
                                              kLimiterSidechainChannelIdx));
#endif

#ifdef FEATURE_FF_DIRT
    // the tracks and sends mixers feed the Dirt insert (mtAudioEngine.cpp, connectMixerToOutput), which feeds the limiters
#ifdef FEATURE_PLUG_HOST
    connections.push_back(new AudioConnection(&dirt, 0, &plug, 0));
    connections.push_back(new AudioConnection(&dirt, 1, &plug, 1));
    connections.push_back(new AudioConnection(&plug, 0, &limiter[OutputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(&plug, 1, &limiter[OutputChannels::kRight], 0));
    g_plugInsert = &plug;
#else
    connections.push_back(new AudioConnection(&dirt, 0, &limiter[OutputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(&dirt, 1, &limiter[OutputChannels::kRight], 0));
#endif
#endif

    constexpr int kStreamInputIdx = 0;
    connections.push_back(new AudioConnection(
      &limiter[OutputChannels::kLeft], 0, &finalMixer[OutputChannels::kLeft], kStreamInputIdx));
    connections.push_back(new AudioConnection(
      &limiter[OutputChannels::kRight], 0, &finalMixer[OutputChannels::kRight], kStreamInputIdx));

    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kLeft], 0, &outputStream, OutputChannels::kLeft));
    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kRight], 0, &outputStream, OutputChannels::kRight));

#ifdef PLATFORM_IMX
    constexpr int kMasterLeftUsbStreamIdx =
      UsbOutputStream::AudioSource::kMaster * UsbOutputStream::SourceChannel::kChannelsCount;
    constexpr int kMasterRightUsbStreamIdx = kMasterLeftUsbStreamIdx + 1;

    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kLeft], 0, &usbOutput, kMasterLeftUsbStreamIdx));
    connections.push_back(new AudioConnection(
      &finalMixer[OutputChannels::kRight], 0, &usbOutput, kMasterRightUsbStreamIdx));

    usbOutput.connectInputToUsbChannel(
      UsbOutputStream::kMaster, UsbOutputStream::SourceChannel::kLeft, 0);
    usbOutput.connectInputToUsbChannel(
      UsbOutputStream::kMaster, UsbOutputStream::SourceChannel::kRight, 1);

    for (int idx = UsbOutputStream::AudioSource::kFirstTrack;
         idx < UsbOutputStream::AudioSource::kMaster;
         idx++)
    {
        constexpr int channelsOffset = 1;
        int usbChannelLeft = (idx + channelsOffset) * 2;
        int usbChannelRight = usbChannelLeft + 1;

        usbOutput.connectInputToUsbChannel(
          idx, UsbOutputStream::SourceChannel::kLeft, usbChannelLeft);
        usbOutput.connectInputToUsbChannel(
          idx, UsbOutputStream::SourceChannel::kRight, usbChannelRight);
    }
#endif
}

#ifndef PLATFORM_IMX
AudioMux* AudioOutput::getLimiterSidechainMux()
{
    return &limiterSidechainMux;
}
#else
AudioMux* AudioOutput::getLimiterSidechainMux(OutputChannels channel)
{
    return &limiterSidechainMux[channel];
}
#endif

AudioEffectLimiter* AudioOutput::getLimiter(OutputChannels channel)
{
    return &limiter[channel];
}

#ifdef FEATURE_FF_DIRT
AudioEffectDirt* AudioOutput::getDirt()
{
    return &dirt;
}
#endif

AudioMixer<AudioOutput::FinalMixInputs::kFinalMixerSize>* AudioOutput::getFinalMixer(
  OutputChannels channel)
{
    return &finalMixer[channel];
}

AudioOutputStream* AudioOutput::getStream()
{
    return &outputStream;
}

#ifdef PLATFORM_IMX
UsbOutputStream* AudioOutput::getUsbStream()
{
    return &usbOutput;
}
#endif
