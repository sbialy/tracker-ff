#pragma once

// USB audio 8+3 mono (Config > USB > Audio mode, USB_AUDIO=1 images on the original): the last stream of the audio
// graph. It reads one block of each of 14 sources and hands the pointers to the platform, which interleaves them into the USB ring
// (platforms/mk66/usbaudio, ffUsbAudioMultiBlock). Inputs: tracks 1-8 from the voice amplifier (post volume and mute, pre pan, dry),
// then the post fader left (8..10) and right (11..13) outputs of synth parts 1-3 (pre pan), summed to mono by the platform.
// Created by audioEngine::init only in a session that streams 8+3 mono, so the stereo and off sessions run the graph as before.

#ifndef PLATFORM_IMX

#include "AudioStream.h"
#include "Platform.h"

class UsbMultiTap : public AudioStream
{
  public:
    static constexpr int kTracks = 8;
    static constexpr int kSynthParts = 3;
    static constexpr int kInputs = kTracks + 2 * kSynthParts;
    static constexpr int kSynthLeft = kTracks;
    static constexpr int kSynthRight = kTracks + kSynthParts;

    explicit UsbMultiTap(Platform::UsbMultiSink sink)
      : AudioStream(kInputs, inputQueueArray)
      , sink_(sink)
    {
    }

  private:
    void update(void) override;

    audio_block_t* inputQueueArray[kInputs];
    Platform::UsbMultiSink sink_;
};

#endif
