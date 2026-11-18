#include "skipback.h"

#if defined(FEATURE_SKIPBACK)

#include <stdio.h>
#include <string.h>

#ifndef SKIPBACK_TEST
#include "cr_section_macros.h"
#define SKIPBACK_SDRAM __NOINIT(EXTERNAL_RAM)
#else
#define SKIPBACK_SDRAM
#endif

namespace skipback
{
namespace
{
// NOINIT: nothing reads a byte the capture did not write (s_fill counts what was written since the last clear)
SKIPBACK_SDRAM __attribute__((aligned(16))) uint8_t s_ring[kRingBytes];

constexpr uint32_t kMask = kRingBytes - 1;
static_assert((kRingBytes & kMask) == 0, "the ring size must be a power of two");
static_assert(kRingBytes % (kBlockFrames * 4) == 0, "a stereo block must not split at the wrap");

// shared with the audio interrupt: written by the main thread only while s_capture is false
volatile bool s_capture;      // the interrupt writes blocks
volatile uint32_t s_head;     // byte offset of the next block
volatile uint32_t s_fill;     // bytes written since the last clear, up to kRingBytes
volatile uint8_t s_mono;

bool s_on;
uint8_t s_seconds = kMaxSeconds;

// the running save (main thread only)
bool s_saving;
void* s_file;
uint32_t s_pos;        // ring byte offset of the next card write
uint32_t s_left;       // bytes still to write
uint32_t s_frames;     // frames of this save
uint32_t s_savedFrames;
int s_nextNumber = 1;  // the next free number found this session (the first save scans from 1)
char s_name[16];
char s_path[32];

void clearRing()
{
    s_head = 0;
    s_fill = 0;
}

void resume()
{
    clearRing();   // the save time is a gap: the next save starts after it, never across it
    s_capture = s_on;
}

struct WavHeader
{
    char riff[4];
    uint32_t chunkSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t format;
    uint16_t channels;
    uint32_t rate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bits;
    char data[4];
    uint32_t dataSize;
};
static_assert(sizeof(WavHeader) == 44, "the canonical 44 byte WAV header");

void fillHeader(WavHeader& h, uint16_t channels, uint32_t dataBytes)
{
    memcpy(h.riff, "RIFF", 4);
    h.chunkSize = dataBytes + 36;
    memcpy(h.wave, "WAVE", 4);
    memcpy(h.fmt, "fmt ", 4);
    h.fmtSize = 16;
    h.format = 1;
    h.channels = channels;
    h.rate = kRate;
    h.bits = 16;
    h.blockAlign = uint16_t(channels * 2);
    h.byteRate = kRate * h.blockAlign;
    memcpy(h.data, "data", 4);
    h.dataSize = dataBytes;
}

// "/Skipback/Skip_NNN.wav", the first number from s_nextNumber that is not on the card, 0 when all are taken
int freeNumber(const Io* io)
{
    for (int n = s_nextNumber; n <= kMaxFiles; n++)
    {
        snprintf(s_path, sizeof s_path, "%s/Skip_%03d.wav", kDir, n);
        if (!io->exists(s_path)) return n;
    }
    return 0;
}
}   // namespace

void configure(bool on, uint8_t channels, uint8_t seconds)
{
    s_capture = false;   // the interrupt leaves the ring alone from here
    const bool changed = on != s_on || (channels == kMono) != (s_mono != 0);
    s_on = on;
    s_mono = channels == kMono ? 1 : 0;
    s_seconds = seconds < kMinSeconds ? kMinSeconds : (seconds > kMaxSeconds ? kMaxSeconds : seconds);
    if (changed) clearRing();
    s_capture = on && !s_saving;
}

void block(const int16_t* left, const int16_t* right)
{
    if (!s_capture) return;
    const uint32_t head = s_head;
    uint32_t* dst = reinterpret_cast<uint32_t*>(s_ring + head);
    uint32_t bytes;
    if (s_mono)
    {
        // two mono samples per word, (L + R) / 2 so a full scale stereo pair does not clip
        for (uint32_t i = 0; i < kBlockFrames; i += 2)
        {
            const int32_t a = (int32_t(left[i]) + right[i]) >> 1;
            const int32_t b = (int32_t(left[i + 1]) + right[i + 1]) >> 1;
            dst[i >> 1] = uint32_t(uint16_t(a)) | (uint32_t(uint16_t(b)) << 16);
        }
        bytes = kBlockFrames * 2;
    }
    else
    {
        // one word per frame, L in the low half: the little endian WAV frame order, the save copies the ring as it is
        for (uint32_t i = 0; i < kBlockFrames; i++)
        {
            dst[i] = uint32_t(uint16_t(left[i])) | (uint32_t(uint16_t(right[i])) << 16);
        }
        bytes = kBlockFrames * 4;
    }
    s_head = (head + bytes) & kMask;
    const uint32_t fill = s_fill + bytes;
    s_fill = fill > kRingBytes ? kRingBytes : fill;
}

Result trigger(const Io* io)
{
    if (!s_on) return Result::Off;
    if (s_saving) return Result::Busy;
    s_capture = false;   // frozen: the interrupt returns at once until the save ends
    const uint32_t bpf = s_mono ? 2 : 4;
    uint32_t want = uint32_t(s_seconds) * kRate * bpf;
    uint32_t bytes = s_fill < want ? s_fill : want;
    bytes -= bytes % bpf;
    if (bytes == 0)
    {
        s_capture = s_on;
        return Result::Empty;
    }
    if (!io->exists(kDir)) io->mkdir(kDir);
    const int n = freeNumber(io);
    void* f = n > 0 ? io->create(s_path) : nullptr;
    WavHeader h;
    fillHeader(h, s_mono ? 1 : 2, bytes);
    if (f && !io->write(f, &h, sizeof h))
    {
        io->close(f);
        f = nullptr;
    }
    if (!f)
    {
        s_capture = s_on;   // the ring is kept, a second try can still save it
        return Result::NoFile;
    }
    s_nextNumber = n + 1;
    snprintf(s_name, sizeof s_name, "Skip_%03d.wav", n);
    s_file = f;
    s_pos = (s_head - bytes) & kMask;
    s_left = bytes;
    s_frames = bytes / bpf;
    s_saving = true;
    return Result::Started;
}

TickResult tick(const Io* io)
{
    if (!s_saving) return kIdle;
    // up to kChunkBytes, and never across the end of the ring
    uint32_t n = kRingBytes - s_pos;
    if (n > s_left) n = s_left;
    if (n > kChunkBytes) n = kChunkBytes;
    if (!io->write(s_file, s_ring + s_pos, n))
    {
        io->close(s_file);
        s_file = nullptr;
        s_saving = false;
        resume();
        return kFailed;
    }
    s_pos = (s_pos + n) & kMask;
    s_left -= n;
    if (s_left > 0) return kSaving;
    io->close(s_file);
    s_file = nullptr;
    s_saving = false;
    s_savedFrames = s_frames;
    resume();
    return kDone;
}

bool on() { return s_on; }
bool saving() { return s_saving; }
uint8_t channels() { return s_mono ? kMono : kStereo; }
uint32_t bytesPerFrame() { return s_mono ? 2 : 4; }
uint32_t capacityFrames() { return kRingBytes / bytesPerFrame(); }
uint32_t filledFrames() { return s_fill / bytesPerFrame(); }
uint32_t savedFrames() { return s_savedFrames; }
const char* lastName() { return s_name; }
const char* lastPath() { return s_path; }

uint8_t* ringForTest() { return s_ring; }

void resetForTest()
{
    s_capture = false;
    s_on = false;
    s_mono = 0;
    s_seconds = kMaxSeconds;
    s_saving = false;
    s_file = nullptr;
    s_savedFrames = 0;
    s_nextNumber = 1;
    s_name[0] = 0;
    s_path[0] = 0;
    clearRing();
}
}   // namespace skipback

#endif
