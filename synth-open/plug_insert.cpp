// The live master insert node of the plugin host (spike), per block path at -O2. See plug_host.h.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "interruptsControl.h"

#include <string.h>

void AudioEffectPlug::attach(const plug_desc* d, void* inst, uint32_t budgetCycles)
{
    DISABLE_AUDIO_INTERRUPTS();
    desc_ = d;
    inst_ = inst;
    budget_ = budgetCycles;
    over_ = 0;
    tripped_ = false;
    ENABLE_AUDIO_INTERRUPTS();
}

void AudioEffectPlug::update(void)
{
    if (!desc_ || tripped_)
    {   // nothing loaded: the blocks pass untouched
        for (unsigned ch = 0; ch < 2; ch++)
        {
            audio_block_t* b = receiveReadOnly(ch);
            if (b)
            {
                transmit(b, ch);
                release(b);
            }
        }
        return;
    }
    audio_block_t* blk[2] = {receiveWritable(0), receiveWritable(1)};
    if (!blk[0] && !blk[1]) return;   // silent bus (a tail plugin would need a "has tail" query, ABI ext)
    for (int ch = 0; ch < 2; ch++)
    {
        if (blk[ch]) continue;
        blk[ch] = allocate();
        if (!blk[ch])
        {
            if (blk[ch ^ 1]) release(blk[ch ^ 1]);
            return;
        }
        memset(blk[ch]->data, 0, sizeof(blk[ch]->data));
    }
    static float buf[2][AUDIO_BLOCK_SAMPLES];
    float* io[2] = {buf[0], buf[1]};
    for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) buf[ch][i] = float(blk[ch]->data[i]) * (1.0f / 32768.0f);
    const uint32_t t0 = *reinterpret_cast<volatile uint32_t*>(0xE0001004);
    desc_->process(inst_, io, 2, AUDIO_BLOCK_SAMPLES);
    last_ = *reinterpret_cast<volatile uint32_t*>(0xE0001004) - t0;
    if (last_ > max_) max_ = last_;
    // watchdog: three blocks in a row over the declared budget and the insert bypasses itself until the next load
    over_ = last_ > budget_ ? uint8_t(over_ + 1) : 0;
    if (over_ >= 3) tripped_ = true;
    for (int ch = 0; ch < 2; ch++)
    {
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            int32_t v = int32_t(buf[ch][i] * 32768.0f);
            blk[ch]->data[i] = int16_t(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        transmit(blk[ch], ch);
        release(blk[ch]);
    }
}

#endif   // FEATURE_PLUG_HOST
