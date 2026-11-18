#ifndef LIBS_AUDIO_EFFECT_DUCK_H_
#define LIBS_AUDIO_EFFECT_DUCK_H_

// Send ducking: one chosen source (a track, a line in channel or a synth part) pulls the delay and reverb returns down while it
// sounds. The detector is the limiter sidechain's: the same input numbering as AudioMux (0..7 tracks, 8 and 9 line in, 10.. synth
// parts), the source is the selected input of one stream, the other inputs are only released. The result is not audio. It is a gain
// per block that goes straight into the return inputs of the tracks and sends mixer (their own gain ramp makes it click free), so
// the cost is a peak scan of one block plus four gain() calls, no extra pass over the returns.
//
// Off (source 0): the stream is inactive. It takes no block, runs no update and leaves the return gains at 1.0, exactly as a build
// without the feature.
//
// duck_math.h has the arithmetic (host tested without the audio graph), this file the stream around it.

#include "AudioStream.h"
#include "duck_math.h"
#include "interruptsControl.h"
#include "mux.h"

static_assert(kDuckSources == MUX_SIZE, "the detector takes the inputs of the limiter sidechain mux");

template<class MixerT>
class AudioEffectDuck : public AudioStream
{
  public:
    AudioEffectDuck(void)
      : AudioStream(MUX_SIZE, inputQueueArray)
    {
        for (int i = 0; i < MUX_SIZE; i++)
            inputQueueArray[i] = nullptr;
        // the engine's connections make it active, set() with source 0 takes it out of the update list again
    }

    // The two return mixers (left, right) and the input of each that carries the delay and the reverb return
    void setTargets(MixerT* left, MixerT* right, unsigned delayInput, unsigned reverbInput)
    {
        mixer[0] = left;
        mixer[1] = right;
        delayCh = delayInput;
        reverbCh = reverbInput;
    }

    // Main loop (project load, the page's encoder). The audio interrupt only reads state, so the whole change is made with it off.
    void set(const DuckParams& p)
    {
        DISABLE_AUDIO_INTERRUPTS();
        state.configure(p);
        const bool on = state.params.source != 0;
        if (on)
            active = numConnections > 0;
        else
        {   // no update from now on and no block is delivered: what is still queued goes back to the pool, the returns to unity
            active = false;
            for (uint8_t ch = 0; ch < MUX_SIZE; ch++)
            {
                audio_block_t* in = receiveReadOnly(ch);
                if (in)
                    release(in);
            }
            for (int c = 0; c < 2; c++)
            {
                if (!mixer[c])
                    continue;
                mixer[c]->gain(delayCh, 1.0f);
                mixer[c]->gain(reverbCh, 1.0f);
            }
        }
        ENABLE_AUDIO_INTERRUPTS();
    }

    const DuckParams& params() const { return state.params; }

    // The gain applied to both returns in the last block (1.0 = no ducking), for the level meter of the page
    float lastGain() const { return state.gain; }

    virtual void update(void)
    {
        const uint8_t sel = state.params.source ? uint8_t(state.params.source - 1) : uint8_t(0xFF);
        uint16_t peak = 0;
        for (uint8_t ch = 0; ch < MUX_SIZE; ch++)
        {
            audio_block_t* in = receiveReadOnly(ch);
            if (!in)
                continue;
            if (ch == sel)
                peak = duckBlockPeak(in->data);
            release(in);
        }
        if (sel == 0xFF)
            return;
        float g;
        if (!state.step(peak, g))
            return; // the gain is where it was last block and the mixers already hold it
        for (int c = 0; c < 2; c++)
        {
            if (!mixer[c])
                continue;
            mixer[c]->gain(delayCh, g);
            mixer[c]->gain(reverbCh, g);
        }
    }

  private:
    DuckState state;
    MixerT* mixer[2] = {nullptr, nullptr};
    unsigned delayCh = 1, reverbCh = 2;
    audio_block_t* inputQueueArray[MUX_SIZE];
};

#endif /* LIBS_AUDIO_EFFECT_DUCK_H_ */
