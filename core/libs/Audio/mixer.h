/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef audio_mixer_h_
#define audio_mixer_h_

#include "AudioStream.h"
#include "dspApplyGain.h"
#include "util_math.h"

#ifdef AUDIO_NULL_IS_SILENCE
// The per block work of every mixer size is one function (mixer.cpp, AudioMixerBase::mix), the template only holds the gain tables
class AudioMixerBase : public AudioStream
{
  public:
    // Room of the input table on the stack of mix(), the most inputs one mixer of this platform may have. The Tracker+ and Mini audio graph
    // has 18 and 21 input mixers (as upstream), the original (MK66) has 13 at most (the send mixers: 8 tracks + 2 external + kSynthParts
    // synth sends) and keeps the smaller table, every word of it is ISR stack. AudioMixer<N> refuses N above this at compile time.
#ifdef PLATFORM_IMX
    static constexpr int kMaxInputs = 21;
#else
    static constexpr int kMaxInputs = 13;
#endif

  protected:
    AudioMixerBase(unsigned char inputs, audio_block_t** queue)
      : AudioStream(inputs, queue)
    {
    }

  private:
    // Only AudioMixer<N> calls it, and only with N <= kMaxInputs (static_assert in AudioMixer), so the table can never be overrun
    template<int mixerSize>
    friend class AudioMixer;
    void mix(int size, int32_t* multiplier, int32_t* previousMultiplier, SaturationType saturationType);

  public:
    // The input adds nothing: its gain is 0 now and was 0 in the previous block, so mix() releases whatever arrives there unread
    // (a missing block ends the gain ramp the same way). A sender may skip building the block.
    virtual bool isInputSilenced(unsigned int channel) const
    {
        (void)channel;
        return false;
    }
};
#define AUDIO_MIXER_PARENT AudioMixerBase
#else
#define AUDIO_MIXER_PARENT AudioStream
#endif

template<int mixerSize>
class AudioMixer : public AUDIO_MIXER_PARENT
{
  public:
    static constexpr float kUnityGain = 65536.0f;
    static constexpr float kMinGain = -32767.0f;
    static constexpr float kMaxGain = 32767.0f;

#ifdef AUDIO_NULL_IS_SILENCE
    static_assert(mixerSize > 0 && mixerSize <= AudioMixerBase::kMaxInputs,
                  "this mixer has more inputs than AudioMixerBase::kMaxInputs of the platform (mixer.h)");
#endif

    AudioMixer(void)
      : AUDIO_MIXER_PARENT(mixerSize, inputQueueArray)
    {
        for (int i = 0; i < mixerSize; i++)
        {
            multiplier[i] = (int32_t)kUnityGain;
            previousMultiplier[i] = (int32_t)kUnityGain;
        }

        setSaturationType(SaturationType::kHardClip);
    }
    virtual void update(void);
    void gain(unsigned int channel, float gain)
    {
        if (channel >= mixerSize)
        {
            return;
        }

        gain = clamp<float>(gain, kMinGain, kMaxGain);

        previousMultiplier[channel] = multiplier[channel];
        multiplier[channel] = gain * kUnityGain;
    }

    void setSaturationType(SaturationType type) { saturationType = type; }

#ifdef AUDIO_NULL_IS_SILENCE
    bool isInputSilenced(unsigned int channel) const override
    {
        return channel < mixerSize && multiplier[channel] == 0 && previousMultiplier[channel] == 0;
    }
#endif

  private:
    SaturationType saturationType;
    int32_t multiplier[mixerSize];
    int32_t previousMultiplier[mixerSize];
    audio_block_t* inputQueueArray[mixerSize];
};

#endif
