// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// tests/usb_audio_out_test.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::usbaudio, a minimal test shim (test_shim.h), tracker-ff 8+3 mono
// (11 channel frames, interleaveMulti).
// Host test of the USB audio ring (UsbAudioOut.h) against a simulated computer. Run: ./run.sh
#include "test_shim.h"

#include <cstring>
#include <initializer_list>

#include "UsbAudioOut.h"


using namespace ff::usbaudio;

namespace {

UsbAudioOut g_out;   // .bss, like on the device

constexpr double kCodecRate = 44117.647;

// The Tracker writes a block every 128 codec frames, at a moment that moves
// with the render time (the engine writes near the end of it); the computer
// takes one packet per 1 ms USB frame, sometimes late (the audio interrupt
// outranks the USB one). Frames carry a counter (channel 0 = n, channel c =
// n ^ (c * 0x1111), stereo R = ~n) so the test can check nothing was lost,
// repeated or shifted between channels.
struct Sim {
    UsbAudioOut& out;
    double hostPpm = 0.0, codecPpm = 0.0;
    double renderMin = 0.3e-3, renderMax = 2.5e-3;   // write moment after the block start
    double lateMax = 0.0;                            // USB interrupt delay
    bool recording = true;
    double tHost = 0.0, tBlock = 0.0;
    uint32_t rng = 1;
    uint16_t sent = 0, expect = 0;
    bool synced = false;
    long packets = 0, silent = 0, gaps = 0, frames = 0;
    uint32_t minFill = ~0u, maxFill = 0;

    explicit Sim(UsbAudioOut& o) : out(o) {}

    double uniform() {
        rng = rng * 1664525u + 1013904223u;
        return double(rng >> 8) / double(1u << 24);
    }

    static uint16_t chan(uint16_t v, uint32_t c, uint32_t ch) {
        return ch == 2 ? (c ? uint16_t(~v) : v) : uint16_t(v ^ (c * 0x1111u));
    }

    void block() {
        const uint32_t ch = out.channels();
        int16_t* b = out.buffer();
        if (b) {
            for (uint32_t i = 0; i < kOutBlockFrames; ++i, ++sent) {
                for (uint32_t c = 0; c < ch; ++c) b[ch * i + c] = int16_t(chan(sent, c, ch));
            }
            out.commit();
        } else {
            sent = uint16_t(sent + kOutBlockFrames);   // not written: a hole the check sees
        }
    }

    void packet(bool measure) {
        if (!recording) return;
        const uint32_t ch = out.channels();
        int16_t pkt[kOutMaxChannels * kOutMaxPacket + 1];
        pkt[ch * kOutMaxPacket] = 0x7A5A;   // guard: a packet never writes past its 45 frames
        const uint32_t n = out.packet(pkt);
        CHECK(n >= kOutMaxPacket - 2 && n <= kOutMaxPacket);
        CHECK(pkt[ch * kOutMaxPacket] == 0x7A5A);
        CHECK(n * ch * 2 <= (ch == 2 ? 180u : 990u));
        ++packets;
        frames += n;
        if (!out.playing()) {
            ++silent;
            synced = false;
            return;
        }
        bool ok = true;
        if (!synced) expect = uint16_t(pkt[0]);
        for (uint32_t i = 0; i < n; ++i, ++expect) {
            const uint16_t v = uint16_t(pkt[ch * i]);
            for (uint32_t c = 1; c < ch; ++c) ok = ok && uint16_t(pkt[ch * i + c]) == chan(v, c, ch);
            if (v != expect) {
                ok = false;
                expect = v;
            }
        }
        if (!ok) ++gaps;
        synced = true;
        if (measure) {
            const uint32_t f = out.fill();
            minFill = f < minFill ? f : minFill;
            maxFill = f > maxFill ? f : maxFill;
        }
    }

    // Runs `seconds`; statistics only after `settle` seconds.
    void run(double seconds, double settle = 0.0) {
        const double frameS = 1e-3 * (1.0 + hostPpm * 1e-6);
        const double blockS = double(kOutBlockFrames) / (kCodecRate * (1.0 + codecPpm * 1e-6));
        const double end = tHost + seconds, from = tHost + settle;
        double nextWrite = tBlock + renderMin + (renderMax - renderMin) * uniform();
        double nextPacket = tHost + lateMax * uniform();
        while (tHost < end || tBlock < end) {
            if (nextPacket <= nextWrite) {
                packet(tHost >= from);
                tHost += frameS;
                nextPacket = tHost + lateMax * uniform();
            } else {
                block();
                tBlock += blockS;
                nextWrite = tBlock + renderMin + (renderMax - renderMin) * uniform();
            }
        }
    }
};

void reset(uint32_t ch = 0) {
    g_out = UsbAudioOut{};   // the device clears it with memset (ffUsbAudioStart)
    if (ch) g_out.setChannels(ch);
}

}  // namespace

TEST(nothing_written_until_the_computer_records) {
    reset();
    for (int i = 0; i < 10; ++i) CHECK(g_out.buffer() == nullptr);
    CHECK(!g_out.streaming());
    int16_t pkt[2 * kOutMaxPacket];
    const uint32_t n = g_out.packet(pkt);
    CHECK(n == 44u);   // nominal size, silence
    CHECK(!g_out.playing());
    CHECK(g_out.buffer() != nullptr);
    CHECK(g_out.streaming());
}

TEST(primes_then_sends_in_order) {
    reset();
    Sim s(g_out);
    s.run(0.2);
    CHECK(g_out.playing());
    CHECK_EQ(s.gaps, 0L);
    // Start latency: three blocks queued before the first frame goes out.
    CHECK(s.silent <= 12);
}

// The codec's 44117.6 Hz against the computer's USB frames, with crystal
// offsets both ways, render-time jitter and late USB interrupts, ten minutes.
TEST(locks_and_holds_ten_minutes) {
    const double ppm[][2] = {{0, 0}, {100, -100}, {-100, 100}, {50, 50}};
    for (const auto& p : ppm) {
        for (double late : {0.0, 0.8e-3}) {
            reset();
            Sim s(g_out);
            s.hostPpm = p[0];
            s.codecPpm = p[1];
            s.lateMax = late;
            s.run(600.0, 5.0);
            CHECK_EQ(g_out.underruns(), 0u);
            CHECK_EQ(g_out.overflows(), 0u);
            CHECK_EQ(s.gaps, 0L);
            CHECK(s.minFill >= 24);
            CHECK(s.maxFill <= kOutRingFrames - kOutBlockFrames);
            // The computer gets the codec's rate, counted in its own USB frames.
            const double rate = double(s.frames) / (double(s.packets) * 1e-3);
            const double want = kCodecRate * (1.0 + p[1] * 1e-6) * (1.0 + p[0] * 1e-6);
            CHECK(rate > want - 1.0 && rate < want + 1.0);
        }
    }
}

TEST(stop_and_restart) {
    reset();
    Sim s(g_out);
    s.run(1.0);
    CHECK(g_out.playing());
    s.recording = false;
    s.run(0.05);
    CHECK(!g_out.streaming());
    CHECK(g_out.buffer() == nullptr);
    CHECK_EQ(g_out.overflows(), 0u);   // the blocks after the stop don't count
    s.recording = true;
    const long silent = s.silent;
    s.run(2.0, 0.5);
    CHECK(g_out.playing());
    CHECK(s.silent - silent <= 12);
    CHECK_EQ(s.gaps, 0L);
    CHECK_EQ(g_out.underruns(), 0u);
}

// The audio blocks pause (a project load) while the computer keeps
// recording: the ring runs dry, the computer gets silence, then the stream
// picks up again in order.
TEST(pause_underruns_and_recovers) {
    reset();
    Sim s(g_out);
    s.run(2.0);
    for (int i = 0; i < 30; ++i) {   // 30 ms of packets, no block
        s.packet(false);
        s.tHost += 1e-3;
    }
    CHECK(g_out.underruns() == 1u);
    s.tBlock = s.tHost;
    s.run(5.0, 1.0);
    CHECK(g_out.playing());
    CHECK(s.gaps <= 1);   // the pause itself
    CHECK_EQ(g_out.underruns(), 1u);
}

// The dev mode Config readout (Platform::usbAudioStreaming) reads streaming(): the same in Stereo and in 8+3 mono. No packet, no
// host. Packets arriving: streaming. Packets stop for kOutIdleBlocks blocks: no host again.
TEST(streaming_flag_is_the_same_in_stereo_and_8plus3) {
    for (uint32_t ch : {2u, uint32_t(kOutMultiChannels)}) {
        reset(ch);
        CHECK(!g_out.streaming());
        for (int i = 0; i < 10; ++i) (void)g_out.buffer();
        CHECK(!g_out.streaming());   // blocks alone do not make it streaming
        int16_t pkt[kOutMaxChannels * kOutMaxPacket];
        (void)g_out.packet(pkt);
        (void)g_out.buffer();
        CHECK(g_out.streaming());
        for (uint32_t i = 0; i < kOutIdleBlocks - 1; ++i) (void)g_out.buffer();
        CHECK(g_out.streaming());    // not yet idle for kOutIdleBlocks blocks
        (void)g_out.buffer();
        CHECK(!g_out.streaming());
        (void)g_out.packet(pkt);
        (void)g_out.buffer();
        CHECK(g_out.streaming());    // the computer records again
    }
}

// ---- tracker-ff 8+3 mono: 11 channel frames, 990 B packets

TEST(multi_silence_packet_is_11_channels) {
    reset(kOutMultiChannels);
    CHECK_EQ(g_out.channels(), 11u);
    int16_t pkt[kOutMaxChannels * kOutMaxPacket];
    for (auto& v : pkt) v = 0x5555;
    const uint32_t n = g_out.packet(pkt);
    CHECK_EQ(n, 44u);
    bool zero = true;
    for (uint32_t i = 0; i < n * 11; ++i) zero = zero && pkt[i] == 0;
    CHECK(zero);
    CHECK(pkt[n * 11] == 0x5555);
}

TEST(multi_locks_and_holds_ten_minutes) {
    const double ppm[][2] = {{0, 0}, {100, -100}, {-100, 100}};
    for (const auto& p : ppm) {
        for (double late : {0.0, 0.8e-3}) {
            reset(kOutMultiChannels);
            Sim s(g_out);
            s.hostPpm = p[0];
            s.codecPpm = p[1];
            s.lateMax = late;
            s.run(600.0, 5.0);
            CHECK_EQ(g_out.underruns(), 0u);
            CHECK_EQ(g_out.overflows(), 0u);
            CHECK_EQ(s.gaps, 0L);
            CHECK(s.minFill >= 24);
            const double rate = double(s.frames) / (double(s.packets) * 1e-3);
            const double want = kCodecRate * (1.0 + p[1] * 1e-6) * (1.0 + p[0] * 1e-6);
            CHECK(rate > want - 1.0 && rate < want + 1.0);
        }
    }
}

TEST(multi_stop_restart_and_pause) {
    reset(kOutMultiChannels);
    Sim s(g_out);
    s.run(1.0);
    s.recording = false;
    s.run(0.05);
    CHECK(!g_out.streaming());
    s.recording = true;
    s.run(2.0, 0.5);
    CHECK(g_out.playing());
    for (int i = 0; i < 30; ++i) {
        s.packet(false);
        s.tHost += 1e-3;
    }
    CHECK_EQ(g_out.underruns(), 1u);
    s.tBlock = s.tHost;
    s.run(5.0, 1.0);
    CHECK(g_out.playing());
    CHECK(s.gaps <= 1);
}

// interleaveMulti: tracks 1-8 as they are, synth part p = (L + R) >> 1 two blocks later, a null input is silence, nothing
// outside the block
void interleave3(int16_t* dst, const int16_t* const* in, MultiDelay& dl, bool* synthSilentBefore) {
    // the same inputs three blocks running: the synth channels carry them from the third block on
    *synthSilentBefore = true;
    for (int b = 0; b < 3; ++b) {
        interleaveMulti(dst, in, dl);
        if (b < 2)
            for (uint32_t f = 0; f < kOutBlockFrames; ++f)
                for (uint32_t p = 0; p < 3; ++p) *synthSilentBefore = *synthSilentBefore && dst[f * 11 + 8 + p] == 0;
    }
}

TEST(multi_interleave_maps_every_input) {
    alignas(4) static int16_t src[kOutMultiInputs][kOutBlockFrames];
    for (uint32_t i = 0; i < kOutMultiInputs; ++i)
        for (uint32_t f = 0; f < kOutBlockFrames; ++f) src[i][f] = int16_t((i + 1) * 1000 + int(f) - (f & 1 ? 32000 : 0));
    src[8][0] = 32767; src[11][0] = 32767;    // part 1, sum overflows int16 before the shift
    src[9][0] = -32768; src[12][0] = -32768;  // part 2, the negative end
    src[10][0] = 3; src[13][0] = -4;          // part 3, -1 >> 1 rounds down to -1
    const int16_t* in[kOutMultiInputs];
    for (uint32_t i = 0; i < kOutMultiInputs; ++i) in[i] = src[i];
    static int16_t dst[kOutMultiChannels * kOutBlockFrames + 4];
    for (auto& v : dst) v = 0x2222;
    static MultiDelay dl;
    std::memset(&dl, 0, sizeof dl);
    bool silent = false;
    interleave3(dst, in, dl, &silent);
    CHECK(silent);   // the first two blocks of a synth part are the (cleared) delay
    bool ok = true;
    for (uint32_t f = 0; f < kOutBlockFrames; ++f) {
        for (uint32_t c = 0; c < 8; ++c) ok = ok && dst[f * 11 + c] == src[c][f];
        for (uint32_t p = 0; p < 3; ++p)
            ok = ok && dst[f * 11 + 8 + p] == int16_t((int32_t(src[8 + p][f]) + int32_t(src[11 + p][f])) >> 1);
    }
    CHECK(ok);
    CHECK_EQ(dst[8], 32767);
    CHECK_EQ(dst[9], -32768);
    CHECK_EQ(dst[10], -1);
    CHECK(dst[kOutMultiChannels * kOutBlockFrames] == 0x2222);
    // silence: idle tracks and parts are nulls, one-sided parts take half of the side that plays
    in[0] = nullptr; in[7] = nullptr; in[8] = nullptr; in[11] = nullptr; in[12] = nullptr;
    interleave3(dst, in, dl, &silent);
    ok = true;
    for (uint32_t f = 0; f < kOutBlockFrames; ++f) {
        ok = ok && dst[f * 11 + 0] == 0 && dst[f * 11 + 7] == 0 && dst[f * 11 + 8] == 0;
        ok = ok && dst[f * 11 + 1] == src[1][f];
        ok = ok && dst[f * 11 + 9] == int16_t(int32_t(src[9][f]) >> 1);    // part 2, right silent
        ok = ok && dst[f * 11 + 10] == int16_t((int32_t(src[10][f]) + int32_t(src[13][f])) >> 1);
    }
    CHECK(ok);
    // a mono engine (L == R) passes bit exact
    for (uint32_t f = 0; f < kOutBlockFrames; ++f) src[11][f] = src[8][f];
    in[8] = src[8]; in[11] = src[11];
    interleave3(dst, in, dl, &silent);
    ok = true;
    for (uint32_t f = 0; f < kOutBlockFrames; ++f) ok = ok && dst[f * 11 + 8] == src[8][f];
    CHECK(ok);
}

// The delay is exactly kOutSynthDelayBlocks: block k of a part comes out with block k + 2 of the tracks
TEST(multi_synth_delay_is_two_blocks) {
    alignas(4) static int16_t trk[kOutBlockFrames], syn[kOutBlockFrames];
    static int16_t dst[kOutMultiChannels * kOutBlockFrames];
    static MultiDelay dl;
    std::memset(&dl, 0, sizeof dl);
    const int16_t* in[kOutMultiInputs] = {};
    in[0] = trk; in[8] = syn; in[11] = syn;
    bool ok = true;
    for (int b = 0; b < 10; ++b) {
        for (uint32_t f = 0; f < kOutBlockFrames; ++f) { trk[f] = int16_t(b * 1000 + f); syn[f] = int16_t(b * 1000 + f); }
        interleaveMulti(dst, in, dl);
        for (uint32_t f = 0; f < kOutBlockFrames; ++f) {
            ok = ok && dst[f * 11] == int16_t(b * 1000 + f);
            ok = ok && dst[f * 11 + 8] == (b < 2 ? 0 : int16_t((b - 2) * 1000 + f));
        }
    }
    CHECK(ok);
}

// A block written through buffer() with interleaveMulti comes out of packet() in frame order, all 11 channels
TEST(multi_block_to_packets) {
    reset(kOutMultiChannels);
    int16_t pkt[kOutMaxChannels * kOutMaxPacket];
    g_out.packet(pkt);   // the computer records
    g_out.buffer();      // the stream starts (a new epoch, not written)
    g_out.packet(pkt);   // the consumer takes the epoch: what is written from here on goes out
    alignas(4) static int16_t src[kOutMultiInputs][kOutBlockFrames];
    const int16_t* in[kOutMultiInputs];
    static MultiDelay dl;
    std::memset(&dl, 0, sizeof dl);
    uint32_t base = 0;
    for (int b = 0; b < 3; ++b) {   // fill to the start threshold (kOutTarget + a block)
        for (uint32_t i = 0; i < kOutMultiInputs; ++i) {
            for (uint32_t f = 0; f < kOutBlockFrames; ++f) src[i][f] = int16_t((base + f) * 16 + i);
            in[i] = i < 8 ? src[i] : nullptr;   // the synth parts are silent here (their delay has its own test)
        }
        int16_t* d = g_out.buffer();
        CHECK(d != nullptr);
        if (!d) return;
        interleaveMulti(d, in, dl);
        g_out.commit();
        base += kOutBlockFrames;
    }
    uint32_t got = 0;
    bool ok = true;
    for (int guard = 0; got < 2 * kOutBlockFrames && guard < 100; ++guard) {
        const uint32_t n = g_out.packet(pkt);
        if (!g_out.playing()) continue;
        for (uint32_t i = 0; i < n; ++i, ++got)
            for (uint32_t c = 0; c < 11; ++c) ok = ok && pkt[i * 11 + c] == (c < 8 ? int16_t(got * 16 + c) : 0);
    }
    CHECK(ok);
    CHECK(got >= 2 * kOutBlockFrames);
    CHECK_EQ(g_out.underruns(), 0u);
}

// copyHalfwords: every alignment pair, every length up to 3 packets, guards around the destination untouched
TEST(copy_halfwords_any_alignment) {
    alignas(4) static int16_t src[1200], dst[1200];
    for (uint32_t i = 0; i < 1200; ++i) src[i] = int16_t(i * 7 + 3);
    bool ok = true;
    for (uint32_t so = 1; so < 3; ++so)
        for (uint32_t d0 = 2; d0 < 4; ++d0)
            for (uint32_t n = 0; n < 1100; n += (n < 40 ? 1 : 37)) {
                for (auto& v : dst) v = 0x5A5A;
                copyHalfwords(dst + d0, src + so, n);
                for (uint32_t i = 0; i < n; ++i) ok = ok && dst[d0 + i] == src[so + i];
                ok = ok && dst[d0 - 1] == 0x5A5A && dst[d0 + n] == 0x5A5A;
            }
    CHECK(ok);
}
