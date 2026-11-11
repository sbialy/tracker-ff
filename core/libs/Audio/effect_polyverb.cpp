// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "effect_polyverb.h"
#include "cr_mtb_buffer.h"

#ifdef PLATFORM_IMX
static uint16_t internalRamReverbBuff[16384] __attribute__((section(".noinit")));
#elif defined(FW_REVERB_SRAM) && FW_REVERB_SRAM >= 1
// SYNTH_OPEN images: the line buffer in internal SRAM, not in the 90 MHz SDRAM (no data cache on the MK66). The half rate reverb
// (Polyverb/reverb.h) keeps both line sets back to back in this one buffer, exactly as long as the lines need (26,658 B), the
// second SDRAM buffer (externalRamReverbBuff, .reverb_memory) of the full rate reverb is gone.
static uint16_t internalRamReverbBuff[Reverb::kBufferSamples] __attribute__((section(".noinit")));
#else
static __NOINIT(EXTERNAL_RAM) uint16_t internalRamReverbBuff[16384];
#endif

AudioEffectPolyverb::AudioEffectPolyverb()
  : AudioStream(2, inputQueueArray)
{
    buffer = internalRamReverbBuff;
    reverb.Init(buffer, nullptr); // the second buffer is not used by the half rate reverb

    reverb.SetInputGain(0.1f);
    reverb.SetDiffusion(0.625f);
    setTime(0.7f);
    setDamp(0.2f);
    setAmount(0.8f);
    blockUpdate();
}

AudioEffectPolyverb::~AudioEffectPolyverb()
{
}

#ifdef AUDIO_NULL_IS_SILENCE
static bool isSilentOrMissing(const audio_block_t* block)
{
    if (block == NULL)
        return true;
    const uint32_t* p = (const uint32_t*)block->data;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES / 2; i++)
    {
        if (p[i] != 0)
            return false;
    }
    return true;
}
#endif

AUDIO_CORE_RAM void AudioEffectPolyverb::update(void)
{
    if (isBlockedUpdate)
        return;

    audio_block_t* block_left;
    audio_block_t* block_right;

#ifdef AUDIO_NULL_IS_SILENCE
    // a missing input is silence: the reverb tail keeps ringing (stock took a block of zeros here, and left the other input unread)
    block_left = receiveWritable(0);
    block_right = receiveWritable(1);
    const bool silentIn = isSilentOrMissing(block_left) && isSilentOrMissing(block_right);
    if (silentIn && reverb.IsQuiet())
    {
        // Silent input and a reverb that has decayed below 1 LSB everywhere (the line buffers are zero): nothing to compute, nothing to
        // transmit. The next block with sound starts from a clean reverb. Not bit identical to the stock tail, see synth-open/README.md.
        if (block_left)
            release(block_left);
        if (block_right)
            release(block_right);
        reverb.SkipQuiet(AUDIO_BLOCK_SAMPLES);
        return;
    }
    if (block_left == NULL)
    {
        block_left = allocate();
        if (block_left)
            memset(block_left->data, 0, sizeof(block_left->data));
    }
    if (block_right == NULL)
    {
        block_right = allocate();
        if (block_right)
            memset(block_right->data, 0, sizeof(block_right->data));
    }
    if (block_left == NULL || block_right == NULL)
    {
        if (block_left)
            release(block_left);
        if (block_right)
            release(block_right);
        return;
    }
#else
    block_left = receiveWritable(0);
    if (block_left == NULL)
        return;
    block_right = receiveWritable(1);
    if (block_right == NULL)
    {
        release(block_left);
        return;
    }
#endif

    reverb.Process(block_left->data, block_right->data, AUDIO_BLOCK_SAMPLES);
#ifdef AUDIO_NULL_IS_SILENCE
    if (!silentIn)
        reverb.ClearQuiet();
#endif

    transmit(block_left, 0);
    transmit(block_right, 1);

    release(block_left);
    release(block_right);
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::reset(void)
{
    isBlockedUpdate = true;
    reverb.Reset();
    isBlockedUpdate = false;
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::setActiveThreshold(float threshold)
{
    reverb.SetActiveThreshold(threshold);
}
