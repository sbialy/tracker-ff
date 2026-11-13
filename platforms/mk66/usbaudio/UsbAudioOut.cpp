// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// src/core/usb/usb_audio_out.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::usbaudio, a block counter, comments shortened.
// Compiled only with USB_AUDIO=1 (-DFEATURE_USB_AUDIO). See UsbAudioOut.h.
#include "UsbAudioOut.h"

#include <cstring>

namespace ff
{
namespace usbaudio
{

static_assert((kOutRingFrames & (kOutRingFrames - 1)) == 0, "ring size is a power of two");
static_assert(kOutRingFrames % kOutBlockFrames == 0, "a block never wraps");
static_assert((kOutStepNominal + kOutStepSpan) >> 14 < kOutMaxPacket, "packets fit the endpoint");

int16_t* UsbAudioOut::buffer()
{
    pending_ = false;
    const uint32_t pk = packets_;
    if (pk == seenPackets_)
    {
        if (idle_ < kOutIdleBlocks && ++idle_ == kOutIdleBlocks)
        {
            active_ = false; // the computer stopped
        }
    }
    else
    {
        idle_ = 0;
        seenPackets_ = pk;
        if (!active_)
        {
            active_ = true;
            epoch_ = epoch_ + 1; // the consumer drops what is left of the last stream
        }
    }
    if (!active_)
    {
        return nullptr;
    }
    const uint32_t w = w_;
    if (kOutRingFrames - (w - r_) < kOutBlockFrames)
    {
        if (idle_ == 0)
        {
            ++overflows_; // (no packet this block: the computer is stopping)
        }
        return nullptr;
    }
    pending_ = true;
    return &ring_[2 * (w & (kOutRingFrames - 1))];
}

void UsbAudioOut::commit()
{
    if (!pending_)
    {
        return;
    }
    pending_ = false;
    ++blocks_;
    w_ = w_ + kOutBlockFrames; // publish after the data
}

uint32_t UsbAudioOut::packet(int16_t* dst)
{
    packets_ = packets_ + 1;
    if (epoch_ != seenEpoch_)
    {
        seenEpoch_ = epoch_;
        r_ = w_;
        playing_ = false;
    }
    acc_ += step();
    uint32_t n = acc_ >> 14;
    acc_ &= 0x3FFF;
    if (n > kOutMaxPacket)
    {
        n = kOutMaxPacket;
    }
    const uint32_t r = r_;
    uint32_t fill = w_ - r;
    if (!playing_)
    {
        // Start once the target plus a block is queued: the next block then lands before the ring runs low.
        if (fill < kOutTarget + kOutBlockFrames)
        {
            std::memset(dst, 0, n * 4);
            return n;
        }
        playing_ = true;
        fillQ8_ = int32_t(fill << 8);
    }
    else if (fill < n)
    {
        underruns_ = underruns_ + 1;
        playing_ = false; // keep what is there and fill up again
        std::memset(dst, 0, n * 4);
        return n;
    }
    const uint32_t at = r & (kOutRingFrames - 1);
    const uint32_t first = n < kOutRingFrames - at ? n : kOutRingFrames - at;
    std::memcpy(dst, &ring_[2 * at], first * 4);
    if (first < n)
    {
        std::memcpy(dst + 2 * first, &ring_[0], (n - first) * 4);
    }
    r_ = r + n; // publish after the copy
    fill -= n;
    // Packet size: a PI loop on the low-passed fill (Q8 frames). The fill seen here is a 128-frame sawtooth
    // (one block every ~2.9 packets), hence the slow filter (~32 packets). Over the target: send more. One
    // frame of error = 16 LSB proportional, the integral learns the codec's clock against the USB frames.
    fillQ8_ += (int32_t(fill << 8) - fillQ8_) >> 5;
    const int32_t errQ8 = fillQ8_ - int32_t(kOutTarget << 8);
    const int32_t span = int32_t(kOutStepSpan);
    integ_ += errQ8 >> 14;
    if (integ_ > span)
    {
        integ_ = span;
    }
    if (integ_ < -span)
    {
        integ_ = -span;
    }
    int32_t d = integ_ + (errQ8 >> 4);
    if (d > span)
    {
        d = span;
    }
    if (d < -span)
    {
        d = -span;
    }
    step_ = uint32_t(int32_t(kOutStepNominal) + d);
    return n;
}

} // namespace usbaudio
} // namespace ff
