// tracker-ff FEATURE_USB_AUDIO_IN, see UsbAudioIn.h.
#include "UsbAudioIn.h"

#include <cstring>

namespace ff
{
namespace usbaudio
{

namespace
{
constexpr uint32_t kMask = kInRingFrames - 1;

typedef uint32_t __attribute__((may_alias)) frame_t; // one stereo frame, 4 byte aligned (ring_ and the blocks are alignas(4))

// n frames out of the ring starting at frame index from, wrapping. Word loops, no memcpy call: this runs in the audio input
// interrupt (the stack gate counts the whole call chain of that level)
inline __attribute__((always_inline)) void copyOut(const int16_t* ring, int16_t* dst, uint32_t from, uint32_t n)
{
    const frame_t* src = reinterpret_cast<const frame_t*>(ring);
    frame_t* d = reinterpret_cast<frame_t*>(dst);
    for (uint32_t i = 0; i < n; i++) d[i] = src[(from + i) & kMask];
}
} // namespace

void UsbAudioIn::push(const int16_t* frames, uint32_t n)
{
    // runs in the USB interrupt: a leaf (no memcpy call, no helper) so the stack gate's ISR nest stays shallow.
    // One stereo frame is one aligned 32 bit word (ring_ and the BDT buffers are 4 byte aligned).
    if (n == 0 || n > kInMaxPacket) return;
    const uint32_t w = w_;
    if (w - r_ + n > kInRingFrames)
    {
        overflows_++; // the reader is not draining: drop the packet, the reader re-primes on its own
        return;
    }
    const frame_t* src = reinterpret_cast<const frame_t*>(frames);
    frame_t* ring = reinterpret_cast<frame_t*>(ring_);
    for (uint32_t i = 0; i < n; i++) ring[(w + i) & kMask] = src[i];
    packets_++;
    w_ = w + n; // published after the data
}

inline __attribute__((always_inline)) void UsbAudioIn::feedbackStep(uint32_t fill)
{
    // PI on the fill: more frames than kInTarget in the ring, ask for fewer per ms. Proportional kFbKp units per frame of error, the
    // integral (8 fractional bits, one unit per frame of error per block) takes the host vs codec clock offset (about 47 units, the
    // 44,114.8 Hz measured against a Mac).
    const int32_t err = int32_t(kInTarget) - int32_t(fill);
    int32_t i = fbInt_ + err;
    if (i > kFbIntMax) i = kFbIntMax;
    if (i < -kFbIntMax) i = -kFbIntMax;
    fbInt_ = i;
    int32_t fb = int32_t(kFbNominal) + err * kFbKp + (i >> 8);
    if (fb < int32_t(kFbMin)) fb = int32_t(kFbMin);
    if (fb > int32_t(kFbHigh)) fb = int32_t(kFbHigh);
    fb_ = uint32_t(fb);
    if (fbOut_ != nullptr) *fbOut_ = uint32_t(fb);
}

inline __attribute__((always_inline)) bool UsbAudioIn::pullImpl(int16_t* dst)
{
    uint32_t fill = w_ - r_;
    if (!primed_)
    {
        fb_ = kFbNominal;
        if (fbOut_ != nullptr) *fbOut_ = kFbNominal;
        if (fill < kInTarget)
        {
            frame_t* z = reinterpret_cast<frame_t*>(dst);
            for (uint32_t i = 0; i < kInBlockFrames; i++) z[i] = 0;
            return false;
        }
        r_ = w_ - kInTarget; // start from the prime level whatever piled up while nobody read
        fill = kInTarget;
        primed_ = true;
    }
    if (fill < kInBlockFrames - 1)
    {
        // ran dry: what is there, the last frame repeated, and prime again
        underruns_++;
        const uint32_t have = fill;
        if (have) copyOut(ring_, dst, r_, have);
        int16_t l = have ? dst[2 * have - 2] : 0;
        int16_t r = have ? dst[2 * have - 1] : 0;
        for (uint32_t i = have; i < kInBlockFrames; i++)
        {
            dst[2 * i] = l;
            dst[2 * i + 1] = r;
        }
        r_ += have;
        primed_ = false;
        return true;
    }
    feedbackStep(fill);
    uint32_t take = kInBlockFrames;
    if (fill < kInLow)
        take = kInBlockFrames - 1; // stretch: the last frame twice
    else if (fill > kInHigh)
        take = kInBlockFrames + 1; // squeeze: one frame dropped (the last of the 129)
    const uint32_t out = take > kInBlockFrames ? kInBlockFrames : take;
    copyOut(ring_, dst, r_, out);
    if (take < kInBlockFrames)
    {
        dst[2 * (kInBlockFrames - 1)] = dst[2 * (kInBlockFrames - 2)];
        dst[2 * (kInBlockFrames - 1) + 1] = dst[2 * (kInBlockFrames - 2) + 1];
        repeats_++;
    }
    else if (take > kInBlockFrames)
    {
        skips_++;
    }
    r_ += take;
    return true;
}

bool UsbAudioIn::pull(int16_t* dst)
{
    return pullImpl(dst);
}

const int16_t* UsbAudioIn::pullBlock(int16_t* dst)
{
    pullImpl(dst);
    return dst;
}

} // namespace usbaudio
} // namespace ff
