// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// src/core/usb/usb_audio_out.h), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::usbaudio, comments shortened, class renamed, kept in SDRAM.
// Compiled only with USB_AUDIO=1 (-DFEATURE_USB_AUDIO).
//
// USB audio, Tracker -> computer: the stereo stream the computer records from the Tracker's USB audio
// device, handed from the audio block (I2S DMA interrupt) to the USB interrupt.
//
// Clock: the IN endpoint is asynchronous, the Tracker sets the pace. The audio block writes 128 frames at
// the codec's 44117.647 Hz, every 1 ms USB frame the interrupt sends 43..45 of them, as many as keep the
// ring about kOutTarget frames full (a PI loop on the low-passed fill).
//
// Threading: buffer() / commit() run in the audio block, packet() in the USB interrupt. One producer, one
// consumer: the producer owns w_, the consumer r_, each published after its data. The producer restarts
// the stream through epoch_ (the consumer flushes when it changes).
#pragma once

#include <cstdint>

namespace ff
{
namespace usbaudio
{

constexpr uint32_t kOutBlockFrames = 128; // kAudioBlockSamples
constexpr uint32_t kOutRingFrames = 512;  // power of two, a multiple of the block
// Mean fill the packets steer to (about 4.4 ms): a block of headroom below, two above.
constexpr uint32_t kOutTarget = 192;
// 44117.647 / 1000 frames per USB frame, 10.14 fixed point, +-1 %.
constexpr uint32_t kOutStepNominal = 722824;
constexpr uint32_t kOutStepSpan = 7228;
constexpr uint32_t kOutMaxPacket = 45; // AUDIO_TX_SIZE / 4
// No packet for this many blocks (about 12 ms): the computer stopped recording.
constexpr uint32_t kOutIdleBlocks = 4;

class UsbAudioOut
{
  public:
    // Audio block: where to write this block's kOutBlockFrames interleaved frames, or nullptr (the computer
    // is not recording, or the ring is full). commit() publishes them once written.
    int16_t* buffer();
    void commit();

    // USB interrupt: the next isochronous IN packet into dst (room for kOutMaxPacket frames), returns its
    // frames. Silence while (re)filling.
    uint32_t packet(int16_t* dst);

    bool streaming() const { return active_; }
    bool playing() const { return playing_; }
    uint32_t fill() const { return w_ - r_; }
    uint32_t step() const { return step_ ? step_ : kOutStepNominal; }

    // Diagnostics: packets sent, under-runs while playing, blocks dropped (ring full).
    uint32_t packets() const { return packets_; }
    uint32_t underruns() const { return underruns_; }
    uint32_t overflows() const { return overflows_; }
    uint32_t blocks() const { return blocks_; }

  private:
    // No constructor: the image keeps it in SDRAM and clears it before first use (usb_audio_hooks.cpp).
    int16_t ring_[2 * kOutRingFrames];
    volatile uint32_t w_;         // frames written, a multiple of the block (producer)
    volatile uint32_t r_;         // frames sent (consumer)
    volatile uint32_t packets_;   // (consumer)
    volatile uint32_t epoch_;     // stream (re)starts (producer)
    volatile uint32_t step_;      // 0 = nominal (consumer)
    volatile uint32_t underruns_; // (consumer)
    uint32_t overflows_;          // (producer)
    uint32_t blocks_;             // blocks written (producer), tracker-ff addition
    uint32_t seenPackets_;        // packets_ at the last block (producer)
    uint32_t idle_;               // blocks without a packet (producer)
    uint32_t seenEpoch_;          // (consumer)
    uint32_t acc_;                // packet size fraction, 10.14 (consumer)
    int32_t fillQ8_;              // low-passed fill after a packet, Q8 frames (consumer)
    int32_t integ_;               // integral term, step LSBs (consumer)
    bool active_;                 // writing blocks (producer)
    volatile bool playing_;       // (consumer)
    bool pending_;                // buffer() handed out a block (producer)
};

} // namespace usbaudio
} // namespace ff
