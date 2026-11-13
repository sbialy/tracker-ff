#pragma once

#include "AudioStream.h"
#include "mixer.h"

class Synth;

class PolySynth : public AudioStream
{
  public:
    PolySynth(void)
      : AudioStream(0, NULL)
    {
    }

    void init(Synth* trackerSynth);
    void setGain(int idx, float gainDB);
#ifdef AUDIO_NULL_IS_SILENCE
    // The pre FX mixers the send outputs feed, and the mixer input of part 0 (part n is firstInput + n). With both gains of a part at 0
    // the part does not build its send blocks (the mixers would release them unread).
    void setSendMixers(const AudioMixerBase* delayMixer, const AudioMixerBase* reverbMixer, int firstInput)
    {
        sendMixer[0] = delayMixer;
        sendMixer[1] = reverbMixer;
        firstSendInput = firstInput;
    }
#endif
    virtual void update(void) override;

    static constexpr int kSend1Offset = 2 * kSynthParts;
    static constexpr int kSend2Offset = 3 * kSynthParts;
    static constexpr int kRightChannelOffset = kSynthParts;

  private:
    Synth* trackerSynth = nullptr;
    bool isBlocked = true;
    float gainDB[kSynthParts] = {};
#ifdef AUDIO_NULL_IS_SILENCE
    const AudioMixerBase* sendMixer[2] = {};
    int firstSendInput = 0;
#endif
};
