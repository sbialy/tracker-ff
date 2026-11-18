// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "AudioStream.h"

#include "reverb.h"
#ifdef AUDIO_NULL_IS_SILENCE
#include "fx_mute_gate.h"
#endif

class AudioEffectPolyverb : public AudioStream
{
  public:
    AudioEffectPolyverb();
    ~AudioEffectPolyverb();
    virtual void update(void);

    // Reset/clear buffers
    void reset();

    // Does reverb still produce sound

    // Set threshold of activity check (sample value)
    void setActiveThreshold(float threshold);

    // Wet/dry ratio, 1.0 full wet
    void setAmount(float amount) { reverb.SetAmount(amount); }

    // Decay time, 1.0 infinite
    void setTime(float time) { reverb.SetTime(time); }

    // High frequency damping, 0-1
    void setDamp(float damp) { reverb.SetLP(damp); }

    // Delay lines diffusion ratio, 0-1
    void SetDiffusion(float diffusion) { reverb.SetDiffusion(diffusion); }

    // Predelay length, 0-1
    void SetPredelayLength(float length) { reverb.SetPredelayLength(length); }

    void blockUpdate() { isBlockedUpdate = true; }

    void unblockUpdate() { isBlockedUpdate = false; }

#ifdef AUDIO_NULL_IS_SILENCE
    // Mixer Mute (fx_mute_gate.h): feed is the send mixer, muted the project's reverbMute
    void setMuteFeed(AudioStream* feed) { muteGate.setFeed(feed); }
    void setMuted(bool muted) { muteGate.setMuted(muted, this); }
    bool isMuted() const { return muteGate.muted(); }
    void cutMuteRing() { muteGate.cutRing(); }
    bool isMuteAsleep() const { return muteGate.asleep(); }
#endif

  private:
    audio_block_t* inputQueueArray[2];
    Reverb reverb;
    uint16_t* buffer;
    bool isBlockedUpdate;
#ifdef AUDIO_NULL_IS_SILENCE
    FxMuteGate muteGate;
#endif
};
