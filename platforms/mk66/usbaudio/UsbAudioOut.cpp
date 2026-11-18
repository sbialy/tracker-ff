// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// src/core/usb/usb_audio_out.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::usbaudio, a block counter, comments shortened, frames of 2 or
// 11 channels with a halfword aligned packet copy, the 8+3 mono interleave (tracker-ff).
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
static_assert(kOutMaxPacket * kOutMultiChannels * 2 <= 1023, "an 8+3 packet fits one full speed isochronous transaction");

namespace
{
typedef uint32_t __attribute__((may_alias)) word_t;
typedef int16_t __attribute__((may_alias)) half_t;
}

// n int16 from src to dst, both at least 2-byte aligned, no overlap. Same alignment: memcpy after at most one halfword. Different:
// dst is made 4-byte aligned, then words are read from the aligned address below src and shifted together (one load and one
// store per word). That read starts one halfword before src and ends at most one halfword after src + n: both stay inside the
// ring object (the ring never starts a misaligned run at index 0, and its last halfword is followed by the stream fields).
void copyHalfwords(int16_t* dst, const int16_t* src, uint32_t n)
{
    half_t* d = dst;
    const half_t* s = src;
    if (n == 0) return;
    if (((uintptr_t(d) ^ uintptr_t(s)) & 2) == 0)
    {
        if (uintptr_t(s) & 2)
        {
            *d++ = *s++;
            --n;
        }
        std::memcpy(const_cast<int16_t*>(reinterpret_cast<const int16_t*>(d)), reinterpret_cast<const int16_t*>(s), n * 2);
        return;
    }
    if (uintptr_t(d) & 2)
    {
        *d++ = *s++; // now d is aligned and s is not
        if (--n == 0) return;
    }
    const word_t* sw = reinterpret_cast<const word_t*>(uintptr_t(s) - 2);
    word_t* dw = reinterpret_cast<word_t*>(d);
    uint32_t prev = *sw++;
    uint32_t words = n >> 1;
    for (; words >= 4; words -= 4, sw += 4, dw += 4)
    {
        const uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
        dw[0] = (prev >> 16) | (a << 16);
        dw[1] = (a >> 16) | (b << 16);
        dw[2] = (b >> 16) | (c << 16);
        dw[3] = (c >> 16) | (e << 16);
        prev = e;
    }
    for (; words; --words)
    {
        const uint32_t a = *sw++;
        *dw++ = (prev >> 16) | (a << 16);
        prev = a;
    }
    if (n & 1)
    {
        reinterpret_cast<half_t*>(dw)[0] = int16_t(prev >> 16);
    }
}

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
    return &ring_[channels() * (w & (kOutRingFrames - 1))];
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
    const uint32_t ch = channels();
    const uint32_t fb = 2 * ch; // bytes per frame
    const uint32_t r = r_;
    uint32_t fill = w_ - r;
    if (!playing_)
    {
        // Start once the target plus a block is queued: the next block then lands before the ring runs low.
        if (fill < kOutTarget + kOutBlockFrames)
        {
            std::memset(dst, 0, n * fb);
            return n;
        }
        playing_ = true;
        fillQ8_ = int32_t(fill << 8);
    }
    else if (fill < n)
    {
        underruns_ = underruns_ + 1;
        playing_ = false; // keep what is there and fill up again
        std::memset(dst, 0, n * fb);
        return n;
    }
    const uint32_t at = r & (kOutRingFrames - 1);
    const uint32_t first = n < kOutRingFrames - at ? n : kOutRingFrames - at;
    if (ch & 1)
    {
        // 8+3: 22 B frames, every other packet starts on a 2-byte boundary (memcpy would take its byte loop there)
        copyHalfwords(dst, &ring_[ch * at], first * ch);
        if (first < n)
        {
            copyHalfwords(dst + ch * first, &ring_[0], (n - first) * ch);
        }
    }
    else
    {
        std::memcpy(dst, &ring_[ch * at], first * fb);
        if (first < n)
        {
            std::memcpy(dst + ch * first, &ring_[0], (n - first) * fb);
        }
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

// Two frames at a time. Every source block is read as words, one word holds two frames, so the 22 halfwords of a frame pair are
// 11 words: tracks 0..7 and the three delayed synth channels give the 11 words a[] (frame 0 in the low halfword, frame 1 in the
// high one), w[] re-packs them into the interleaved order (PKHBT / PKHTB) and one 11 word burst (ldm/stm) writes the pair to the
// ring. That is 704 word writes per block where a halfword per sample would be 1,408, the ring is in SDRAM behind the write
// buffer and every access there is a bus transaction (the 4 word and 8 word bursts are the cheap ones, ref/umc-cpu-investigation.md).
// A silent input reads a block of zeros, so the frame is always complete. A synth part swaps through its delay slot: the block of
// kOutSynthDelayBlocks ago goes out, this block's mono (L + R) / 2 takes its place (a silent side is 0, so (x + 0) >> 1 is the old
// single side case and 0 + 0 the old empty case).
namespace
{
const uint32_t s_zero[kOutBlockFrames / 2] = {};
}

#ifdef __arm__
__attribute__((optimize("O2"))) // the image builds this file at -Os: the pair kernel needs the unrolled, scheduled loop
#endif
void interleaveMulti(int16_t* dst, const int16_t* const* in, MultiDelay& dl)
{
    constexpr uint32_t S = kOutMultiChannels;
    static_assert(S == 11, "the pair kernel is written for 11 channels");
    static_assert(kOutBlockFrames % 2 == 0, "two frames per step");
    const uint32_t* tp[kOutTracks];
    for (uint32_t c = 0; c < kOutTracks; c++)
    {
        tp[c] = in[c] != nullptr ? reinterpret_cast<const uint32_t*>(in[c]) : s_zero;
    }
    const uint32_t* lp[kOutSynthParts];
    const uint32_t* rp[kOutSynthParts];
    uint32_t* hp[kOutSynthParts];
    const uint32_t slot = dl.slot;
    dl.slot = slot + 1 < kOutSynthDelayBlocks ? slot + 1 : 0;
    for (uint32_t p = 0; p < kOutSynthParts; p++)
    {
        const int16_t* l = in[kOutTracks + p];
        const int16_t* r = in[kOutTracks + kOutSynthParts + p];
        lp[p] = l != nullptr ? reinterpret_cast<const uint32_t*>(l) : s_zero;
        rp[p] = r != nullptr ? reinterpret_cast<const uint32_t*>(r) : s_zero;
        hp[p] = reinterpret_cast<uint32_t*>(dl.hist[slot][p]);
    }
    uint32_t* d = reinterpret_cast<uint32_t*>(dst);
    for (uint32_t f = 0; f < kOutBlockFrames / 2; f++, d += S)
    {
        uint32_t a[S];
#pragma GCC unroll 8
        for (uint32_t c = 0; c < kOutTracks; c++)
        {
            a[c] = tp[c][f];
        }
#pragma GCC unroll 3
        for (uint32_t p = 0; p < kOutSynthParts; p++)
        {
            a[kOutTracks + p] = hp[p][f];
            const uint32_t x = lp[p][f], y = rp[p][f];
            const uint32_t lo = uint16_t((int32_t(int16_t(x)) + int32_t(int16_t(y))) >> 1);
            const uint32_t hi = uint16_t(((int32_t(x) >> 16) + (int32_t(y) >> 16)) >> 1);
            hp[p][f] = lo | (hi << 16);
        }
        uint32_t w[S];
        w[0] = (a[0] & 0xFFFFu) | (a[1] << 16);
        w[1] = (a[2] & 0xFFFFu) | (a[3] << 16);
        w[2] = (a[4] & 0xFFFFu) | (a[5] << 16);
        w[3] = (a[6] & 0xFFFFu) | (a[7] << 16);
        w[4] = (a[8] & 0xFFFFu) | (a[9] << 16);
        w[5] = (a[10] & 0xFFFFu) | (a[0] & 0xFFFF0000u);
        w[6] = (a[1] >> 16) | (a[2] & 0xFFFF0000u);
        w[7] = (a[3] >> 16) | (a[4] & 0xFFFF0000u);
        w[8] = (a[5] >> 16) | (a[6] & 0xFFFF0000u);
        w[9] = (a[7] >> 16) | (a[8] & 0xFFFF0000u);
        w[10] = (a[9] >> 16) | (a[10] & 0xFFFF0000u);
#ifdef __arm__
        {
            // one burst, 8 + 3 words: the compiler would store them one by one in the order it likes
            const uint32_t* sp = w;
            uint32_t* dp = d;
            asm volatile("ldmia %[s]!, {r2-r9}\n\tstmia %[d]!, {r2-r9}\n\tldmia %[s]!, {r2-r4}\n\tstmia %[d]!, {r2-r4}"
                         : [s] "+r"(sp), [d] "+r"(dp)
                         :
                         : "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "memory");
        }
#else
        for (uint32_t i = 0; i < S; i++)
        {
            d[i] = w[i];
        }
#endif
    }
}

} // namespace usbaudio
} // namespace ff
