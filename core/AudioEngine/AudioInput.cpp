#include "AudioInput.h"

AudioInput::AudioInput()
{
    // recording i2s
    connections.push_back(
      new AudioConnection(&i2sStream, InputChannels::kLeft, &stereoToMono, InputChannels::kLeft));
    connections.push_back(
      new AudioConnection(&i2sStream, InputChannels::kRight, &stereoToMono, InputChannels::kRight));

    connections.push_back(new AudioConnection(
      &stereoToMono, InputChannels::kLeft, &recordingRmsI2s[InputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(
      &stereoToMono, InputChannels::kRight, &recordingRmsI2s[InputChannels::kRight], 0));

    connections.push_back(new AudioConnection(
      &stereoToMono, InputChannels::kLeft, &recordingQueueI2s[InputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(
      &stereoToMono, InputChannels::kRight, &recordingQueueI2s[InputChannels::kRight], 0));

    // recording usb
    connections.push_back(new AudioConnection(
      &usbStream, InputChannels::kLeft, &usbRecordingAmplifier[InputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(
      &usbStream, InputChannels::kRight, &usbRecordingAmplifier[InputChannels::kRight], 0));
    connections.push_back(new AudioConnection(
      &usbRecordingAmplifier[InputChannels::kLeft], 0, &recordingRmsUsb[InputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(&usbRecordingAmplifier[InputChannels::kRight],
                                              0,
                                              &recordingRmsUsb[InputChannels::kRight],
                                              0));
    connections.push_back(new AudioConnection(&usbRecordingAmplifier[InputChannels::kLeft],
                                              0,
                                              &recordingQueueUsb[InputChannels::kLeft],
                                              0));
    connections.push_back(new AudioConnection(&usbRecordingAmplifier[InputChannels::kRight],
                                              0,
                                              &recordingQueueUsb[InputChannels::kRight],
                                              0));

    // live processing i2s
    connections.push_back(new AudioConnection(
      &stereoToMono, InputChannels::kLeft, &streamRms[InputChannels::kLeft], 0));
    connections.push_back(new AudioConnection(
      &stereoToMono, InputChannels::kRight, &streamRms[InputChannels::kRight], 0));

    connections.push_back(
      new AudioConnection(&stereoToMono, InputChannels::kLeft, &gate[InputChannels::kLeft], 0));
    connections.push_back(
      new AudioConnection(&stereoToMono, InputChannels::kRight, &gate[InputChannels::kRight], 0));
}

AudioEffectDynamics* AudioInput::getGate(InputChannels channel)
{
    return &gate[channel];
}

AudioStereoToMono* AudioInput::getStereoToMono()
{
    return &stereoToMono;
}

AudioRecordQueue* AudioInput::getRecordingQueueI2s(int channel)
{
    return &recordingQueueI2s[channel];
}

AudioRecordQueue* AudioInput::getRecordingQueueUsb(int channel)
{
    return &recordingQueueUsb[channel];
}

AudioAnalyzeRMS* AudioInput::getRecordingRmsI2s(int channel)
{
    return &recordingRmsI2s[channel];
}

AudioAnalyzeRMS* AudioInput::getRecordingRmsUsb(int channel)
{
    return &recordingRmsUsb[channel];
}

AudioInputStream* AudioInput::getI2sStream()
{
    return &i2sStream;
}

AudioInputStream* AudioInput::getUsbStream()
{
    return &usbStream;
}

void AudioInput::setInBuffers(void* i2sData, void* usbData)
{
    i2sStream.setInBuffer(i2sData);
    usbStream.setInBuffer(usbData);
}

AudioAnalyzeRMS* AudioInput::getStreamRmsI2s(InputChannels channel)
{
    return &streamRms[channel];
}

AudioAmplifier* AudioInput::getUsbRecordingAmplifier(InputChannels channel)
{
    return &usbRecordingAmplifier[channel];
}
