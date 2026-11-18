#ifndef LIBS_AUDIO_FX_AUTO_MUTE_H_
#define LIBS_AUDIO_FX_AUTO_MUTE_H_
#include <stdint.h>

// Auto mute of the master delay and reverb while USB audio streams 8+3 mono (Config > USB > "FX in 8+3").
// A RUNTIME override on top of the Mixer's Mute (FxMuteGate): the project flags (delayMute, reverbMute) are never written, saved
// projects keep the user's state, and the effective mute is "flag or override" (fxDelayMuted / fxReverbMuted in mtAudioEngine.cpp).
// The 8+3 channels are tapped before the sends (tracks: voice amplifier, synth: post fader, both dry), so the recording does not change.
//
// Debounce: macOS may keep the isochronous stream running while an app only has the Tracker selected as input. The override engages
// after the host has recorded for kEngageBlocks in a row and releases after kReleaseBlocks without streaming, so a short gap or
// a short burst does not toggle the FX. tick() runs in the audio interrupt, once per block (audioEngine::update).

// ms to blocks of kBlockFrames at the MK66 I2S rate (44,117.647 Hz), rounded up (1,000 ms = 345 blocks of 2.901 ms)
constexpr uint32_t fxAutoMuteBlocks(uint32_t ms)
{
    return uint32_t((uint64_t(ms) * 44117647u + (uint64_t(128) * 1000000u) - 1u) / (uint64_t(128) * 1000000u));
}

class FxAutoMute
{
  public:
    static constexpr uint32_t kEngageMs = 1000;
    static constexpr uint32_t kReleaseMs = 1000;
    static constexpr uint32_t kEngageBlocks = fxAutoMuteBlocks(kEngageMs);
    static constexpr uint32_t kReleaseBlocks = fxAutoMuteBlocks(kReleaseMs);

    // Config > USB > FX in 8+3 (Auto mute, the default). Off releases at once.
    void setEnabled(bool on)
    {
        enabled_ = on;
        if (!on)
            reset();
    }
    bool enabled() const { return enabled_; }

    // The override is on: the FX are muted for the sound, the Mute keys are locked.
    bool active() const { return active_; }

    // Audio interrupt, once per block. streaming: Platform::usbAudioStreaming() (0 no host, 1 stereo, 2 8+3 mono). offline: the offline
    // render (export), where the FX belong to the result, so the override is released at once. Returns true when active() changed.
    bool tick(uint8_t streaming, bool offline)
    {
        const bool was = active_;
        const bool want = enabled_ && streaming == 2 && !offline;
        if (offline)
        {
            reset();
        }
        else if (want)
        {
            idle_ = 0;
            if (!active_ && ++run_ >= kEngageBlocks)
            {
                active_ = true;
                run_ = 0;
            }
        }
        else
        {
            run_ = 0;
            if (active_ && ++idle_ >= kReleaseBlocks)
            {
                active_ = false;
                idle_ = 0;
            }
        }
        return active_ != was;
    }

  private:
    void reset()
    {
        active_ = false;
        run_ = 0;
        idle_ = 0;
    }

    volatile bool active_ = false;
    bool enabled_ = true;
    uint16_t run_ = 0;
    uint16_t idle_ = 0;
};

#ifdef AUDIO_NULL_IS_SILENCE
extern FxAutoMute fxAutoMute;
inline bool fxAutoMuteActive()
{
    return fxAutoMute.active();
}
#else
inline bool fxAutoMuteActive()
{
    return false;
}
#endif

#endif /* LIBS_AUDIO_FX_AUTO_MUTE_H_ */
