#ifndef LIBS_AUDIO_FX_MUTE_GATE_H_
#define LIBS_AUDIO_FX_MUTE_GATE_H_
#include <stdint.h>

#include "AudioStream.h"

// Mute of a master send FX (AudioEffectPolyverb, AudioEffectShortDelay) that takes its whole cost away, SYNTH_OPEN images
// (AUDIO_NULL_IS_SILENCE). The Mixer's Mute (mtProject.values.reverbMute / delayMute) already sets every send into the FX to 0, the
// gate then lets the tail ring as stock does for at most kTailBlocks, fades the output out over kFadeBlocks, clears the FX state and
// takes the FX and its send mixer out of update_all (AudioStream::sleepStream): no update, and transmit drops every block for them.
// Unmute wakes both (setMuted(false)), the FX starts from a clean state. See ref/fx-mute-design.md.
//
// Threading: step() runs inside update(), setMuted() before update_all (audioEngine::update, same interrupt).
class FxMuteGate
{
  public:
    static constexpr uint32_t kTailBlocks = 1034; // 3.0 s at 44,117.647 Hz in blocks of 128 samples
    static constexpr int32_t kFadeBlocks = 32;   // 93 ms
    static constexpr int32_t kOne = 1 << 15;     // output gain one (Q15)
    static constexpr int32_t kFadeStep = kOne / kFadeBlocks;

    enum Step : uint8_t
    {
        kRun,        // not muted, or the tail rings: the stock update
        kFade,       // the stock update, then the output is ramped from rampFrom() to rampTo()
        kEnterSleep, // clear the FX state, then sleep() it and the feed
        kAsleep      // muted and asleep (update() is not called while asleep, this is the block of a race with the main loop)
    };

    void setFeed(AudioStream* feed) { feed_ = feed; }
#ifdef FEATURE_STEREO_SENDS
    // the side send mixer of the stereo sends (stereo_sends.h) sleeps and wakes with the feed
    void setFeed2(AudioStream* feed) { feed2_ = feed; }
#endif

    // Mute: step() does the rest. Unmute: the flag first, then the two streams go back into update_all (audioEngine::update calls it in
    // the audio interrupt before update_all, the barrier keeps the order for a caller in the main loop).
    void setMuted(bool muted, AudioStream* self)
    {
        muted_ = muted;
        if (!muted)
        {
            __asm volatile("" ::: "memory"); // the flag is stored before the streams are active again
            if (feed_ != nullptr)
                feed_->wakeStream();
#ifdef FEATURE_STEREO_SENDS
            if (feed2_ != nullptr)
                feed2_->wakeStream();
#endif
            self->wakeStream();
        }
    }
    bool muted() const { return muted_; }
    // Skip what is left of the ring phase (USB 8+3 auto mute, fx_auto_mute.h): the fade starts at the next block. Muted only.
    void cutRing()
    {
        if (muted_ && blocks_ < kTailBlocks)
            blocks_ = kTailBlocks;
    }
    bool asleep() const { return asleep_; }

    // Audio interrupt, once per update(), before anything else of the FX.
    Step step()
    {
        if (!muted_)
        {
            if (asleep_)
            {
                // woken: the state was cleared when it fell asleep, full output at once
                asleep_ = false;
                gain_ = kOne;
                blocks_ = 0;
                return kRun;
            }
            blocks_ = 0;
            if (gain_ == kOne)
                return kRun;
            from_ = gain_; // unmuted during the fade: back up at the same rate
            gain_ = gain_ + kFadeStep > kOne ? kOne : gain_ + kFadeStep;
            return kFade;
        }
        if (asleep_)
            return kAsleep;
        if (gain_ == kOne && blocks_ < kTailBlocks)
        {
            blocks_++;
            return kRun;
        }
        if (gain_ == 0)
        {
            // the last fade block ended at 0: nothing more to hear
            asleep_ = true;
            return kEnterSleep;
        }
        from_ = gain_;
        gain_ = gain_ > kFadeStep ? gain_ - kFadeStep : 0;
        return kFade;
    }
    int32_t rampFrom() const { return from_; }
    int32_t rampTo() const { return gain_; }

    // Audio interrupt, kEnterSleep: the FX (after it cleared its state) and its feed leave update_all.
    void sleep(AudioStream* self)
    {
        if (feed_ != nullptr)
            feed_->sleepStream();
#ifdef FEATURE_STEREO_SENDS
        if (feed2_ != nullptr)
            feed2_->sleepStream();
#endif
        self->sleepStream();
    }

    // kFade: a linear ramp from rampFrom() to rampTo() over the block, in place (AudioStream.cpp, out of line: off the hot path of update)
    static void ramp(int16_t* data, int32_t from, int32_t to);

  private:
    AudioStream* feed_ = nullptr;
#ifdef FEATURE_STEREO_SENDS
    AudioStream* feed2_ = nullptr;
#endif
    volatile bool muted_ = false;
    bool asleep_ = false;
    uint16_t blocks_ = 0;
    int32_t gain_ = kOne;
    int32_t from_ = kOne;
};

#endif /* LIBS_AUDIO_FX_MUTE_GATE_H_ */
