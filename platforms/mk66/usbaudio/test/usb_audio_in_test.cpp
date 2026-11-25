// Host test of the computer -> Tracker ring (FEATURE_USB_AUDIO_IN, UsbAudioIn). Event simulation of 10 s: the host sends 1 ms
// packets of 44..45 frames at its own rate, the codec side pulls 128 frame blocks at 44,117.647 Hz. The stream is a frame counter,
// so every repeated or skipped frame shows up in the output sequence and is compared with the repeats / skips counters.
// Two host models: one that ignores the feedback endpoint (fixed rate, the safety net repeats / skips), and one that follows it
// (asynchronous OUT, UAC 1.0 3.7.2.2): it latches the 10.14 feedback every 32 ms (bRefresh 5) and sends that many frames per ms.
#include "UsbAudioIn.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using ff::usbaudio::UsbAudioIn;

struct Result
{
    uint32_t packets, overflows, underruns, repeats, skips, primedBlocks;
    uint32_t seenRepeat, seenSkip, otherJumps, minFill, maxFill;
    uint32_t fbMin, fbMax;
};

// hostHz: frames per second of a host that ignores the feedback. follow: the host sends what the feedback asks instead, the codec
// runs at codecHz on the host's clock
static Result run(double hostHz, double seconds, double stopAt = -1, double resumeAt = -1, bool follow = false, double codecHz = 44117.647)
{
    static UsbAudioIn in;
    static volatile uint32_t fbWord;
    memset(&in, 0, sizeof in); // zero, as ffUsbAudioStart does
    fbWord = 0;
    in.setFeedbackOut(&fbWord);
    in.restart();
    double fbLatched = ff::usbaudio::kFbNominal / 16384.0;
    uint32_t ms = 0;
    double tHostNext = 0.0, tBlockNext = 0.0;
    double hostAcc = 0.0;
    uint32_t counter = 0; // next frame value the host sends
    int16_t pkt[2 * ff::usbaudio::kInMaxPacket];
    int32_t last = -1;
    Result r{};
    r.minFill = 1u << 30;
    r.fbMin = 1u << 30;
    bool lastWasStream = false;
    while (tHostNext < seconds || tBlockNext < seconds)
    {
        if (tHostNext <= tBlockNext)
        {
            const bool paused = stopAt >= 0 && tHostNext >= stopAt && tHostNext < resumeAt;
            if (follow && ms % 32 == 0) fbLatched = fbWord / 16384.0; // bRefresh 5: a new value every 32 ms
            ms++;
            hostAcc += follow ? fbLatched : hostHz / 1000.0;
            uint32_t n = uint32_t(hostAcc);
            hostAcc -= n;
            if (!paused)
            {
                for (uint32_t i = 0; i < n; i++)
                {
                    pkt[2 * i] = int16_t(counter & 0x7fff);
                    pkt[2 * i + 1] = int16_t(-(int32_t)(counter & 0x7fff));
                    counter++;
                }
                in.push(pkt, n);
            }
            tHostNext += 0.001;
        }
        else
        {
            int16_t blk[256];
            const bool data = in.pull(blk);
            if (in.primed() && data)
            {
                if (r.minFill > in.fill()) r.minFill = in.fill();
                if (r.maxFill < in.fill()) r.maxFill = in.fill();
                if (r.fbMin > fbWord) r.fbMin = fbWord;
                if (r.fbMax < fbWord) r.fbMax = fbWord;
                for (int i = 0; i < 128; i++)
                {
                    int32_t v = blk[2 * i];
                    if (lastWasStream || i > 0)
                    {
                        const int32_t d = (v - last) & 0x7fff;
                        if (d == 0) r.seenRepeat++;
                        else if (d == 2) r.seenSkip++;
                        else if (d != 1) r.otherJumps++;
                    }
                    if (blk[2 * i + 1] != (int16_t)-v) r.otherJumps += 1000; // L and R must stay together
                    last = v;
                }
                lastWasStream = true;
            }
            else
            {
                r.primedBlocks++;
                lastWasStream = false;
            }
            tBlockNext += 128.0 / codecHz;
        }
    }
    r.packets = in.packets();
    r.overflows = in.overflows();
    r.underruns = in.underruns();
    r.repeats = in.repeats();
    r.skips = in.skips();
    return r;
}

static int failures;
#define CHECK(c, ...)                                  \
    do                                                 \
    {                                                  \
        if (!(c))                                      \
        {                                              \
            printf("FAIL  " __VA_ARGS__);              \
            printf("\n");                              \
            failures++;                                \
        }                                              \
        else                                           \
        {                                              \
            printf("ok    " __VA_ARGS__);              \
            printf("\n");                              \
        }                                              \
    } while (0)

int main()
{
    struct Case
    {
        const char* name;
        double hz;
    } cases[] = { { "host 44100 (codec 0.04 % faster)", 44100.0 },
                  { "host 44117.647 (no drift)", 44117.647 },
                  { "host 44150 (host faster, skips)", 44150.0 },
                  { "host 44050 (slow host)", 44050.0 } };
    for (const Case& c : cases)
    {
        const Result r = run(c.hz, 10.0);
        printf("      %s: packets %u overflows %u underruns %u repeats %u skips %u, fill %u..%u, silent blocks %u, stream jumps repeat %u skip %u other %u\n",
               c.name, r.packets, r.overflows, r.underruns, r.repeats, r.skips, r.minFill, r.maxFill, r.primedBlocks, r.seenRepeat, r.seenSkip, r.otherJumps);
        CHECK(r.underruns == 0, "%s: no under-run in 10 s", c.name);
        CHECK(r.overflows == 0, "%s: no overflow in 10 s", c.name);
        CHECK(r.otherJumps == 0, "%s: the stream only repeats or skips single frames, L/R stay together", c.name);
        CHECK(r.seenRepeat == r.repeats && r.seenSkip == r.skips, "%s: output jumps equal the counters (repeat %u / skip %u)", c.name, r.seenRepeat, r.seenSkip);
        CHECK(r.primedBlocks <= 3, "%s: primed after at most 3 silent blocks", c.name);
        // one frame per frame of clock difference and no more: 44117.647 - host Hz frames per second
        const double drift = (c.hz > 44117.647 ? c.hz - 44117.647 : 44117.647 - c.hz) * 10.0;
        CHECK(r.repeats + r.skips <= drift + 8, "%s: corrections track the drift (%u in 10 s, drift %.0f frames)", c.name, r.repeats + r.skips, drift);
    }
    // asynchronous: the host follows the feedback, the codec clock is off by the measured amount (44,114.8 Hz against a Mac) and more
    struct FbCase
    {
        const char* name;
        double codecHz;
        double seconds;
    } fbCases[] = { { "feedback, codec 44114.8 (measured vs a Mac)", 44114.8, 60.0 },
                    { "feedback, codec 44117.647 (nominal)", 44117.647, 10.0 },
                    { "feedback, codec 44100", 44100.0, 10.0 },
                    { "feedback, codec 44150", 44150.0, 10.0 } };
    for (const FbCase& c : fbCases)
    {
        const Result r = run(0.0, c.seconds, -1, -1, true, c.codecHz);
        printf("      %s, %.0f s: packets %u overflows %u underruns %u repeats %u skips %u, fill %u..%u, feedback %u..%u, silent blocks %u, other %u\n",
               c.name, c.seconds, r.packets, r.overflows, r.underruns, r.repeats, r.skips, r.minFill, r.maxFill, r.fbMin, r.fbMax, r.primedBlocks,
               r.otherJumps);
        CHECK(r.underruns == 0 && r.overflows == 0, "%s: no under-run, no overflow", c.name);
        CHECK(r.repeats == 0 && r.skips == 0 && r.seenRepeat == 0 && r.seenSkip == 0 && r.otherJumps == 0,
              "%s: no frame repeated or dropped (was about 15 per second with the adaptive sink at 44,114.8 Hz)", c.name);
        CHECK(r.fbMin >= ff::usbaudio::kFbMin && r.fbMax <= ff::usbaudio::kFbHigh && r.fbMax <= ff::usbaudio::kFbMax,
              "%s: feedback %u..%u inside %u..%u, never above 45 frames per ms (%u)", c.name, r.fbMin, r.fbMax, ff::usbaudio::kFbMin,
              ff::usbaudio::kFbHigh, ff::usbaudio::kFbMax);
        // fill() is read after the block took its 128 frames, the band is checked on the fill before the read
        const uint32_t lo = r.minFill + ff::usbaudio::kInBlockFrames, hi = r.maxFill + ff::usbaudio::kInBlockFrames;
        CHECK(lo >= ff::usbaudio::kInLow && hi <= ff::usbaudio::kInHigh, "%s: fill before the read %u..%u stays inside the safety band %u..%u",
              c.name, lo, hi, ff::usbaudio::kInLow, ff::usbaudio::kInHigh);
    }
    // the host stops for 100 ms (a pause): the reader runs dry once, primes again, nothing else breaks
    const Result s = run(44100.0, 10.0, 4.0, 4.1);
    printf("      host pause 4.0..4.1 s: underruns %u overflows %u silent blocks %u other jumps %u\n", s.underruns, s.overflows, s.primedBlocks, s.otherJumps);
    CHECK(s.underruns == 1 && s.overflows == 0, "pause: exactly one under-run and a re-prime");
    printf("RESULT: %s\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
}
