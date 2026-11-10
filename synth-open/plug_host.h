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
class DisplayDriver;
namespace polysynth::open
{
struct EngineDef;
}

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
    uintptr_t rwRun = 0;   // 0: rwDst (or right after the text run address when rwDst is null)
    bool (*commit)(const uint8_t* src, uintptr_t run, uint32_t len) = nullptr;
    bool run = true;       // false: stop after the rebase (the synth bank writes text and rw to the flash itself, no entry call)
    uint32_t wantUid = 0;  // not 0: the header must carry this uid and crc32 (what the index listed), else kStale before any copy
    uint32_t wantCrc = 0;
};
Err load(FileSystem* fs, const char* path, const Place& pl, uint32_t wantKind, Loaded& out);
// Checks a descriptor against an image range (also used for the in-image plugins, range 0 = no range check).
Err checkDesc(const plug_desc* d, uintptr_t base, uint32_t size, uint32_t wantKind);

void syncCode();   // after writing code: DSB, ISB, invalidate the LMEM code cache

// FlexNVM installer for insert 2 (plug_nvm.cpp): programs len bytes of text at run (inside [kNvmRun, kNvmRun + kNvmBytes)),
// skips every flash command when the flash already holds those bytes. Only ERSSCR and PGM8, only these two sectors.
bool nvmInstall(const uint8_t* src, uintptr_t run, uint32_t len);
uint32_t nvmCommands();   // flash commands issued since boot (0 when every load found its bytes in place)
uint64_t nvmCommandCycles();   // DWT cycles spent inside them (the core runs at 90 MHz there: HSRUN is left for each command)
// One command on the synth bank (plug_bank.h, [0x10000000, 0x1001E000)): ERSSCR of the sector at bank offset off, PGM8 of the
// phrase at off. The caches are invalidated after it, so the flash reads back what it holds.
bool nvmErase(uint32_t off);
bool nvmProgram(uint32_t off, const uint8_t* data8);
// How the erases went (install-crackle): before an erase the audio interrupt renders ahead (output_i2s.cpp). leadOk: erases
// that started with the full lead, leadShort: the lead did not build in 300 ms (erased anyway), noAudio: no audio block came,
// leadMin: the smallest lead an erase started with (blocks), maskMaxCyc: the longest stretch with the interrupts off (DWT,
// 90 MHz inside a command)
struct NvmStats
{
    uint32_t erases, leadOk, leadShort, noAudio, leadMin, maskMaxCyc;
};
const NvmStats& nvmStats();
void nvmStatsReset();
void nvmLegacy(bool on);
void nvmSliceUs(uint32_t us);   // fixed erase slice in us (0: from the last audio block), /Plugins/nvm-slice.txt   // the 1.2.0 way: every command in one window, MCLK not kept (/Plugins/nvm-legacy, A/B tests only)

// ---- synth engine plugins (plug_synth.cpp, phase 3): /Plugins/Synth/*.plg is the library, every engine in it is in the
// shim's engine list from boot (TrackerImpl::init, before Synth::init), installed or not. The FlexNVM bank (plug_bank.h) is a
// cache: an engine is installed when a part needs it (synthEnsure), the least recently used ones that no part needs make room.
// The library holds every synth .plg the scan of /Plugins/Synth keeps (scanIndex, one per uid). A listed engine needs its own stored
// number (open_engine.h kPlugStoredFirst..kPlugStoredLast, 7..255, one byte in a project), so at most 249 are listed (open_cold.cpp
// kPlugMax), the rest of the 256 are files that cannot be (no numbers, numbers taken). Nothing else is sized by the engine count: the
// tables of an entry are in the SDRAM (plug_synth.cpp and open_cold.cpp, about 0.36 KB each), the logos and the flash bank are caches, the work arena
// is shared by the engines in use.
constexpr int kLibMax = 256;
void synthBoot(FileSystem* fs);
// Main thread. Makes the plugin engines of the types in want[] installed and attached (built-in types are skipped). The engines
// of want[] are never evicted for each other, the ones of keep[] (what the parts play now) only when nothing else frees room.
// An install shows "Loading <name>" (plugUiSynthLoading) and takes about 0.1 to 1 s. False: at least one could not be loaded
// (no room, file gone, flash error, safe mode): its parts stay silent and show "Missing: <name>", /Plugins/synth.txt says why.
bool synthEnsure(const int* want, int nWant, const int* keep, int nKeep);
bool synthLoaded(int typeId);       // a library engine that is attached now (false for built-in and unknown types)
bool synthIsPlugin(int typeId);     // a type of the library
bool synthUiUp();                   // the display is up (synthLogos ran): an install may show its popup
// The display is up: the list images of the first 32 listed engines go into the RAM_G logo cache (plug_logo.h). boot() calls it once
void synthLogos(DisplayDriver* dd);
// Main loop. The bitmap id of a plugin engine's logo (the list image, or the editor image), -1: none, show its name. The editor image
// is loaded now when it is not in RAM_G (a card read of a few ms), a list image comes with synthLogoPrefetch (bitmapAt gives nullptr
// until then, the list draws the name)
int synthLogoBitmap(const polysynth::open::EngineDef* d, bool editor);
// Main loop, before the picker is drawn at a new place: the list images of these engines (at most 16, the rows around the selection)
// are put into RAM_G, the ones already there first, so no slot that is wanted or on screen is reused
void synthLogoPrefetch(const polysynth::open::EngineDef* const* defs, int n);
void useFs(FileSystem* fs);   // the card for writeText before boot() (synthBoot runs first)
// scanIndex of one directory and kind on the card (the names are copied into index, the list buffers are shared)
int scanDir(const char* dir, IndexEntry* index, int cap, ScanStats& st, uint32_t kind);
bool writeText(const char* path, const char* text, int n);   // <path>.tmp, read back, replaceFileOps: the old file is kept until then
int readText(const char* path, char* buf, int cap);          // the whole file after moving back <path>.bak, -1: none or a card error
constexpr const char* kMarker = "/Plugins/.loading";      // crash marker: a plugin was starting (section 7 of the design)

// ---- the plugin index: /Plugins/Master/*.plg, header and manifest only, nothing executes (IndexEntry: plug_index.h)
int indexCount();
const IndexEntry* indexAt(int i);
int indexFind(uint32_t uid);    // -1: no file with that uid on the card
void rescan();                  // re-read /Plugins/Master when it changed (the Inserts page asks on entry, plug_index.h ScanCache)
void cardChanged();             // a USB storage session or a card change: the next rescan() reads every file again

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
void projectLoaded(const char* dir, bool newProject = false);   // newProject: an empty Workspace, not an open
// Workspace save: write the sidecar (remove it when no slot is used). False: the settings are not on the card (the save failed)
bool projectSave(const char* dir);
// Project open (before projectLoaded) and save: the file follows the project. False: the card failed. A failed open copy makes
// the next projectLoaded start empty and keeps the project's file (no save writes over it, plug_sidecar_io.h kStale).
bool sidecarOpen(const char* projectDir, const char* workspaceDir);
bool sidecarToProject(const char* workspaceDir, const char* projectDir);

// ---- boot and main loop (Tracker.cpp)
void boot(Platform* platform);   // end of TrackerImpl::init: the bench (FEATURE_PLUG_BENCH), crash marker, index
void tick();                 // TrackerImpl::update: key window, debounced loads, crash marker removal
void bench(FileSystem* fs);  // plug_bench.cpp

}   // namespace plug

// plug_ui.cpp
bool plugUiShiftHeld();
void plugUiSlotGone(int slot);
void plugUiSynthLoading(const char* name);
void synthModelTick();   // SynthEditor.cpp: installs and picks the engine the model list rests on (main loop)   // "Loading <name>" popup, drawn now (the install that follows blocks the main loop)

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
