// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// tests/usb_audio_out_test.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::usbaudio, a minimal test shim (test_shim.h).
// Host test of the USB audio ring (UsbAudioOut.h) against a simulated computer. Run: ./run.sh
#include "test_shim.h"

#include <initializer_list>

#include "UsbAudioOut.h"


using namespace ff::usbaudio;

namespace {

UsbAudioOut g_out;   // .bss, like on the device

constexpr double kCodecRate = 44117.647;

// The Tracker writes a block every 128 codec frames, at a moment that moves
// with the render time (the engine writes near the end of it); the computer
// takes one packet per 1 ms USB frame, sometimes late (the audio interrupt
// outranks the USB one). Frames carry a counter (L = n, R = ~n) so the test
// can check nothing was lost or repeated.
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

    void block() {
        int16_t* b = out.buffer();
        if (b) {
            for (uint32_t i = 0; i < kOutBlockFrames; ++i, ++sent) {
                b[2 * i] = int16_t(sent);
                b[2 * i + 1] = int16_t(~sent);
            }
            out.commit();
        } else {
            sent = uint16_t(sent + kOutBlockFrames);   // not written: a hole the check sees
        }
    }

    void packet(bool measure) {
        if (!recording) return;
        int16_t pkt[2 * kOutMaxPacket];
        const uint32_t n = out.packet(pkt);
        CHECK(n >= kOutMaxPacket - 2 && n <= kOutMaxPacket);
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
            const uint16_t v = uint16_t(pkt[2 * i]);
            ok = ok && uint16_t(pkt[2 * i + 1]) == uint16_t(~v);
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

void reset() { g_out = UsbAudioOut{}; }

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
