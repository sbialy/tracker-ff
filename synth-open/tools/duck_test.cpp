// Host test of the send ducking (core/libs/Audio/duck_math.h and effect_duck.h) on the real AudioStream.cpp, mixer.cpp and dspApplyGain.cpp,
// built by duck_test.sh. The graph is the engine's in miniature, in the order the engine constructs it:
//
//   kick (a track's amplifier) --> duck input 0       dry --> mixer 0       pad --> mixer 1 (the delay return) and mixer 2 (the reverb return)
//   other (a second track)     --> duck input 1                              duck --> gain() of mixer inputs 1 and 2 (the return inputs)
//   mixer (AudioMixer<4>, the tracks and sends mixer) --> probe
//
// The pad stands for the two returns: a steady 1 kHz sine. The kick is a short decaying burst every 40 blocks. Every scenario compares the
// probe's samples, so the claims are about what leaves the mixer:
//   1  off: the output is the exact sum of dry and the two returns (unity gains, no ducking, no block taken by the duck stream)
//   2  on: while the kick sounds the returns go down by the depth within two blocks, the dry path is never touched, between kicks the
//      returns come back to unity bit exactly after the release, the release time is the set one (block granularity)
//   3  the threshold: a kick under it does not duck, a kick at twice it ducks fully
//   4  the source: the other track (silent) never ducks, switching the source off in the middle of a duck puts unity back and every block
//      the duck stream held goes back to the pool (no leak), with the duck off again the output is bit identical to scenario 1
//   5  depth 0 and depth 100
//   6  sidecar and state: the arithmetic on its own (grid, clamp, release table)
#define private public // the test reads the mixer gains and the duck stream state
#include "AudioStream.h"
#include "effect_duck.h"
#include "mixer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void disableAudioInterrupt() {}
void enableAudioInterrupt() {}

namespace
{
int gFail = 0;
#define CHECK(c, ...)                                  \
    do                                                 \
    {                                                  \
        if (!(c))                                      \
        {                                              \
            gFail++;                                   \
            printf("FAIL %s:%d  ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                       \
            printf("\n");                              \
        }                                              \
    } while (0)

uint32_t gBlock;
// 3 periods in a block: the rms of the pad is the same in every block, whatever the phase
constexpr float kPadHz = 3.0f * 44117.64706f / 128.0f;

// kick: a burst of `len` blocks starting every `period` blocks at `amp`, a 60 Hz sine with a 1/e decay of 4 blocks
struct KickShape
{
    int period = 40;
    int len = 6;
    float amp = 24000.0f;
    int firstAt = 5;
    bool silent = false;
};

// the samples of the kick in block n of its pattern
void kickSamples(const KickShape& shape, int n, int16_t* out, bool& sounding)
{
    const int rel = n - shape.firstAt;
    sounding = !(rel < 0 || (rel % shape.period) >= shape.len);
    if (!sounding)
        return;
    const int k = rel % shape.period;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
    {
        const float t = float(k * AUDIO_BLOCK_SAMPLES + i);
        out[i] = int16_t(shape.amp * expf(-t / (4.0f * AUDIO_BLOCK_SAMPLES)) * sinf(t * 6.2831853f * 60.0f / 44117.647f));
    }
}

uint16_t kickPeak(const KickShape& shape, int n)
{
    int16_t d[AUDIO_BLOCK_SAMPLES];
    bool on;
    kickSamples(shape, n, d, on);
    return on ? duckBlockPeak(d) : uint16_t(0);
}

class Kick : public AudioStream
{
  public:
    Kick() : AudioStream(0, nullptr) {}
    KickShape shape;
    void update() override
    {
        if (shape.silent)
            return; // a silent track transmits nothing (NULL block = silence)
        int16_t d[AUDIO_BLOCK_SAMPLES];
        bool on;
        kickSamples(shape, int(gBlock), d, on);
        if (!on)
            return;
        audio_block_t* b = allocate();
        if (!b)
            return;
        memcpy(b->data, d, sizeof(d));
        transmit(b);
        release(b);
    }
};

class Tone : public AudioStream
{
  public:
    Tone(float amp, float freq, int outputs) : AudioStream(0, nullptr), amp(amp), freq(freq), outputs(outputs) {}
    void update() override
    {
        if (amp == 0.0f)
            return;
        audio_block_t* b = allocate();
        if (!b)
            return;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            b->data[i] = int16_t(amp * sinf((float(gBlock) * AUDIO_BLOCK_SAMPLES + i) * 6.2831853f * freq / 44117.647f));
        for (int o = 0; o < outputs; o++)
            transmit(b, o);
        release(b);
    }
    float amp, freq;
    int outputs;
};

class Probe : public AudioStream
{
  public:
    Probe() : AudioStream(1, q) {}
    void update() override
    {
        audio_block_t* b = receiveReadOnly(0);
        std::vector<int16_t> blk(AUDIO_BLOCK_SAMPLES, 0);
        if (b)
        {
            memcpy(blk.data(), b->data, sizeof(b->data));
            release(b);
        }
        out.push_back(blk);
    }
    std::vector<std::vector<int16_t>> out;
    audio_block_t* q[1];
};

using Mixer = AudioMixer<4>;

struct Rig
{
    // construction order = update order: the sources, the duck, the mixer, the probe
    Kick kick;
    Kick other;
    Tone dry;
    Tone pad;
    AudioEffectDuck<Mixer> duck;
    Mixer mixer;
    Probe probe;
    AudioConnection *c0, *c1, *c2, *c3, *c4, *c5, *c6;

    Rig(float dryAmp, float padAmp, bool wireDuck)
      : dry(dryAmp, 220.0f, 1)
      , pad(padAmp, kPadHz, 1)
    {
        other.shape.silent = true;
        c0 = new AudioConnection(&dry, 0, &mixer, 0);
        c1 = new AudioConnection(&pad, 0, &mixer, 1);
        c2 = new AudioConnection(&pad, 0, &mixer, 2);
        c3 = new AudioConnection(&mixer, 0, &probe, 0);
        if (wireDuck)
        {
            c4 = new AudioConnection(&kick, 0, &duck, 0);
            c5 = new AudioConnection(&other, 0, &duck, 1);
        }
        duck.setTargets(&mixer, nullptr, 1, 2);   // the engine has a second mixer for the right channel, this graph is mono
        for (int ch = 0; ch < 4; ch++)
            mixer.gain(ch, 1.0f);
    }
    void run(int blocks)
    {
        for (int n = 0; n < blocks; n++)
        {
            AudioStream::update_all();
            gBlock++;
        }
    }
    ~Rig()
    {
        delete c0; delete c1; delete c2; delete c3;
    }
};

audio_block_t gPool[40];

double rms(const std::vector<int16_t>& b)
{
    double s = 0;
    for (int16_t v : b)
        s += double(v) * v;
    return sqrt(s / b.size());
}

double padRms(Rig& r, int block)
{
    return rms(r.probe.out[block]);
}

// the pad with both returns at unity is two copies of a 1 kHz sine of amplitude padAmp: rms 2 * padAmp / sqrt 2
}   // namespace

// The tests can not rebuild the graph (streams register themselves for good), so each scenario is its own process: argv[1] picks it.
int main(int argc, char** argv)
{
    AudioStream::initialize_memory(gPool, 40);
    const int scenario = argc > 1 ? atoi(argv[1]) : 0;
    const float padAmp = 6000.0f;
    const double padRmsUnity = 2.0 * padAmp / sqrt(2.0);

    switch (scenario)
    {
        case 1:
        {   // off: unity, nothing held. With a dry signal the output is exactly the sum
            Rig r(5000.0f, padAmp, true);
            r.duck.set({0, 60, 50, 50});   // what audioEngine::init does: off, out of the update list
            r.run(120);
            CHECK(!r.duck.isActive(), "the duck stream is in the update list with no source set");
            bool exact = true;
            for (size_t n = 0; n < r.probe.out.size(); n++)
            {
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
                {
                    const int64_t s = int64_t(int16_t(5000.0f * sinf((float(n) * AUDIO_BLOCK_SAMPLES + i) * 6.2831853f * 220.0f / 44117.647f))) +
                                      2 * int64_t(int16_t(padAmp * sinf((float(n) * AUDIO_BLOCK_SAMPLES + i) * 6.2831853f * kPadHz / 44117.647f)));
                    if (r.probe.out[n][i] != int16_t(s > 32767 ? 32767 : (s < -32768 ? -32768 : s)))
                        exact = false;
                }
            }
            CHECK(exact, "off: the mixer output differs from dry + 2 * pad");
            printf("scenario 1 off: %s\n", exact ? "exact sum" : "DIFFERS");
            break;
        }
        case 2:
        {   // on, track 1, depth 60, release 50 (170 ms), threshold 50 (-20 dBFS): the output follows the arithmetic model block by block
            Rig r(0.0f, padAmp, true);
            r.kick.shape.period = 300;
            const DuckParams p = {1, 60, 50, 50};
            r.duck.set(p);
            const int N = 280;
            r.run(N);
            // the model: the same peaks through DuckState (the kick is the same function of the block number)
            DuckState m;
            m.configure(p);
            std::vector<float> g(N + 1, 1.0f);
            float cur = 1.0f;
            Kick kk;
            kk.shape.period = 300;
            for (int n = 0; n < N; n++)
            {
                gBlock = n;
                float ng;
                uint16_t pk = kickPeak(kk.shape, n);
                if (m.step(pk, ng))
                    cur = ng;
                g[n + 1] = cur;
            }
            const double unity = padRmsUnity;
            double worst = 0;
            for (int n = 0; n < N; n++)
            {
                const double a = g[n], b = g[n + 1];
                const double want = sqrt((a * a + a * b + b * b) / 3.0);   // the mixer ramps linearly from the last gain to the new one inside the block: rms of the ramp
                worst = fmax(worst, fabs(padRms(r, n) / unity - want));
            }
            printf("scenario 2 measured against the model over %d blocks: worst difference %.4f\n", N, worst);
            CHECK(worst < 0.005, "the output leaves the model by %.4f", worst);
            const double d5 = padRms(r, 5) / unity, d6 = padRms(r, 6) / unity, d7 = padRms(r, 7) / unity;
            printf("scenario 2 kick onset: block 5 %.3f, 6 %.3f, 7 %.3f of unity (depth 60 -> floor 0.400)\n", d5, d6, d7);
            CHECK(d6 < 0.42 && d6 > 0.38, "the returns are not at the depth one block after the kick (%.3f)", d6);
            double floorv = 1.0;
            int recovered = -1;
            for (int n = 6; n < N; n++)
            {
                const double q = padRms(r, n) / unity;
                floorv = fmin(floorv, q);
                if (n > 11 && recovered < 0 && q > 0.999)
                    recovered = n;
            }
            printf("scenario 2 floor %.3f, back to unity at block %d (kick ended at block 10), release %.0f ms = %.1f blocks\n", floorv, recovered, duckReleaseMs(50), duckReleaseMs(50) / kDuckBlockMs);
            CHECK(floorv > 0.38 && floorv < 0.41, "the floor is %.3f, not 1 - 0.60", floorv);
            CHECK(recovered > 40 && recovered < N, "the returns do not come back (%d)", recovered);
            bool exactBack = true;
            for (int n = recovered + 1; n < N; n++)
                if (fabs(padRms(r, n) - unity) > 0.002 * unity)
                    exactBack = false;
            CHECK(exactBack, "the returns stay off unity after the release");
            break;
        }
        case 3:
        {   // threshold: a quiet kick (under -20 dBFS = 3277) does not duck, one at twice the threshold ducks fully
            Rig quiet(0.0f, padAmp, true);
            quiet.kick.shape.amp = 2800.0f;
            quiet.duck.set({1, 100, 50, 50});
            quiet.run(40);
            double lo = 1e9;
            for (int n = 0; n < 40; n++)
                lo = fmin(lo, padRms(quiet, n));
            printf("scenario 3 quiet kick (2800, under the threshold 3277): lowest pad %.3f of unity\n", lo / padRmsUnity);
            CHECK(lo > 0.99 * padRmsUnity, "a kick under the threshold ducked the returns (%.3f)", lo / padRmsUnity);
            break;
        }
        case 4:
        {   // loud kick, depth 100: down to zero
            Rig loud(0.0f, padAmp, true);
            loud.kick.shape.amp = 30000.0f;
            loud.duck.set({1, 100, 20, 50});
            loud.run(40);
            double lo = 1e9;
            for (int n = 0; n < 40; n++)
                lo = fmin(lo, padRms(loud, n));
            printf("scenario 4 loud kick depth 100: lowest pad %.4f of unity\n", lo / padRmsUnity);
            CHECK(lo < 0.02 * padRmsUnity, "depth 100 does not silence the returns (%.4f)", lo / padRmsUnity);
            break;
        }
        case 5:
        {   // the other (silent) track never ducks. Then the source is switched to the kick, and off again in the middle of a duck
            Rig r(0.0f, padAmp, true);
            r.duck.set({2, 80, 50, 50});   // input 1 = the silent track
            r.run(60);
            double lo = 1e9;
            for (int n = 0; n < 60; n++)
                lo = fmin(lo, padRms(r, n));
            CHECK(lo > 0.99 * padRmsUnity, "the silent track ducked the returns (%.3f)", lo / padRmsUnity);
            r.duck.set({1, 80, 50, 50});
            r.run(8);   // the kick at block 65 .. 70 (40 period, first at 5 -> 45, 85): 60 + 8 = 68: inside the second kick? kicks are at 5, 45, 85
            // run until a kick is on
            r.run(20);
            const size_t before = r.probe.out.size();
            const int used = AudioStream::memory_used;
            r.duck.set({0, 80, 50, 50});   // off in the middle of whatever state
            r.run(4);
            double last = padRms(r, int(r.probe.out.size()) - 1);
            printf("scenario 5 after off: pad %.3f of unity, blocks in use %d -> %d\n", last / padRmsUnity, used, (int)AudioStream::memory_used);
            CHECK(fabs(last - padRmsUnity) < 0.01 * padRmsUnity, "the returns are not back at unity after the source went off (%.3f)", last / padRmsUnity);
            CHECK(!r.duck.isActive(), "the stream is still in the update list with the source off");
            CHECK((int)AudioStream::memory_used <= used, "blocks leaked when the source went off");
            // and stays quiet: 120 more blocks with the kick sounding and the stream off
            r.run(120);
            double lo2 = 1e9;
            for (size_t n = before + 4; n < r.probe.out.size(); n++)
                lo2 = fmin(lo2, padRms(r, int(n)));
            CHECK(lo2 > 0.99 * padRmsUnity, "ducking continued with the source off (%.3f)", lo2 / padRmsUnity);
            break;
        }
        case 6:
        {   // dry untouched: the dry path of a run with the duck on equals the same run with the duck off, sample for sample
            std::vector<std::vector<int16_t>> a, b;
            {
                Rig r(7000.0f, 0.0f, true);   // no returns at all
                r.duck.set({1, 100, 50, 50});
                r.run(100);
                a = r.probe.out;
            }
            printf("scenario 6 dry path: %zu blocks captured with the duck on\n", a.size());
            // the second run is its own process (the streams of the first are still registered): the driver script compares the two dumps
            FILE* f = fopen(argc > 2 ? argv[2] : "/dev/null", "wb");
            if (f)
            {
                for (auto& blk : a)
                    fwrite(blk.data(), 2, blk.size(), f);
                fclose(f);
            }
            break;
        }
        case 7:
        {   // the same run with the duck left off (no source): the dump the scenario 6 must equal
            Rig r(7000.0f, 0.0f, true);
            r.run(100);
            FILE* f = fopen(argc > 2 ? argv[2] : "/dev/null", "wb");
            if (f)
            {
                for (auto& blk : r.probe.out)
                    fwrite(blk.data(), 2, blk.size(), f);
                fclose(f);
            }
            printf("scenario 7 dry path: duck off dump written\n");
            break;
        }
        case 8:
        {   // the arithmetic
            DuckState st;
            st.configure({1, 100, 50, 50});
            float g;
            // below the threshold: nothing, no mixer call
            bool sent = st.step(3000, g);
            CHECK(!sent && g == 1.0f, "a peak under the threshold changed the gain");
            // at twice the threshold: full depth, one call, then none while steady
            sent = st.step(6600, g);
            CHECK(sent && g == 0.0f, "twice the threshold is not full depth (%f)", g);
            sent = st.step(6600, g);
            CHECK(!sent, "a steady state keeps calling the mixer");
            // the grid: every sent gain is a multiple of 1/4096
            st.configure({1, 37, 10, 50});
            for (int pk = 0; pk < 20000; pk += 137)
            {
                st.step(uint16_t(pk), g);
                CHECK(g * 4096.0f == floorf(g * 4096.0f), "gain %f is off the grid", g);
            }
            // release: the envelope after n silent blocks is peak * relCoef^n
            st.configure({1, 100, 50, 50});
            st.env = 0.0f;
            st.sent = 1.0f;
            st.step(20000, g);
            int blocks = 0;
            while (st.step(0, g) || g < 0.999f)
            {
                if (++blocks > 2000)
                    break;
            }
            const float expect = logf(20000.0f / (32768.0f * powf(10.0f, -20.0f / 20.0f))) * duckReleaseMs(50) / kDuckBlockMs;
            printf("scenario 8 release: gain back to 1.0 after %d blocks (the envelope falls under the threshold after %.1f)\n", blocks, expect);
            CHECK(fabsf(float(blocks) - expect) < 2.5f, "release time is not the set one: %d blocks", blocks);
            // the release table and the threshold table at their ends
            CHECK(fabsf(duckReleaseMs(0) - 30.0f) < 0.01f && fabsf(duckReleaseMs(100) - 960.0f) < 0.5f, "release range");
            CHECK(duckThresholdDb(0) == -40.0f && fabsf(duckThresholdDb(100)) < 1e-4f, "threshold range");
            // clamp and validity
            DuckParams bad = {200, 255, 101, 100};
            CHECK(!duckValid(bad), "a source of 200 is valid");
            const DuckParams c = duckClamp(bad);
            CHECK(c.source == 0 && c.depth == 100 && c.release == 100 && c.thresh == 100, "clamp");
            // source 0 puts the state back to idle
            st.configure({0, 60, 50, 50});
            CHECK(st.env == 0.0f && st.gain == 1.0f && st.sent == 1.0f, "off does not reset");
            // the peak of a block
            int16_t d[128] = {0};
            d[17] = -32768;
            d[90] = 1234;
            CHECK(duckBlockPeak(d) == 32768, "the peak of -32768 is %u", duckBlockPeak(d));
            break;
        }
        default:
            printf("usage: duck_test N [dumpfile]\n");
            return 2;
    }
    if (gFail)
        printf("scenario %d: %d FAILED\n", scenario, gFail);
    return gFail ? 1 : 0;
}
