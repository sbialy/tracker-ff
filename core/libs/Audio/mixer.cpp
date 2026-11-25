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

#include "mixer.h"
#include "dspApplyGain.h"

#ifdef AUDIO_NULL_IS_SILENCE
// Same result as the stock loop below for every input (bit identical), with less work:
//  - a NULL input is silence: its gain ramp ends (previous = target) as a block of zeros would end it
//  - an input with gain 0 (target and previous) adds nothing: its block is just released
//  - nothing left to add: no output block (NULL, which every consumer reads as silence)
//  - one input left, hard clip (the only type a track mixer uses by default, saturate16 of an int16 is the identity):
//      unity gain: the input block itself is forwarded, nothing is copied, mixed or clipped
//      other gain: the gain is applied in place (applyGain is applyGainAndCopy + saturate16 for one input)
//  - several inputs: the first one with a gain initialises the sum (no memset for a muted first channel, no copy of a shared block)
AUDIO_CORE_RAM void AudioMixerBase::mix(int size,
                                        int32_t* multiplier,
                                        int32_t* previousMultiplier,
                                        SaturationType saturationType)
{
    audio_block_t* in[kMaxInputs]; // size <= kMaxInputs: only AudioMixer<N> calls mix() and it static_asserts N
    int first = -1;
    int contributors = 0;

    for (int channel = 0; channel < size; channel++)
    {
        audio_block_t* block = receiveReadOnly(channel);
        if (block == nullptr)
        {
            previousMultiplier[channel] = multiplier[channel];
        }
        else if (multiplier[channel] == 0 && previousMultiplier[channel] == 0)
        {
            release(block);
            block = nullptr;
        }
        else
        {
            if (first < 0)
                first = channel;
            contributors++;
        }
        in[channel] = block;
    }
    if (contributors == 0)
        return;

    audio_block_t* out = in[first];
    if (contributors == 1 && saturationType == SaturationType::kHardClip)
    {
        if (multiplier[first] != MULTI_UNITYGAIN || previousMultiplier[first] != MULTI_UNITYGAIN)
        {
            if (out->ref_count > 1)
            {
                audio_block_t* copy = allocate();
                if (copy)
                    memcpy(copy->data, out->data, sizeof(copy->data));
                release(out);
                out = copy;
                if (out == nullptr)
                    return;
            }
            applyGain(out->data, multiplier[first], previousMultiplier[first]);
        }
        transmit(out);
        release(out);
        return;
    }

    int32_t mixedBuffer[AUDIO_BLOCK_SAMPLES];
    if (out->ref_count > 1)
    {
        out = allocate(); // the sum is written over it, nothing of the input is needed in it
        if (out == nullptr)
        {
            for (int channel = first; channel < size; channel++)
            {
                if (in[channel])
                    release(in[channel]);
            }
            return;
        }
        applyGainAndCopy(mixedBuffer, in[first]->data, multiplier[first], previousMultiplier[first]);
        release(in[first]);
    }
    else
    {
        applyGainAndCopy(mixedBuffer, out->data, multiplier[first], previousMultiplier[first]);
    }
    for (int channel = first + 1; channel < size; channel++)
    {
        if (in[channel] != nullptr)
        {
            applyGainThenAdd(mixedBuffer, in[channel]->data, multiplier[channel], previousMultiplier[channel]);
            release(in[channel]);
        }
    }
    saturate(out->data, mixedBuffer, saturationType);
    transmit(out);
    release(out);
}

template<int mixerSize>
AUDIO_CORE_RAM void AudioMixer<mixerSize>::update(void)
{
    static_assert(mixerSize <= AudioMixerBase::kMaxInputs, "AudioMixerBase::mix has no room for that many inputs (mixer.h)");
    this->mix(mixerSize, multiplier, previousMultiplier, saturationType);
}
#else
template<int mixerSize>
AUDIO_CORE_RAM void AudioMixer<mixerSize>::update(void)
{

    audio_block_t *in = nullptr, *out = nullptr;

    int32_t mixedBuffer[AUDIO_BLOCK_SAMPLES];

    for (int channel = 0; channel < mixerSize; channel++)
    {
        if (nullptr == out)
        {
            out = receiveWritable(channel);
            if (out)
            {
                applyGainAndCopy(
                  mixedBuffer, out->data, multiplier[channel], previousMultiplier[channel]);
            }
#ifdef AUDIO_NULL_IS_SILENCE
            else
            {
                previousMultiplier[channel] = multiplier[channel]; // a block of zeros ends the gain ramp
            }
#endif
        }
        else
        {
            in = receiveReadOnly(channel);
            if (nullptr != in)
            {
                applyGainThenAdd(
                  mixedBuffer, in->data, multiplier[channel], previousMultiplier[channel]);
                release(in);
            }
#ifdef AUDIO_NULL_IS_SILENCE
            else
            {
                previousMultiplier[channel] = multiplier[channel];
            }
#endif
        }
    }
    if (nullptr != out)
    {
        saturate(out->data, mixedBuffer, saturationType);

        transmit(out);
        release(out);
    }
}
#endif

// if you use this template with other value you should add it in this place to force compilation
// for every class in one .cpp file (to avoid symbol visibility issues)
template class AudioMixer<2>;
template class AudioMixer<4>;
template class AudioMixer<7>;
template class AudioMixer<8>;
template class AudioMixer<10>;
#ifdef PLATFORM_IMX
template class AudioMixer<18>;
template class AudioMixer<11>;
template class AudioMixer<21>;
#elif defined(FEATURE_SYNTH_OPEN)
// MixerAndFxs mixes 8 tracks + kSynthParts synth channels on MK66 as well
template class AudioMixer<11>;
// and its delay and reverb send mixers hold 8 tracks + 2 external inputs + kSynthParts synth sends
template class AudioMixer<13>;
#endif
