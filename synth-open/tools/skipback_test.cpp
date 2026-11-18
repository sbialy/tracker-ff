// Host test of the Skip-back ring and save (core/source/skipback.cpp): the capture, the wrap, the freeze while saving, the WAV bytes, the
// file names and the error paths, against an in-memory card. Built plain and with ASan/UBSan by run_tests.sh:
//   c++ -std=c++20 -DFEATURE_EXT_HOOKS -DSKIPBACK_TEST -I core/source synth-open/tools/skipback_test.cpp core/source/skipback.cpp
#include "skipback.h"

#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace skipback;

namespace
{
int g_fail;
#define CHECK(c)                                                         \
    do                                                                   \
    {                                                                    \
        if (!(c))                                                        \
        {                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);           \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

// ---- the card
std::map<std::string, std::vector<uint8_t>> g_files;
std::map<std::string, bool> g_dirs;
std::string g_open;
bool g_refuseCreate;
int g_failWriteAfter = -1;   // the Nth write fails (0 = the header)
int g_writes;

void* create(const char* p)
{
    if (g_refuseCreate || !g_open.empty()) return nullptr;
    g_files[p].clear();
    g_open = p;
    g_writes = 0;
    return &g_open;
}
bool write(void* f, const void* d, uint32_t n)
{
    if (f != &g_open || g_open.empty()) return false;
    if (g_failWriteAfter >= 0 && g_writes++ >= g_failWriteAfter) return false;
    auto& v = g_files[g_open];
    v.insert(v.end(), static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    return true;
}
void close(void* f)
{
    if (f == &g_open) g_open.clear();
}
bool exists(const char* p) { return g_files.count(p) || g_dirs.count(p); }
void mkdir(const char* p) { g_dirs[p] = true; }
const Io kIo = { create, write, close, exists, mkdir };

void resetCard()
{
    g_files.clear();
    g_dirs.clear();
    g_open.clear();
    g_refuseCreate = false;
    g_failWriteAfter = -1;
}

// ---- the audio: frame f has L = f * 3, R = -f * 5 (16 bit wrap), so every frame and its order are visible in the file
int16_t L(uint32_t f) { return int16_t(uint16_t(f * 3u)); }
int16_t R(uint32_t f) { return int16_t(uint16_t(0u - f * 5u)); }
uint32_t g_frame;

void feed(uint32_t blocks)
{
    int16_t l[kBlockFrames], r[kBlockFrames];
    for (uint32_t b = 0; b < blocks; b++)
    {
        for (uint32_t i = 0; i < kBlockFrames; i++)
        {
            l[i] = L(g_frame + i);
            r[i] = R(g_frame + i);
        }
        block(l, r);
        g_frame += kBlockFrames;
    }
}

TickResult runSave(int maxTicks = 100000)
{
    TickResult t = kSaving;
    for (int i = 0; i < maxTicks && t == kSaving; i++) t = tick(&kIo);
    return t;
}

uint32_t rd32(const std::vector<uint8_t>& v, size_t o) { uint32_t x; memcpy(&x, &v[o], 4); return x; }
uint16_t rd16(const std::vector<uint8_t>& v, size_t o) { uint16_t x; memcpy(&x, &v[o], 2); return x; }

// the file holds exactly the frames [first, first + frames) of the fed audio, in the given channel mode
bool fileIs(const std::string& path, uint16_t ch, uint32_t first, uint32_t frames)
{
    auto it = g_files.find(path);
    if (it == g_files.end()) { printf("    %s not written\n", path.c_str()); return false; }
    const auto& v = it->second;
    const uint32_t bpf = ch * 2u;
    if (v.size() != 44 + size_t(frames) * bpf) { printf("    %s size %zu, want %u\n", path.c_str(), v.size(), 44 + frames * bpf); return false; }
    bool ok = memcmp(&v[0], "RIFF", 4) == 0 && rd32(v, 4) == 36 + frames * bpf && memcmp(&v[8], "WAVEfmt ", 8) == 0 &&
              rd32(v, 16) == 16 && rd16(v, 20) == 1 && rd16(v, 22) == ch && rd32(v, 24) == kRate &&
              rd32(v, 28) == kRate * bpf && rd16(v, 32) == bpf && rd16(v, 34) == 16 && memcmp(&v[36], "data", 4) == 0 &&
              rd32(v, 40) == frames * bpf;
    if (!ok) { printf("    %s header wrong\n", path.c_str()); return false; }
    for (uint32_t k = 0; k < frames; k++)
    {
        const uint32_t f = first + k;
        if (ch == 2)
        {
            if (int16_t(rd16(v, 44 + k * 4)) != L(f) || int16_t(rd16(v, 46 + k * 4)) != R(f))
            {
                printf("    %s frame %u (fed %u) differs\n", path.c_str(), k, f);
                return false;
            }
        }
        else if (int16_t(rd16(v, 44 + k * 2)) != int16_t((int32_t(L(f)) + R(f)) >> 1))
        {
            printf("    %s mono frame %u (fed %u) differs\n", path.c_str(), k, f);
            return false;
        }
    }
    return true;
}

void start(bool on, uint8_t ch, uint8_t secs)
{
    resetForTest();
    resetCard();
    g_frame = 0;
    configure(on, ch, secs);
}
}   // namespace

int main()
{
    const uint32_t stereoCap = kRingBytes / 4, monoCap = kRingBytes / 2;
    const std::string f1 = "/Skipback/Skip_001.wav", f2 = "/Skipback/Skip_002.wav", f3 = "/Skipback/Skip_003.wav";

    // 1 off: the block writes nothing, the shortcut does nothing, the card is not touched
    start(false, kStereo, 6);
    memset(ringForTest(), 0xA5, 4096);
    feed(8);
    CHECK(filledFrames() == 0);
    CHECK(ringForTest()[0] == 0xA5 && ringForTest()[4095] == 0xA5);
    CHECK(trigger(&kIo) == Result::Off);
    CHECK(tick(&kIo) == kIdle);
    CHECK(g_files.empty() && g_dirs.empty());

    // 2 on, Stereo, part of the ring filled: everything captured is saved, the folder is made, Skip_001
    start(true, kStereo, 6);
    feed(100);
    CHECK(filledFrames() == 100 * kBlockFrames);
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(saving());
    CHECK(runSave() == kDone);
    CHECK(g_dirs.count("/Skipback"));
    CHECK(fileIs(f1, 2, 0, 100 * kBlockFrames));
    CHECK(savedFrames() == 100 * kBlockFrames && std::string(lastName()) == "Skip_001.wav" && std::string(lastPath()) == f1);
    // the save cleared the ring: a second press right away has nothing, the next audio is captured again
    CHECK(trigger(&kIo) == Result::Empty);
    feed(10);
    CHECK(filledFrames() == 10 * kBlockFrames);

    // 3 Stereo, the ring wrapped several times: the last 2.97 s (the whole ring) in order, Seconds 6 clamps to what the ring holds
    start(true, kStereo, 6);
    feed(3 * stereoCap / kBlockFrames + 37);
    CHECK(filledFrames() == stereoCap);
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(runSave() == kDone);
    CHECK(fileIs(f1, 2, g_frame - stereoCap, stereoCap));

    // 4 Seconds 1: the last 44,100 frames (not a block multiple), across the wrap
    start(true, kStereo, 1);
    feed(stereoCap / kBlockFrames + 300);
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(runSave() == kDone);
    CHECK(fileIs(f1, 2, g_frame - kRate, kRate));

    // 5 Mono: (L + R) / 2, twice the capacity, Seconds 6 = 5.94 s held, wrapped
    start(true, kMono, 6);
    CHECK(capacityFrames() == monoCap);
    feed(2 * monoCap / kBlockFrames + 5);
    CHECK(filledFrames() == monoCap);
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(runSave() == kDone);
    CHECK(fileIs(f1, 1, g_frame - monoCap, monoCap));
    start(true, kMono, 3);   // Mono 3 s: 132,300 frames
    feed(monoCap / kBlockFrames + 11);
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(runSave() == kDone);
    CHECK(fileIs(f1, 1, g_frame - 3 * kRate, 3 * kRate));

    // 6 the ring is frozen while the save runs: blocks between the chunks are dropped, the file is the moment of the press
    start(true, kStereo, 6);
    feed(stereoCap / kBlockFrames + 3);
    const uint32_t pressed = g_frame;
    CHECK(trigger(&kIo) == Result::Started);
    int ticks = 0;
    for (TickResult t = kSaving; t == kSaving; ticks++)
    {
        feed(2);   // the interrupt keeps coming
        t = tick(&kIo);
        if (t == kDone) break;
    }
    CHECK(ticks == int(kRingBytes / kChunkBytes));
    CHECK(fileIs(f1, 2, pressed - stereoCap, stereoCap));
    CHECK(filledFrames() == 0);   // done: the ring restarted empty
    feed(4);
    CHECK(filledFrames() == 4 * kBlockFrames);
    // a second press while a save runs is refused, the running save is not disturbed
    feed(stereoCap / kBlockFrames);
    const uint32_t pressed2 = g_frame;
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(tick(&kIo) == kSaving);
    CHECK(trigger(&kIo) == Result::Busy);
    CHECK(runSave() == kDone);
    CHECK(fileIs(f2, 2, pressed2 - stereoCap, stereoCap));

    // 7 names: the first free number, then on from it; a full set of 999 refuses the file and keeps the ring
    start(true, kStereo, 6);
    g_files[f1] = {1};
    g_files[f3] = {1};
    feed(10);
    CHECK(trigger(&kIo) == Result::Started && runSave() == kDone);
    CHECK(fileIs(f2, 2, 0, 10 * kBlockFrames));
    feed(10);
    CHECK(trigger(&kIo) == Result::Started && runSave() == kDone);
    CHECK(g_files.count("/Skipback/Skip_004.wav") == 1);
    start(true, kStereo, 6);
    for (int n = 1; n <= kMaxFiles; n++)
    {
        char p[40];
        snprintf(p, sizeof p, "/Skipback/Skip_%03d.wav", n);
        g_files[p] = {1};
    }
    feed(10);
    CHECK(trigger(&kIo) == Result::NoFile);
    CHECK(!saving() && filledFrames() == 10 * kBlockFrames);

    // 8 the card refuses the file: NoFile, capture goes on with what it had, a later press saves it all
    start(true, kStereo, 6);
    feed(10);
    g_refuseCreate = true;
    CHECK(trigger(&kIo) == Result::NoFile);
    CHECK(!saving());
    feed(5);
    CHECK(filledFrames() == 15 * kBlockFrames);
    g_refuseCreate = false;
    CHECK(trigger(&kIo) == Result::Started && runSave() == kDone);
    CHECK(fileIs(f1, 2, 0, 15 * kBlockFrames));

    // 9 the header write fails: NoFile, the file is closed. A data write fails: kFailed, closed, capture restarts
    start(true, kStereo, 6);
    feed(10);
    g_failWriteAfter = 0;
    CHECK(trigger(&kIo) == Result::NoFile);
    CHECK(g_open.empty());
    g_failWriteAfter = 3;
    feed(stereoCap / kBlockFrames);
    CHECK(trigger(&kIo) == Result::Started);
    CHECK(runSave() == kFailed);
    CHECK(g_open.empty() && !saving());
    feed(2);
    CHECK(filledFrames() == 2 * kBlockFrames);

    // 10 a switch clears: Stereo -> Mono drops the stereo audio, Seconds alone keeps it, off -> on starts empty
    start(true, kStereo, 6);
    feed(50);
    configure(true, kStereo, 2);
    CHECK(filledFrames() == 50 * kBlockFrames);
    configure(true, kMono, 2);
    CHECK(filledFrames() == 0);
    feed(4);
    configure(false, kMono, 2);
    feed(4);
    CHECK(trigger(&kIo) == Result::Off);
    configure(true, kMono, 2);
    CHECK(filledFrames() == 0 && trigger(&kIo) == Result::Empty);
    // Seconds out of range clamp to 1..6
    configure(true, kStereo, 0);
    feed(stereoCap / kBlockFrames);
    CHECK(trigger(&kIo) == Result::Started && runSave() == kDone && savedFrames() == kRate);

    // 11 full scale: the mono sum does not wrap, the stereo halves keep their sign
    start(true, kMono, 6);
    {
        int16_t l[kBlockFrames], r[kBlockFrames];
        for (uint32_t i = 0; i < kBlockFrames; i++)
        {
            l[i] = (i & 1) ? -32768 : 32767;
            r[i] = (i & 1) ? -32768 : 32767;
        }
        block(l, r);
    }
    CHECK(trigger(&kIo) == Result::Started && runSave() == kDone);
    {
        const auto& v = g_files[f1];
        CHECK(v.size() == 44 + kBlockFrames * 2);
        CHECK(int16_t(rd16(v, 44)) == 32767 && int16_t(rd16(v, 46)) == -32768);
    }

    printf("%s: %d failed\n", g_fail ? "skipback_test FAIL" : "skipback_test PASS", g_fail);
    return g_fail ? 1 : 0;
}
