// Runtime plugin host (spike, FEATURE_PLUG_HOST): loader, host service table, the live master insert node.
// ABI in synth-open/plug/plug_abi.h, design in ~/prj/poly/ref/plugin-architecture.md.
#pragma once

#include "plug/plug_abi.h"
#include <stdint.h>

class FileSystem;

namespace plug
{

enum Err : int32_t
{
    kOk = 0,
    kNoFile,
    kHeader,     // magic / header size
    kAbi,        // abi major, min_host, cpu
    kTooBig,     // image + bss over the slot
    kRead,
    kCrc,
    kRebase,     // a rebase offset or the word it names is outside the image
    kEntry,      // entry returned NULL (the plugin refused this host)
    kDesc,       // descriptor or one of its pointers outside the image, wrong kind, struct too small
};

struct Loaded
{
    plug_file_header hdr;
    uintptr_t base;          // run address of the image
    const plug_desc* desc;
};

constexpr uint32_t kSlotBytes = 8192;   // the SRAM_L code slot
uint8_t* sramSlot();                     // kSlotBytes, SRAM_L (code bus, zero wait), .dmabuffers (NOLOAD, no flash)

const plug_host* hostTable();

// Where the two segments of an image go. text (code and constants) is written at textDst and runs at textRun (the same
// address, a code bus alias of it, or a flash address: commit() then programs it). rw (data and bss) is written and runs at
// rwDst, or right after the text when rwDst is null.
struct Place
{
    uint8_t* textDst = nullptr;
    uint32_t textLen = 0;
    uintptr_t textRun = 0;
    uint8_t* rwDst = nullptr;
    uint32_t rwLen = 0;
    uintptr_t rwRun = 0;
    bool (*commit)(const uint8_t* src, uintptr_t run, uint32_t len) = nullptr;
};
Err load(FileSystem* fs, const char* path, const Place& pl, uint32_t wantKind, Loaded& out);
// Checks a descriptor against an image range (also used for the in-image plugins, range 0 = no range check).
Err checkDesc(const plug_desc* d, uintptr_t base, uint32_t size, uint32_t wantKind);

void syncCode();   // after writing code: DSB, ISB, invalidate the LMEM code cache

// Boot hook (Tracker.cpp, end of init): the bench (FEATURE_PLUG_BENCH, /plugbench.plg) then /plugins/master.plg into the insert.
void boot(FileSystem* fs);
void bench(FileSystem* fs);   // plug_bench.cpp

}   // namespace plug

#include "AudioStream.h"

// Master insert running a loaded PLUG_KIND_MASTER_INSERT plugin, between Dirt and the limiters. Without a plugin, or once the
// watchdog tripped, the blocks pass untouched.
class AudioEffectPlug : public AudioStream
{
  public:
    AudioEffectPlug() : AudioStream(2, inputQueueArray) {}
    virtual void update(void);
    void attach(const plug_desc* d, void* inst, uint32_t budgetCycles);   // main thread, audio interrupt off inside
    uint32_t lastCycles() const { return last_; }
    uint32_t maxCycles() const { return max_; }
    bool tripped() const { return tripped_; }

  private:
    audio_block_t* inputQueueArray[2];
    const plug_desc* desc_ = nullptr;
    void* inst_ = nullptr;
    uint32_t budget_ = 0;
    uint32_t last_ = 0, max_ = 0;
    uint8_t over_ = 0;
    volatile bool tripped_ = false;
};

AudioEffectPlug* plugInsert();   // AudioOutput's node
