#pragma once

// Skip-back capture (Config > Features > Skip-back): a rolling buffer in the SDRAM keeps the last seconds of the master output (the block
// the codec gets), Shift + Sample Rec writes them to a WAV file in /Skipback. Off: the audio block returns at the first test, nothing is
// written, no file is read or created. The ring is a static SDRAM block (__NOINIT, sized from the free SDRAM of the 1.4.0 image, see
// ref/idea-skipback.md), the image without FEATURE_EXT_HOOKS has none of this.
//
// Threads: block() runs in the audio interrupt, everything else on the main thread. The main thread stops the capture (saving) before it
// reads the ring, so the interrupt never writes what the save reads.

#include <stdint.h>

// FEATURE_NO_SKIPBACK: the 1.4.0 release image leaves Skip-back out (Capture needs the SDRAM, user decision 2026-11-15), the code stays for 1.5
#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(FEATURE_NO_SKIPBACK)
#define FEATURE_SKIPBACK 1
#endif

namespace skipback
{
constexpr uint32_t kRingBytes = 512u * 1024u;   // power of two: the head wraps with a mask, a 512 B or 256 B block never splits
constexpr uint32_t kBlockFrames = 128;           // kAudioBlockSamples
constexpr uint32_t kRate = 44100;                // the WAV header rate, as the stock recorder writes it (the codec runs at 44,117.6 Hz)
constexpr uint32_t kChunkBytes = 8192;           // card write per main loop pass
constexpr uint8_t kMinSeconds = 1;
constexpr uint8_t kMaxSeconds = 6;               // mono holds 5.94 s, stereo 2.97 s: a longer setting saves what the ring holds
constexpr int kMaxFiles = 999;
constexpr const char* kDir = "/Skipback";

enum Channels : uint8_t
{
    kStereo = 0,
    kMono = 1,
};

enum class Result : uint8_t
{
    Started,   // the save runs, tick() writes it
    Off,       // the feature is off
    Busy,      // a save is running
    Empty,     // nothing captured since the switch or the last save
    NoFile,    // no free name or the card refused the file
};

enum TickResult : int
{
    kIdle = 0,
    kSaving = 1,
    kDone = 2,
    kFailed = -1,
};

// The card side, so the test runs on the Mac without a Platform (skipback_test.cpp)
struct Io
{
    void* (*create)(const char* path);                          // new file for writing, null: refused
    bool (*write)(void* f, const void* data, uint32_t n);       // all n bytes or false
    void (*close)(void* f);
    bool (*exists)(const char* path);
    void (*mkdir)(const char* path);
};

void configure(bool on, uint8_t channels, uint8_t seconds);   // main thread: on/off, Stereo/Mono, the saved length. A change clears the ring
void block(const int16_t* left, const int16_t* right);         // audio interrupt: one block of kBlockFrames frames, after the render
Result trigger(const Io* io);                                  // main thread: freeze the ring, create the file, write its header
TickResult tick(const Io* io);                                 // main thread: the next chunk of a running save

bool on();
bool saving();
uint8_t channels();
uint32_t bytesPerFrame();
uint32_t capacityFrames();      // what the ring holds in the current channel mode
uint32_t filledFrames();        // captured since the switch or the last save (up to the capacity)
uint32_t savedFrames();         // the frames of the last save
const char* lastName();         // file name of the last save ("Skip_001.wav"), "" before the first
const char* lastPath();         // its full path

uint8_t* ringForTest();
void resetForTest();
}   // namespace skipback
