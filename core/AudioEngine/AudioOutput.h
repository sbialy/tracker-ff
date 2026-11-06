#ifndef AUDIOENGINE_AUDIOOUTPUT_H_
#define AUDIOENGINE_AUDIOOUTPUT_H_

#include "Audio.h"
#ifdef FEATURE_FF_DIRT
#include "ff_dirt.h"
#endif
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif

class AudioOutput
{
  public:
    enum OutputChannels
    {
        kLeft,
        kRight,
        kChannelsCount
    };

    enum FinalMixInputs
    {
        kStream = 0,
        kSdCardPreview = 1,
        kTestWaveform = 4,
        kMetronomeTick = 5,
        kNesEmulatorSound = 6,
        kFinalMixerSize
    };

    AudioOutput();
#ifndef PLATFORM_IMX
    AudioMux* getLimiterSidechainMux();
#else
    UsbOutputStream* getUsbStream();
    AudioMux* getLimiterSidechainMux(OutputChannels channel);
#endif
    AudioEffectLimiter* getLimiter(OutputChannels channel);
#ifdef FEATURE_FF_DIRT
    AudioEffectDirt* getDirt();
#endif
#if defined(FEATURE_FF_DIRT) || defined(FEATURE_PLUG_HOST)
    AudioStream* getMasterInsert();   // the first node after the tracks and sends mixers: Dirt, else the plugin node
#endif
    AudioMixer<FinalMixInputs::kFinalMixerSize>* getFinalMixer(OutputChannels channel);
    AudioOutputStream* getStream();

  private:
#ifdef FEATURE_FF_DIRT
    // before the limiters: the audio objects update in creation order, so the master bus stays in the same block
    AudioEffectDirt dirt;
#endif
#ifdef FEATURE_PLUG_HOST
    // the two master insert plugin slots (synth-open/plug_host.h), after Dirt when the build has it
    AudioEffectPlug plug;
#endif
#ifndef PLATFORM_IMX
    AudioMux limiterSidechainMux;
#else
    AudioMux limiterSidechainMux[OutputChannels::kChannelsCount];
    UsbOutputStream usbOutput;
#endif
    AudioEffectLimiter limiter[OutputChannels::kChannelsCount];
    AudioOutputStream outputStream;
    AudioMixer<FinalMixInputs::kFinalMixerSize> finalMixer[OutputChannels::kChannelsCount];
    std::vector<AudioConnection*> connections;
};

#endif /* AUDIOENGINE_AUDIOOUTPUT_H_ */
