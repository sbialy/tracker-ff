// tracker-ff FEATURE_USB_AUDIO_IN (poly-stereo-io step 5). Own code, MIT like the rest of tracker-ff.
//
// USB audio, computer -> Tracker: the stereo 16 bit 44.1 kHz stream the recorder takes as its USB source. The USB interrupt
// writes the packets (43..45 frames per ms at the host's 44,100 Hz) into a ring, the audio block (I2S interrupt, 128 frames at the
// codec's 44,117.6 Hz) reads one block per call.
//
// Clock: the endpoint is ASYNCHRONOUS with an explicit feedback endpoint (UAC 1.0 3.7.2.2, USB 2.0 5.12.4.2): every audio block the
// reader computes how many frames per 1 ms frame the host should send (10.14 fixed point, nominal the codec's 44.117647) from the
// ring fill, a PI loop that steers the fill to kTarget. The USB interrupt hands that word to the host on endpoint 11. A host that
// follows it never makes the ring drift, so no frame is repeated or dropped. The old dead band stays as a safety net only (a host
// that ignores the feedback, a stall): below kInLow a block repeats its last frame (127 frames read), above kInHigh it skips one.
//
// Threading: push() runs in the USB interrupt (the only writer of w_), pull() in the audio block (the only writer of r_).
// Compiled only with FEATURE_USB_AUDIO_IN, the class is trivial (no constructor), the image keeps it in SDRAM and clears it.
#pragma once

#include <cstdint>

namespace ff
{
namespace usbaudio
{

constexpr uint32_t kInBlockFrames = 128;
constexpr uint32_t kInRingFrames = 512; // power of two
constexpr uint32_t kInTarget = 256;     // prime level and the fill the reader steers to (5.8 ms)
constexpr uint32_t kInLow = 160;        // safety net, below: repeat one frame per block (the feedback keeps the fill near kInTarget)
constexpr uint32_t kInHigh = 384;       // safety net, above: skip one frame per block
constexpr uint32_t kInMaxPacket = 45;   // FF_AUDIO_RX_SIZE / 4

// Feedback, 10.14 fixed point frames per 1 ms USB frame (3 bytes on the wire). Nominal: the codec's 44,117.647 Hz.
constexpr uint32_t kFbOne = 1u << 14;                  // one frame per ms
constexpr uint32_t kFbNominal = 722824;                // 44.117647 * 16384
constexpr uint32_t kFbMax = kInMaxPacket * kFbOne;     // 737,280: 45 frames per ms, the most the 180 B wMaxPacketSize carries
constexpr uint32_t kFbMin = kFbNominal - kFbOne / 2;   // 714,632: 0.5 frame per ms (1.1 %) below nominal
constexpr uint32_t kFbHigh = kFbNominal + kFbOne / 2 < kFbMax ? kFbNominal + kFbOne / 2 : kFbMax; // 731,016, under kFbMax
constexpr int32_t kFbKp = 16;                          // feedback units per frame of fill error (P: 1 s time constant)
constexpr int32_t kFbIntMax = int32_t(kFbOne / 4) << 8; // integral clamp, a quarter frame per ms (<< 8 fractional bits)

class UsbAudioIn
{
  public:
    // USB interrupt: n stereo frames (L R interleaved int16) from a packet. A packet that does not fit is dropped whole.
    void push(const int16_t* frames, uint32_t n);

    // Audio block: the next kInBlockFrames frames into dst (kInBlockFrames * 2 int16). Silence until the ring holds kInTarget
    // frames (and again after an under-run), true when dst holds stream data.
    bool pull(int16_t* dst);

    // The same as pull() for the audio input interrupt: returns dst, so the hook that calls it is a tail call (no frame of its own: the
    // stack gate counts the whole chain of that level, with the hook's own 8 B frame the ALCO row of the release set sat 2 B under the 512 B floor)
    const int16_t* pullBlock(int16_t* dst);

    // The recorder starts or stops listening: drop what is in the ring and prime again, the feedback back to nominal.
    void restart()
    {
        r_ = w_;
        primed_ = false;
        fbInt_ = 0;
        if (fbOut_ != nullptr) *fbOut_ = kFbNominal;
    }

    // The word the USB interrupt sends on the feedback endpoint (internal RAM, the USB controller reads it by DMA). Set once at
    // start, nullptr: no feedback written (the host test without a USB side).
    void setFeedbackOut(volatile uint32_t* w) { fbOut_ = w; }
    // The feedback the last block computed (kFbNominal while not primed).
    uint32_t feedback() const { return fb_; }

    uint32_t fill() const { return w_ - r_; }
    // Diagnostics: packets taken, packets dropped (ring full), blocks that ran dry, frames repeated, frames skipped.
    uint32_t packets() const { return packets_; }
    uint32_t overflows() const { return overflows_; }
    uint32_t underruns() const { return underruns_; }
    uint32_t repeats() const { return repeats_; }
    uint32_t skips() const { return skips_; }
    bool primed() const { return primed_; }

  private:
    bool pullImpl(int16_t* dst); // the body of pull() and pullBlock(), inlined into both
    void feedbackStep(uint32_t fill); // the PI step of a primed block, inlined into pullImpl
    alignas(4) int16_t ring_[kInRingFrames * 2];
    volatile uint32_t w_; // frames written (producer)
    volatile uint32_t r_; // frames read (consumer)
    volatile uint32_t packets_;
    volatile uint32_t overflows_;
    uint32_t underruns_;
    uint32_t repeats_;
    uint32_t skips_;
    int32_t fbInt_;              // integral of the fill error, << 8
    uint32_t fb_;                // last feedback value (10.14)
    volatile uint32_t* fbOut_;   // where the USB interrupt reads it
    bool primed_;
};

} // namespace usbaudio
} // namespace ff
