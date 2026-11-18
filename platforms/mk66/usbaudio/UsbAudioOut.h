// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// src/core/usb/usb_audio_out.h), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::usbaudio, comments shortened, class renamed, kept in SDRAM,
// frames of 2 or 11 channels (tracker-ff 8+3 mono: interleaveMulti, copyHalfwords).
// Compiled only with USB_AUDIO=1 (-DFEATURE_USB_AUDIO).
//
// USB audio, Tracker -> computer: the stereo stream the computer records from the Tracker's USB audio
// device, handed from the audio block (I2S DMA interrupt) to the USB interrupt.
//
// tracker-ff: the frame holds 2 (stereo) or kOutMultiChannels (8+3 mono) interleaved channels, setChannels() after the clear.
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
// 8+3 mono: tracks 1-8 dry, pre pan (the voice amplifier), then the synth parts 1-3 as (L + R) / 2 of the post fader outputs
constexpr uint32_t kOutTracks = 8;
constexpr uint32_t kOutSynthParts = 3;
constexpr uint32_t kOutMultiChannels = kOutTracks + kOutSynthParts;
constexpr uint32_t kOutMaxChannels = kOutMultiChannels;
// Inputs of the 8+3 tap: tracks 0..7, synth part left 8..10, synth part right 11..13. A null input is a block of silence.
constexpr uint32_t kOutMultiInputs = kOutTracks + 2 * kOutSynthParts;

// The synth parts reach the Tracker's own mix two blocks after the synth renders them (the panning amplifiers and the tracks
// mixer run before the synth in update_all, ui_shot usbmc sim), the tracks in the same block. The 8+3 synth channels are
// delayed by as much, so the 11 channels line up as the Tracker mixes them.
constexpr uint32_t kOutSynthDelayBlocks = 2;

// The synth delay line: plain bytes, no constructor, cleared before use. In SRAM (usb_audio_hooks.cpp): the block side reads and
// writes 384 words of it per block. Word aligned, the pair kernel reads two frames per word.
struct alignas(4) MultiDelay
{
    int16_t hist[kOutSynthDelayBlocks][kOutSynthParts][kOutBlockFrames];
    uint32_t slot;
};

// n int16 from src to dst (2-byte aligned, no overlap), word copies whatever the two alignments are. May read one halfword before
// src and one after src + n when the two differ in alignment (UsbAudioOut::packet: inside the ring object).
void copyHalfwords(int16_t* dst, const int16_t* src, uint32_t n);

// 8+3 mono: one block (kOutBlockFrames) of the kOutMultiInputs inputs (4-byte aligned) into kOutMultiChannels interleaved channels
// at dst (4-byte aligned).
// Tracks 1-8 as they are, synth part p = (L + R) >> 1 of kOutSynthDelayBlocks blocks ago (through dl).
void interleaveMulti(int16_t* dst, const int16_t* const* in, MultiDelay& dl);
// No packet for this many blocks (about 12 ms): the computer stopped recording.
constexpr uint32_t kOutIdleBlocks = 4;

class UsbAudioOut
{
  public:
    // Audio block: where to write this block's kOutBlockFrames interleaved frames, or nullptr (the computer
    // is not recording, or the ring is full). commit() publishes them once written.
    int16_t* buffer();
    void commit();

    // After the clear, before the first block or packet: 2 or kOutMultiChannels (0, the cleared value, is stereo).
    void setChannels(uint32_t n) { ch_ = n; }
    uint32_t channels() const { return ch_ ? ch_ : 2; }

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
    alignas(4) int16_t ring_[kOutMaxChannels * kOutRingFrames];
    uint32_t ch_;                 // channels per frame, 0 = 2 (set before the stream starts)
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
