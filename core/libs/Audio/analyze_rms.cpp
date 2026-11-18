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

#include "analyze_rms.h"
#include "config.h"
#include "utility/dspinst.h"
#include <cmath>

AUDIO_CORE_RAM void AudioAnalyzeRMS::update(void)
{
    audio_block_t* block = receiveReadOnly();
    const uint8_t left = wanted;
    if (left == 0)
    {
        // nobody reads this meter: no sums
        if (block)
        {
            release(block);
        }
        return;
    }
    wanted = left - 1;
    if (left == 1)
    {
        // asleep from the next block on: the sums so far are stale by the time a reader comes back
        accum = 0;
        count = 0;
        if (block)
        {
            release(block);
        }
        return;
    }
    if (!block)
    {
        count++;
        return;
    }
#if defined(KINETISK)
    uint32_t* p = (uint32_t*)(block->data);
    uint32_t* end = p + AUDIO_BLOCK_SAMPLES / 2;
    int64_t sum = accum;
    do
    {
        uint32_t n1 = *p++;
        uint32_t n2 = *p++;
        uint32_t n3 = *p++;
        uint32_t n4 = *p++;
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n1, n1);
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n2, n2);
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n3, n3);
        sum = multiply_accumulate_16tx16t_add_16bx16b(sum, n4, n4);
    } while (p < end);
    accum = sum;
    count++;
#else
    int16_t* p = block->data;
    int16_t* end = p + AUDIO_BLOCK_SAMPLES;
    int64_t sum = accum;
    do
    {
        int32_t n = *p++;
        sum += n * n;
    } while (p < end);
    accum = sum;
    count++;
#endif
    release(block);
}

AUDIO_CORE_FLASH float AudioAnalyzeRMS::read(void)
{
    wanted = kHold;
    DISABLE_GLOBAL_INTERRUPTS();
    int64_t sum = accum;
    accum = 0;
    uint32_t num = count;
    count = 0;
    ENABLE_GLOBAL_INTERRUPTS();
    float meansq = sum / (num * AUDIO_BLOCK_SAMPLES);
    // TODO: shift down to 32 bits and use sqrt_uint32
    //       but is that really any more efficient?
    return sqrtf(meansq) / 32767.0;
}
