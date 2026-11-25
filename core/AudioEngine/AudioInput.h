#ifndef AUDIOENGINE_AUDIOINPUT_H_
#define AUDIOENGINE_AUDIOINPUT_H_

#include "Audio.h"

class AudioInput
{
  public:
    enum InputChannels
    {
        kLeft,
        kRight,
        kChannelsCount
    };

    AudioInput();
    AudioEffectDynamics* getGate(InputChannels channel);
    AudioStereoToMono* getStereoToMono();
    AudioRecordQueue* getRecordingQueueI2s(int channel);
    AudioAnalyzeRMS* getRecordingRmsI2s(int channel);
    AudioRecordQueue* getRecordingQueueUsb(int channel);
    AudioAnalyzeRMS* getRecordingRmsUsb(int channel);
    AudioInputStream* getI2sStream();
    AudioInputStream* getUsbStream();
    // both stream buffers of an audio block in one call (the audio input interrupt: one frame instead of the caller keeping every value across two getter calls)
    void setInBuffers(void* i2sData, void* usbData);
    AudioAnalyzeRMS* getStreamRmsI2s(InputChannels channel);
    AudioAmplifier* getUsbRecordingAmplifier(InputChannels channel);

  private:
#ifdef PLATFORM_IMX
    AudioInputStream i2sStream;   // the IMX input interrupt already copies to its own buffer
#else
    AudioInputStream i2sStream{ false, true };   // line in through the block ring (core/libs/Audio/LineInRing.h)
#endif
    AudioInputStream usbStream{ true }; // no buffer on the original (mtAudioEngine::updateInputAudioBuffers): no blocks
    AudioAmplifier usbRecordingAmplifier[InputChannels::kChannelsCount];
    AudioRecordQueue recordingQueueI2s[InputChannels::kChannelsCount];
    AudioAnalyzeRMS recordingRmsI2s[InputChannels::kChannelsCount];
    AudioRecordQueue recordingQueueUsb[InputChannels::kChannelsCount];
    AudioAnalyzeRMS recordingRmsUsb[InputChannels::kChannelsCount];

    // live stream has the setting to pick one of the stereo channels, so it's better to use
    // AudioStereoToMono instead of mixer
    AudioStereoToMono stereoToMono;
    AudioEffectDynamics gate[InputChannels::kChannelsCount];
    AudioAnalyzeRMS streamRms[InputChannels::kChannelsCount];

    std::vector<AudioConnection*> connections;
};

#endif /* AUDIOENGINE_AUDIOINPUT_H_ */
