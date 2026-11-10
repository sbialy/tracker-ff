// Open PolySynth shim for MK66 - synth voice budget and governor interface.
// Plain C++ with no PolySynth or audio library includes, so core/source/Profiler.cpp and the
// exporter can call it. Implemented in open_synth.cpp (OpenSynth owns the pool and the governor).
#pragma once

#include <cstdint>

#include "open_slotnames.h"

namespace polysynth
{
class Patch;
class PatchStore;
} // namespace polysynth

namespace polysynth::open
{

// Audio interrupt, once per live block, after the whole block rendered: `cycles` is the CPU
// cycle count of the block (DWT CYCCNT delta, 180 MHz on MK66). Feeds the load meter and the
// governor (ot::audio::Governor). No-op without a Synth.
void reportBlockCycles(uint32_t cycles);

// Audio interrupt, at the start of every live block (Profiler::startMeasuringAudio, before MIDI and the sequencer): `startCycles`
// is the cycle counter at the block start and `cycles` reads it. Turns the note-on admission on: a note-on whose estimated cost
// would take the block over kAdmitPermille starts at the start of the next block instead (at most one block late, never dropped).
// The note-ons deferred in the block before start here, first. No-op without a Synth.
void reportBlockStart(uint32_t startCycles, uint32_t (*cycles)());

// Note-on admission limit: estimated block load, permille of the block (CpuMeter::kBudgetCycles). The default, and a setter for
// the console (cpu admit N, clamped 500..1000)
constexpr uint32_t kAdmitPermille = 870;   // device 2026-11-10, ref/gov-fix.md
void setAdmitPermille(uint32_t permille);
uint32_t admitPermille();

// One live block as the admission saw it (cycles at 180 MHz): the worst block since the last Play and the last overrun block
struct BlockShot
{
    uint32_t cycles;    // the whole block (the readout's window)
    uint32_t pre;       // block start to the first synth part render
    uint32_t extra;     // estimated cost of the note-ons started in it (admitted and from the queue)
    uint32_t admitted;  // note-ons admitted in it
    uint32_t flushed;   // note-ons from the queue started at its start
    uint32_t deferred;  // note-ons it moved to the next block
    uint32_t estMax;    // the largest estimate of an admission check in it (0: none)
    uint32_t voices;    // synth voice renders in it (fading ones included)
    uint32_t renderCyc; // their timed cycles together (engine render calls only)
    uint32_t moving;    // synth parts whose parameters moved in it (automation, Patch FX, edits)
};
void loadShots(BlockShot& worst, BlockShot& lastOverrun);

// Live load counters of the open synth since the last Play (resetLoadStats) or project load. Read from the main loop or the
// console (copies of 32-bit words written by the audio interrupt).
struct LoadStats
{
    uint32_t blocks;        // live blocks counted
    uint32_t overruns;      // blocks at or over the block budget (CpuMeter, L >= 1000 permille)
    uint32_t peakPermille;  // the worst single block
    uint32_t avgPermille;   // CpuMeter average (about 100 ms)
    uint32_t cuts;          // governor cuts (cap lowered)
    uint32_t cutsTotal;     // governor cuts since boot
    int32_t cap;            // governor cap now
    int32_t budget;         // voice budget
    int32_t sounding;       // synth voices sounding now
    uint32_t govSteals;     // voices the governor stole after a cut
    uint32_t steals;        // voices stolen by a note-on in a full pool
    uint32_t fastKills;     // of those, released voices under kFastKillLevel freed at once (no fade block)
    uint32_t preSteals;     // victims faded in the block before their deferred note started
    uint32_t admitted;      // note-ons checked by the admission (live, inside a block)
    uint32_t deferred;      // note-ons moved to the start of the next block
    uint32_t deferMaxBlock; // most note-ons deferred in one block
    uint32_t deferMaxUs;    // longest delay of a deferred note-on (asked for to started)
    uint32_t queueFull;     // note-ons admitted over the limit because the queue was full
    uint32_t attackUs[3];   // learned extra cost of a note-on per synth part (engine noteOn + first render over a running voice)
    uint32_t voiceUs[3];    // learned cost of one running voice per synth part
    uint32_t stepPreUs;     // learned: how much later the synth render starts in a block with note-ons than guessed at the first
};
void loadStats(LoadStats& s);
// Play (Sequencer::play): the counters above start again (the governor's own lifetime count stays in cutsTotal)
void resetLoadStats();

// Offline render (export): the governor is off and the cap is back at the budget.
void setOffline(bool offline);

// Project load: every synth voice is cut and the governor and load meter start fresh (cap back at
// the budget, no hold-off). Call with audio interrupts disabled. No-op without a Synth.
void resetOnProjectLoad();

struct EngineDef;
// A plugin engine is about to be detached (FEATURE_PLUG_HOST, plug_synth.cpp, its flash bytes may change next): every part whose
// patch is of `def` has its voices cut and no engine (silent) until it gets a new patch. Call with audio interrupts disabled.
void engineGone(const EngineDef* def);
// A plugin engine starts again in place (plug_synth.cpp, its work memory moves while the arena is packed): every part whose patch
// is of `def` has its voices cut and its slot scratch cleared, it keeps the engine (no call reaches the engine while it is stopped,
// plug_synth.cpp clears PlugSynth::x). engineRestarted runs slotReset on the cleared scratch once the engine is back. Both with
// audio interrupts disabled (the caller's window, they never touch the interrupt mask).
void engineRestartLocked(const EngineDef* def);
void engineRestartedLocked(const EngineDef* def);
// The patch was made while its engine was not attached (no parameters) and the engine is attached now: the part needs a new
// patch of the same type to play (Synth::setInstrumentType)
bool patchStale(const Patch& patch);

// Audio interrupt, once per part and block (PolySynth::update): true when the part would render nothing this block - no
// voice sounding and no slot effect tail (the same test as the early return of the instrument's render). The caller then
// transmits nothing (a missing block is silence for the amplifiers, analysers and mixers downstream) instead of
// rendering, soft clipping and sending blocks of zeros. False for a part that does not exist.
bool partIdle(int part);

// Patch slots and the Patch step FX. Per part, 16 slots in the project: slot 0 is <P>.patch (what stock firmware opens),
// slot n > 0 is <P>-n.patch, both in Workspace/ and in the project folder. The part's live Patch is the slot that is open in
// the editor (current). The bank holds the plain values (no macros, no OpenPatch) of every slot file of the part's engine,
// for the FX. A slot file of another engine, a missing file or a damaged one is not in the bank.
constexpr int kPatchBankSlots = 16;
constexpr int kPatchBankParts = 3;
// The slot 0 backup of an engine change is the 17th file of the part, "P-b" (patchSlotName(part, kBackupSlot)). It is not a slot: the
// bank and the selector never see it, copyWorkspacePatches copies it like the slot files, and the project open prunes it in the Workspace.
constexpr int kBackupSlot = kPatchBankSlots;
// File name without folder and extension of slot `slot` of `part`: "P" for slot 0, "P-n" otherwise, "P-b" for kBackupSlot.
void patchSlotName(int part, int slot, char (&out)[8]);

// Main thread, with audio interrupts DISABLED by the caller, in the same window that replaces the part's live Patch object (the old
// one may be freed there). The audio callback reads only this pointer (one aligned word), never the shared_ptr the Synth holds.
void patchBankSetLive(int part, Patch* patch);
// Main thread: current slot back to 0 and the bank rebuilt from the slot files in `workspace` (project load, engine change).
void patchBankPreload(int part, Patch& patch, PatchStore& workspace);
// The open slot of the part in the editor (0 when the bank was never built)
int patchBankCurrent(int part);
// Main thread, project load before slot 0 is read: the editor goes back to slot 0 with no pending copy and no FX values, so a
// fallback that saves the open slot (Init when <P>.patch is missing) writes <P>.patch and never a slot of the previous project.
void patchBankToSlotZero(int part);
// Main thread: the side file <P>.names (open_slotnames.h) of the part in `workspace`, the names of slots 1..15. Read: false and all
// names empty when the file is missing or damaged. Write: a part with no name and no file writes nothing.
static_assert(kSlotNameSlots == kPatchBankSlots, "one name per slot");
bool patchNamesRead(int part, PatchStore& workspace, SlotNames& out);
bool patchNamesWrite(int part, PatchStore& workspace, const SlotNames& in);
// The names of a project save. `names` holds what the part has in memory (an empty name is "not named"). When the load could not read the
// names file (a card error, the part's names are then all empty in memory) the file is read again first and its names fill every slot
// the memory leaves empty, so the names on the card survive and the edits made since are kept (`names` then holds the merged set, the
// caller takes it back). When it still cannot be read the file is left untouched and this is true: there is nothing to save, the next
// save tries again, and stock has no names file to fail the project save for. False only when the file could not be written.
bool patchNamesPersist(int part, PatchStore& workspace, SlotNames& names);
// Engine change, main thread, the old engine still live and its slot 0 file (<P>.patch) up to date: keeps that file as <P>-b.patch.
// kSkipped: there is nothing to keep, slot 0 is an untouched Init (the bytes of a new patch of the engine), has no file or no file of the live
// engine, an older backup stays. kFailed: the card failed (slot 0 could not be read, or the backup could not be written): slot 0 may be a
// patch that the new engine's Init would replace, the caller must not change the engine. An older backup stays.
enum class Backup
{
    kFailed,
    kSkipped,
    kSaved,
};
Backup patchBackupSave(int part, Patch& live, PatchStore& workspace);
// The engine may change after this backup result: every result but kFailed (a patch that could not be kept must not be replaced).
inline bool backupAllowsEngineChange(Backup result)
{
    return result != Backup::kFailed;
}
// Where a save of the open slot leaves the part's dirty flag, on every path that saves (slot select, engine change, project save, accepting a
// library patch): saved clears it, a failed save sets it, even when the patch was clean before (an accepted library patch is not dirty until
// it is saved), so a later project save or slot select retries it and never discards it.
inline void settleDirtyAfterSave(bool saved, bool& patchDirty)
{
    patchDirty = !saved;
}
// Engine change, main thread, after the new engine's Init patch is live and the bank was rebuilt for it: if <P>-b.patch is a file of
// that engine (the engine field of the file decides, as for every slot file) it becomes slot 0: written to <P>.patch, loaded into the
// live patch and the bank. False: nothing changed, the caller loads Init.
bool patchBackupRestore(int part, Patch& live, PatchStore& workspace);
// Main thread: if the FX replaced the live values, put the edited values of the open slot back (the editor does it when a screen
// shows the patch). Every write of a parameter does it by itself, in the same audio-free window, and a read of a parameter gives the
// edited value while the FX has replaced it, so an edit made while the pattern plays is never undone by a later restore.
void patchBankRestore(int part);
// Main thread: writes the live patch of the part as the file of its open slot in `workspace` (<P>.patch for slot 0, <P>-n.patch
// otherwise). The values the FX replaced go back and the patch is copied out in one audio-free window, so the file is the open slot
// whatever the FX does meanwhile. The old file stays until the new one is complete. On success the bank row of the slot is the values
// that were written. False: nothing was written, the file and the bank are as they were.
bool patchSaveOpenSlot(int part, Patch& live, PatchStore& workspace);

enum class SlotSelect
{
    kSame,        // already the open slot
    kLoaded,      // the slot file was loaded into the live patch
    kCopied,      // no file: the live patch stays and becomes the new slot (no file until it is saved)
    kOtherEngine, // a file of another engine sits there: nothing changed, the slot is not usable with this engine
    kError,       // the file could not be read: nothing changed
    kSaveFailed,  // Synth::selectPatchSlot: the open slot is edited and its file could not be written: nothing changed, it stays open
};
// Main thread. Open slot `slot`. The caller saved the open slot first when it was dirty. Macros of the live patch come from
// the file (kLoaded) or stay (kCopied). The file's values, the current slot and the FX shadow change in one audio-free window.
SlotSelect patchBankSelect(int part, int slot, Patch& patch, PatchStore& workspace);
// Main thread: cancel of the library browser. Reloads the open slot from its file, or from the slot it was copied from.
// False when there is nothing to reload.
bool patchBankRevert(int part, Patch& patch, PatchStore& workspace);

// Audio callback (Sequencer::tick, before the synth renders): the Patch step FX. Copies the values of `slot` into the part's
// live patch. The slot that is open in the editor puts the edited values back. False (nothing changed) when the slot is out of
// range or has no file of this engine. No allocation, no file access, no shared_ptr, patchDirty untouched.
bool patchBankApply(int part, int slot);

// Global synth voice pool shared by all slots: default 4 (OpenTracker 2.2.4), max 8.
constexpr int kDefaultVoiceBudget = 4;
constexpr int kMaxVoiceBudget = 8;
void setVoiceBudget(int voices);
int voiceBudget();
int voiceCap();       // budget lowered by the governor
int voicesSounding(); // synth voices counted by the pool (stolen ones excluded)
uint32_t governorCuts();

} // namespace polysynth::open
