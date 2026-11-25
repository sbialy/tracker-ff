// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "effect_polyverb.h"
#include "cr_mtb_buffer.h"

#ifdef PLATFORM_IMX
static uint16_t internalRamReverbBuff[16384] __attribute__((section(".noinit")));
#elif defined(FW_REVERB_SRAM) && FW_REVERB_SRAM >= 1
// SYNTH_OPEN images: the line buffer in internal SRAM, not in the 90 MHz SDRAM (no data cache on the MK66). The half rate reverb
// (Polyverb/reverb.h) keeps both line sets back to back in this one buffer, as short as the wrap allows (27,456 B), the
// second SDRAM buffer (externalRamReverbBuff, .reverb_memory) of the full rate reverb is gone.
static uint16_t internalRamReverbBuff[Reverb::kBufferSamples] __attribute__((section(".noinit")));
#else
static __NOINIT(EXTERNAL_RAM) uint16_t internalRamReverbBuff[16384];
#endif
#ifdef FW_STEREO_SENDS
// Stereo sends: the side input lines (ring 3, 6,272 B) are in the SDRAM, not in the SRAM heap. The 16 KB heap rule and the profile rule
// (new buffers never go to SRAM): the lines are read and written only while a pair is linked, the E and E2 rings stay in SRAM as they
// were, so a project with no pair costs what it did without the flag. Zeroed by Reverb::Reset() like the SRAM rings.
static __NOINIT(EXTERNAL_RAM) uint16_t sideReverbBuff[Reverb::kSideSamples];
#endif

AudioEffectPolyverb::AudioEffectPolyverb()
  : AudioStream(2, inputQueueArray)
{
    buffer = internalRamReverbBuff;
#ifdef FW_STEREO_SENDS
    reverb.Init(buffer, sideReverbBuff); // the second buffer of the full rate reverb is gone, the side lines (ring 3) use that argument
#else
    reverb.Init(buffer, nullptr); // the second buffer is not used by the half rate reverb
#endif

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
    const FxMuteGate::Step muteStep = muteGate.step();
    if (muteStep >= FxMuteGate::kEnterSleep)
    {
        // muted, the tail has rung and faded: a clean reverb for the wake (the SRAM lines zeroed once), then the reverb and its send
        // mixer leave update_all (their queued inputs released)
        if (muteStep == FxMuteGate::kEnterSleep)
            reverb.Reset();
        muteGate.sleep(this);
        return;
    }
    // a missing input is silence: the reverb tail keeps ringing (stock took a block of zeros here, and left the other input unread)
    block_left = receiveWritable(0);
#ifdef FW_STEREO_SENDS
    // port 1 is the side (L - R) / 2 send, port 0 the mid. The right block starts as a copy of the mid, as the second port of the mono
    // feed was (bit identical to it when the side is silent or missing)
    audio_block_t* block_side = receiveReadOnly(1);
    const bool silentIn = isSilentOrMissing(block_left) && isSilentOrMissing(block_side);
    block_right = nullptr;
#else
    block_right = receiveWritable(1);
    const bool silentIn = isSilentOrMissing(block_left) && isSilentOrMissing(block_right);
#endif
    if (silentIn && reverb.IsQuiet())
    {
        // Silent input and a reverb that has decayed below 1 LSB everywhere (the line buffers are zero): nothing to compute, nothing to
        // transmit. The next block with sound starts from a clean reverb. Not bit identical to the stock tail, see synth-open/README.md.
        if (block_left)
            release(block_left);
#ifdef FW_STEREO_SENDS
        if (block_side)
            release(block_side);
#else
        if (block_right)
            release(block_right);
#endif
        reverb.SkipQuiet(AUDIO_BLOCK_SAMPLES);
        return;
    }
    if (block_left == NULL)
    {
        block_left = allocate();
        if (block_left)
            memset(block_left->data, 0, sizeof(block_left->data));
    }
#ifdef FW_STEREO_SENDS
    block_right = allocate();
    if (block_right && block_left)
        memcpy(block_right->data, block_left->data, sizeof(block_right->data));
#else
    if (block_right == NULL)
    {
        block_right = allocate();
        if (block_right)
            memset(block_right->data, 0, sizeof(block_right->data));
    }
#endif
    if (block_left == NULL || block_right == NULL)
    {
        if (block_left)
            release(block_left);
        if (block_right)
            release(block_right);
#ifdef FW_STEREO_SENDS
        if (block_side)
            release(block_side);
#endif
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

#ifdef FW_STEREO_SENDS
    reverb.Process(block_left->data, block_right->data, AUDIO_BLOCK_SAMPLES, isSilentOrMissing(block_side) ? nullptr : block_side->data);
    if (block_side)
        release(block_side);
#else
    reverb.Process(block_left->data, block_right->data, AUDIO_BLOCK_SAMPLES);
#endif
#ifdef AUDIO_NULL_IS_SILENCE
    if (!silentIn)
        reverb.ClearQuiet();
    if (muteStep == FxMuteGate::kFade)
    {
        FxMuteGate::ramp(block_left->data, muteGate.rampFrom(), muteGate.rampTo());
        FxMuteGate::ramp(block_right->data, muteGate.rampFrom(), muteGate.rampTo());
    }
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
