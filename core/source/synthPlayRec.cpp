#include "synthPlayRec.h"

#if defined(FEATURE_SYNTH_REC) || defined(SYNTHPLAYREC_TEST)

namespace synthplayrec
{
namespace
{
// shared with the audio interrupt: written by the main thread only while s_state is kIdle
int16_t* volatile s_dst;
volatile uint32_t s_head;     // frames written
volatile uint32_t s_cap;      // frames the take may hold, a multiple of kBlockFrames
volatile State s_state;
}   // namespace

void begin(int16_t* dst, uint32_t capacityFrames)
{
    s_state = kIdle;
    s_dst = dst;
    s_head = 0;
    s_cap = capacityFrames - capacityFrames % kBlockFrames;
    if (s_cap >= kBlockFrames)
    {
        s_state = kRecording;
    }
}

void block(const int16_t* left, const int16_t* right)
{
    if (s_state != kRecording)
    {
        return;
    }
    const uint32_t head = s_head;
    int16_t* dst = s_dst + head;
    // halfword stores: the take starts where the packed samples end, which is any even byte address
    for (uint32_t i = 0; i < kBlockFrames; i++)
    {
        dst[i] = int16_t((int32_t(left[i]) + right[i]) >> 1);
    }
    s_head = head + kBlockFrames;
    if (head + 2 * kBlockFrames > s_cap)
    {
        s_state = kFull;
    }
}

State state()
{
    return s_state;
}

uint32_t frames()
{
    return s_head;
}

uint32_t capacity()
{
    return s_cap;
}

uint32_t end()
{
    s_state = kIdle;
    return s_head;
}

uint32_t trimSilence(const int16_t* take, uint32_t n)
{
    while (n > 0)
    {
        const int32_t v = take[n - 1];
        if (v > kSilence || v < -kSilence)
        {
            break;
        }
        n--;
    }
    return n;
}
}   // namespace synthplayrec

#endif
