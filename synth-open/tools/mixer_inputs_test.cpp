// Host test of the input limit of AudioMixer (core/libs/Audio/mixer.h kMaxInputs), built by mixer_inputs_test.sh with the real AudioStream.cpp,
// mixer.cpp and dspApplyGain.cpp, once as the original (kMaxInputs 13) and once as Tracker+/Mini (PLATFORM_IMX, kMaxInputs 21), with
// AddressSanitizer on: an input table that is too small for the mixer is a stack-buffer-overflow in mix(), which aborts the run.
//   every input of a mixer of the largest size of the platform carries a constant block, the output is the exact sum of the gains
//   a smaller mixer (11 inputs) and the sizes of the IMX graph (18, 21) work the same
//   inputs left out (NULL = silence) and inputs with gain 0 do not disturb the sum
#include "AudioStream.h"
#include "mixer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

void disableAudioInterrupt() {}
void enableAudioInterrupt() {}

namespace
{
int gFail = 0;
#define CHECK(c, ...)                                   \
    do                                                  \
    {                                                   \
        if (!(c))                                       \
        {                                               \
            gFail++;                                    \
            printf("FAIL %s:%d  ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

// every sample of the block is `value`, NULL (no transmit) when `silent`
class Const : public AudioStream
{
  public:
    Const() : AudioStream(0, nullptr) {}
    int16_t value = 0;
    bool silent = false;
    static audio_block_t* take() { return allocate(); }
    void update() override
    {
        if (silent)
            return;
        audio_block_t* b = allocate();
        if (!b)
            return;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            b->data[i] = value;
        transmit(b);
        release(b);
    }
};

class Probe : public AudioStream
{
  public:
    Probe() : AudioStream(1, q) {}
    void update() override
    {
        audio_block_t* b = receiveReadOnly(0);
        got = b != nullptr;
        if (b)
        {
            first = b->data[0];
            uniform = true;
            for (int i = 1; i < AUDIO_BLOCK_SAMPLES; i++)
                uniform = uniform && b->data[i] == b->data[0];
            release(b);
        }
    }
    bool got = false, uniform = false;
    int16_t first = 0;
    audio_block_t* q[1];
};

// gains are binary fractions: the sum is exact
const float kGains[4] = {0.5f, 1.0f, 2.0f, 0.25f};

// One mixer of N inputs, a source on every input, mute[i] -> the input has gain 0, silent[i] -> the source sends nothing
template<int N>
void run(const char* name, const std::vector<bool>& mute, const std::vector<bool>& silent)
{
    static_assert(N <= AudioMixerBase::kMaxInputs);
    audio_block_t* pool = new audio_block_t[N + 8];
    AudioStream::initialize_memory(pool, N + 8);
    // construction order = update order: the sources, the mixer, the probe-> Nothing is destroyed (streams and connections register for good)
    std::vector<Const*> src;
    for (int i = 0; i < N; i++)
        src.push_back(new Const());
    AudioMixer<N>* mixer = new AudioMixer<N>();
    Probe* probe = new Probe();
    long expect = 0;
    for (int i = 0; i < N; i++)
    {
        src[i]->value = int16_t(40 + 8 * i);
        src[i]->silent = silent[i];
        new AudioConnection(src[i], 0, mixer, i);
        const float g = mute[i] ? 0.0f : kGains[i % 4];
        mixer->gain(i, g);
        if (!mute[i] && !silent[i])
            expect += long(src[i]->value * g);
    }
    new AudioConnection(mixer, 0, probe, 0);
    for (int n = 0; n < 4; n++) // the gain ramp of the first block is over from the second on
        AudioStream::update_all();
    CHECK(probe->got, "%s: no output block", name);
    CHECK(probe->uniform && probe->first == expect, "%s: output %d, expected %ld", name, probe->first, expect);
    printf("%-26s output %6d expected %6ld %s\n", name, probe->first, expect, probe->first == expect ? "ok" : "DIFFERS");
    // the pool is whole again: no block leaked by any path of mix()
    int freeBlocks = 0;
    for (audio_block_t* b = nullptr; (b = Const::take()) != nullptr; freeBlocks++)
        ;
    CHECK(freeBlocks == N + 8, "%s: %d of %d pool blocks free (leak)", name, freeBlocks, N + 8);
}
} // namespace

// The graph registers its streams for good (AudioStream keeps them in a list): one scenario per process, argv[1] picks the size
int main(int argc, char** argv)
{
    const int n = argc > 1 ? atoi(argv[1]) : 0;
    auto none = [](int k) { return std::vector<bool>(k, false); };
    switch (n)
    {
        case 11: run<11>("11 inputs", none(11), none(11)); break;
#ifdef PLATFORM_IMX
        case 18: run<18>("18 inputs", none(18), none(18)); break;
        case 21:
        {
            run<21>("21 inputs", none(21), none(21));
            break;
        }
        case 22: // some inputs muted, some silent
        {
            std::vector<bool> mute(21, false), silent(21, false);
            mute[0] = mute[20] = mute[13] = true;
            silent[19] = silent[14] = silent[7] = true;
            run<21>("21 inputs, 3 muted 3 NULL", mute, silent);
            break;
        }
#else
        case 13: run<13>("13 inputs", none(13), none(13)); break;
        case 22: // some inputs muted, some silent
        {
            std::vector<bool> mute(13, false), silent(13, false);
            mute[0] = mute[12] = mute[5] = true;
            silent[11] = silent[6] = silent[3] = true;
            run<13>("13 inputs, 3 muted 3 NULL", mute, silent);
            break;
        }
#endif
        default: printf("unknown scenario %d\n", n); return 2;
    }
    return gFail ? 1 : 0;
}
