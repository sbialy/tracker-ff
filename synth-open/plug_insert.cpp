// The master insert node of the plugin host, per block path at -O2. See plug_host.h.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "interruptsControl.h"

#include <string.h>

void AudioEffectPlug::attach(int slot, const plug_desc* d, void* inst, uint32_t budgetCycles)
{
    Run& r = run_[slot];
    DISABLE_AUDIO_INTERRUPTS();
    r.desc = d;
    r.inst = inst;
    r.budget = budgetCycles;
    r.last = r.max = 0;
    r.over = 0;
    r.tripped = false;
    r.dirty = 0;
    ENABLE_AUDIO_INTERRUPTS();
}

void AudioEffectPlug::queue(int slot, uint32_t index, float value)
{
    if (index >= uint32_t(plug::kMaxParams)) return;
    Run& r = run_[slot];
    r.pend[index] = value;   // the value first, then the bit: the audio thread reads the bit, then the value
    __atomic_fetch_or(&r.dirty, 1u << index, __ATOMIC_RELEASE);
}

void AudioEffectPlug::update(void)
{
    bool any = false;
    for (const Run& r : run_) any = any || (r.desc && !r.tripped);
    if (!any)
    {   // nothing running: the blocks pass untouched
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
    if (!blk[0] && !blk[1]) return;   // silent bus: nothing transmitted (PLUG_EXT_TAIL is reserved)
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
    for (Run& r : run_)
    {
        if (!r.desc || r.tripped) continue;
        uint32_t d = __atomic_exchange_n(&r.dirty, 0u, __ATOMIC_ACQUIRE);
        while (d)
        {
            const uint32_t i = uint32_t(__builtin_ctz(d));
            d &= d - 1u;
            if (r.desc->set_param) r.desc->set_param(r.inst, i, r.pend[i]);
        }
        const uint32_t t0 = *reinterpret_cast<volatile uint32_t*>(0xE0001004);   // DWT_CYCCNT
        r.desc->process(r.inst, io, 2, AUDIO_BLOCK_SAMPLES);
        r.last = *reinterpret_cast<volatile uint32_t*>(0xE0001004) - t0;
        if (r.last > r.max) r.max = r.last;
        // watchdog: three blocks in a row over the budget and the slot bypasses itself until the next load
        r.over = r.last > r.budget ? uint8_t(r.over + 1) : 0;
        if (r.over >= 3) r.tripped = true;
    }
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
