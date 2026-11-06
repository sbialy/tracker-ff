// Runtime plugin host, master insert host v1 (FEATURE_PLUG_HOST): loader, host service table, plugin index, the two master
// insert slots, parameter queue, project sidecar, crash marker and key skip. ABI in synth-open/plug/plug_abi.h, design in
// ~/prj/poly/ref/plugin-architecture.md (phase 2).
//
// Slots (decisions 2 and 9): insert 1 runs from the 8 KB SRAM_L slot (text and rw), insert 2 runs its text from the FlexNVM
// data flash at 0x1001E000 (8 KB, the two sectors run 3 proved, programmed at load with HSRUN left around every command) and
// its rw from a fixed 1 KB SRAM buffer. Both sit after the tracks and sends mixers (after Dirt in an FF_DIRT=1 build) and before
// the stock limiters, insert 1 first.
#pragma once

#if defined(FEATURE_PLUG_HOST) && !defined(FEATURE_SYNTH_OPEN)
#error "PLUG_HOST=1 needs SYNTH_OPEN=1: the sidecar hooks live in the open synth paths"
#endif

#include "plug/plug_abi.h"
#include "plug_index.h"
#include "plug_sidecar.h"
#include <stdint.h>

class FileSystem;
class Platform;

namespace plug
{

struct Loaded
{
    plug_file_header hdr;
    uintptr_t base;          // run address of the image
    const plug_desc* desc;
};

constexpr uint32_t kSlotBytes = 8192;   // the SRAM_L code slot (insert 1)
uint8_t* sramSlot();                     // kSlotBytes, SRAM_L (code bus, zero wait), .dmabuffers (NOLOAD, no flash)
constexpr uintptr_t kNvmRun = 0x1001E000u;   // insert 2 text: the last two 4 KB sectors of the 128 KB data flash
constexpr uint32_t kNvmBytes = 8192;

const plug_host* hostTable();

// Where the two segments of an image go. text (code and constants) is written at textDst and runs at textRun (the same
// address, or a flash address: commit() then programs it). rw (data and bss) is written and runs at rwDst, or right after
// the text when rwDst is null.
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

// FlexNVM installer for insert 2 (plug_nvm.cpp): programs len bytes of text at run (inside [kNvmRun, kNvmRun + kNvmBytes)),
// skips every flash command when the flash already holds those bytes. Only ERSSCR and PGM8, only these two sectors.
bool nvmInstall(const uint8_t* src, uintptr_t run, uint32_t len);
uint32_t nvmCommands();   // flash commands issued since boot (0 when every load found its bytes in place)

// ---- the plugin index: /Plugins/Master/*.plg, header and manifest only, nothing executes (IndexEntry: plug_index.h)
int indexCount();
const IndexEntry* indexAt(int i);
int indexFind(uint32_t uid);    // -1: no file with that uid on the card
void rescan();                  // re-read /Plugins/Master (the Inserts page does it on entry)

// ---- the two master insert slots (main thread)
enum class SlotState : uint8_t
{
    kEmpty,      // nothing assigned
    kPending,    // assigned, loads within a moment (debounce, or the boot key window is still open)
    kRunning,
    kMissing,    // the project names a plugin that is not on the card: audio passes, the values are kept and saved back
    kFailed,     // the file is there but did not load (slotError() says why): audio passes, the values are kept
    kSafe,       // safe mode: not loaded this session, the values are kept
    kTripped,    // over its CPU budget three blocks in a row: bypassed until the next load (or Inserts off and on)
    kBypassed,   // loaded, but a built-in Saturation entry is picked: no audio through it, values editable and kept
};
SlotState slotState(int slot);
Err slotError(int slot);
const char* slotName(int slot);      // the plugin name ("" when empty)
int slotIndex(int slot);             // index entry of the assigned plugin, -1 when empty or missing
void assign(int slot, int indexEntry);   // -1 clears. Loads after a short debounce (tick). The UI marks the project changed
uint32_t paramCount(int slot);       // 0 unless running
const plug_param* param(int slot, uint32_t i);
float paramValue(int slot, uint32_t i);
void setParam(int slot, uint32_t i, float v);   // clamped and rounded to the step, queued for the audio thread
uint32_t slotCycles(int slot);       // DWT cycles of the last process() call, 0 when not running
uint32_t slotMaxCycles(int slot);

// Whether the inserts play: the project's saturationType is kSatInserts (the "Inserts" entry of the Saturation list). A built-in
// entry bypasses both slots, they stay loaded and in the sidecar. tick() follows saturationType on its own, the UI calls
// this for an immediate change.
void setInsertsOn(bool on);
bool insertsOn();

// The mixer stage under the Inserts entry (Inserts page, "Mixer" column): kSatHardClip, kSatSoftClip or kSatSoftKnee, kept in
// the sidecar ("mixer" line, Hard Clip without one). The audio engine runs the mixers on it while saturationType is Inserts.
uint8_t mixerStage();
void setMixerStage(uint8_t sat);   // the UI marks the project changed

bool safeMode();
const char* safeReason();            // "" or why: the crash marker named a plugin, or Shift was held at power on

// ---- project sidecar (the file manager calls these, dir is "Workspace" or a project folder)
constexpr const char* kSidecarName = "plugins.txt";
// after a project load from the Workspace: read the sidecar, queue the loads (bypassed ones too), turn a format 1 sidecar's
// Hard Clip into kSatInserts (insertsOnAfterLoad), a built-in Dirt entry of an FF build into Inserts with Dirt (dirtMigrate,
// FF_DIRT=0 builds), then set the engine's saturation (the mixer stage is in the sidecar)
void projectLoaded(const char* dir);
bool projectSave(const char* dir);     // Workspace save: write the sidecar (remove it when no slot is used)
void copySidecar(const char* srcDir, const char* dstDir);   // project open and save: the file follows the project

// ---- boot and main loop (Tracker.cpp)
void boot(Platform* platform);   // end of TrackerImpl::init: the bench (FEATURE_PLUG_BENCH), crash marker, index
void tick();                 // TrackerImpl::update: key window, debounced loads, crash marker removal
void bench(FileSystem* fs);  // plug_bench.cpp

}   // namespace plug

// plug_ui.cpp
bool plugUiShiftHeld();
void plugUiSlotGone(int slot);

#include "AudioStream.h"

// The master insert node: the two slots in a row on one float conversion pair, between Dirt and the limiters. Without a
// running plugin, or once a slot's watchdog tripped, the blocks pass untouched.
class AudioEffectPlug : public AudioStream
{
  public:
    AudioEffectPlug() : AudioStream(2, inputQueueArray) {}
    virtual void update(void);
    // main thread, audio interrupt off inside. desc null detaches.
    void attach(int slot, const plug_desc* d, void* inst, uint32_t budgetCycles);
    // main thread: the latest value per parameter reaches set_param at the next block start
    void queue(int slot, uint32_t index, float value);
    uint32_t lastCycles(int slot) const { return run_[slot].last; }
    uint32_t maxCycles(int slot) const { return run_[slot].max; }
    bool tripped(int slot) const { return run_[slot].tripped; }

  private:
    struct Run
    {
        const plug_desc* desc;
        void* inst;
        uint32_t budget, last, max;
        uint8_t over;
        volatile bool tripped;
        volatile uint32_t dirty;   // one bit per parameter with a queued value
        float pend[plug::kMaxParams];
    };
    audio_block_t* inputQueueArray[2];
    Run run_[plug::kSlots] = {};
};

AudioEffectPlug* plugInsert();   // AudioOutput's node
