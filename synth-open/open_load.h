// Open PolySynth shim for MK66 - synth voice budget and governor interface.
// Plain C++ with no PolySynth or audio library includes, so core/source/Profiler.cpp and the
// exporter can call it. Implemented in open_synth.cpp (OpenSynth owns the pool and the governor).
#pragma once

#include <cstdint>
#include <cstring>

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

// PolySynth::update, at the start of every block (live and offline): a new block starts, the tempo is asked once for all parts until the
// next one. No-op without a Synth.
void synthBlock();

// Audio interrupt, at the start of every live block (Profiler::startMeasuringAudio, before MIDI and the sequencer): `startCycles`
// is the cycle counter at the block start and `cycles` reads it. Turns the note-on admission on: a note-on whose estimated cost
// would take the block over kAdmitPermille starts at the start of the next block instead (at most one block late, never dropped).
// The note-ons deferred in the block before start here, first. No-op without a Synth.
void reportBlockStart(uint32_t startCycles, uint32_t (*cycles)());

// Note-on admission limit: estimated block load, permille of the block (CpuMeter::kBudgetCycles). The default, and a setter for
// the console (cpu admit N, clamped 500..1000)
constexpr uint32_t kAdmitPermille = 870;   // device 2026-11-10
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

// Stop with a New Note Action in use (ref/synth-nna-design.md D11): every synth part releases every gated voice.
void releaseAllParts();

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

// Main thread (plug_synth.cpp synthService, the patch editor's cycle view): the engine of part `part` and the stored values of its live
// patch (no macros, no automation, the Patch FX values while the FX has replaced them), count entries, valid until the main thread
// changes the part's patch. nullptr: no such part, no patch or a missing engine.
const EngineDef* partPatch(int part, const float** values, int* count);
// Main thread: the wave file path the live patch of `part` stores for its engine parameter `param` (OpenPatch::waveRef), into out (cap
// bytes). Its length, or -1: no such part or patch, no path for the parameter, or the parameter was edited since the path was stored.
int partWaveRef(int part, int param, char* out, size_t cap);
// Main thread (the patch editor's value text of a wave file parameter): the path of entry `index` of the host's /Waves list (plug_files.h, sorted by
// path, relative to /Waves) into out (cap bytes). False when there is no card or no such entry. Builds the list when this session has not (a walk
// of the card, once: the engine's first file read does the same), later calls read SDRAM only. plug_synth.cpp, false without the plugin host.
bool waveListName(int index, char* out, size_t cap);
// Main thread, same list: true when /Waves holds no usable wave (no card, no folder, no .wav), so the wave file parameters (waveFileParam, open_mod.h)
// are not listed anywhere. True without the plugin host (no engine there has such a parameter, nothing is listed either way).
bool waveFilesAbsent();
// Main thread: true when the stored wave path resolves on the card, as the engine's lookup sees it (the file at that path or its base name directly in
// /Waves, plug_files.h waveListHas). True when the list cannot tell (full list, no plugin host).
bool waveListHas(const char* path);
// A wave file parameter: an index into the card's /Waves list (plug_abi.h PLUG_EXT_FILES, PLUG_FILES_MAX entries, 0 = none), what ALCO's A File and
// B File are. The contract is the shape (an integer 0..PLUG_FILES_MAX named "<x> File"), nothing is added to the plugin ABI or to the patch.
inline bool waveFileShape(const char* name, bool isInt, float mn, float mx)
{
    const size_t n = name ? std::strlen(name) : 0;
    return isInt && mn == 0.0f && mx == 1024.0f && n > 5 && !std::strcmp(name + n - 5, " File");
}
// The editor's rows (parameter list, macro targets): true for the parameter that is not listed now, a wave file parameter while waveFilesAbsent.
inline bool waveFileRowHidden(const char* displayName, bool isEnum, float minValue, float maxValue)
{
    return !isEnum && waveFileShape(displayName, true, minValue, maxValue) && waveFilesAbsent();
}

// Audio interrupt, once per part and block (PolySynth::update): true when the part would render nothing this block - no
// voice sounding and no slot effect tail (the same test as the early return of the instrument's render). The caller then
// transmits nothing (a missing block is silence for the amplifiers, analysers and mixers downstream) instead of
// rendering, soft clipping and sending blocks of zeros. False for a part that does not exist.
bool partIdle(int part);

// Part destinations of the synth automation (open_mod.h ModKind::kPart): audio interrupt, once per block. Fills v[kModParts] (ModPart order:
// pan, gain, reverb send, delay send) with the offset the slots add to the part's setting, in the audio engine's units (pan points, dB,
// send percent), and returns the mask of the destinations that have a slot, bit n = ModPart n. 0: no part slot (v is not written).
uint8_t partAuto(int part, float* v);

// Paraphony (PLUG_EXT_CHORD, plug_abi.h). An engine with a Chord parameter (the MIDI Chord FX table, synth-open/plug/plug_chord.h) and
// a Chord Mode parameter (0 Poly, 1 Para): the built-in ACD, or a plugin that answers the query (plug_synth.cpp checks the answer
// at attach). -1 for an engine without them.
struct EngineDef;
struct ChordParams
{
    int8_t chord = -1;
    int8_t mode = -1;
    int8_t strum = -1;   // the Strum parameter (signed units of kStrumUnitUs, + up, - down), -1: the engine plays chords unstrummed
    uint8_t last = 255;  // the last chord value its Chord enum takes (67 for a plugin of the 68 entry table), a higher value is played as plain notes
};
ChordParams chordParamsOf(const EngineDef* def);
ChordParams plugChordParams(const EngineDef* def);   // plug_synth.cpp: an attached plugin engine's answer
// Sequencer (audio interrupt, or main thread with the same order): true when the part's patch is paraphonic, so a MIDI Chord step is
// sent as ONE note-on. partNoteChord before that note-on puts the step's chord value (0..76) into the engine's Chord parameter
// for the next note-on of `note` on the part only, a Strum FX (strum != 0, units of 2 ms, + up, - down) into its Strum
// parameter when the engine has one. False / no-op without a Synth or for a part that does not exist.
bool partChordPara(int part, uint8_t chord = 0);   // false too when chord is past the engine's Chord enum (ChordParams::last)
void partNoteChord(int part, uint8_t note, uint8_t chord, int8_t strum = 0);
// Pitch bend range of a part in semitones for a full deflection (+-8192) of the bend message, 0..24, default 1 (the Micro-tune FX
// reads in cents at 1). The Synth play page sets it while it is open and sets 1 again at exit. Main thread, a plain float store,
// the audio callback reads it with the next bend message. No-op for a part that does not exist.
void partBendRange(int part, float semitones);
// MPE in (Synth play page, lower zone, ref/mpe-in-p1.md). mpeNote(chan) right before the send of a note-on or note-off of a member channel 2..16 and
// mpeNote(0) right after (the same call chain, like noteTag): the voice remembers the channel, a note-off with a channel releases only that channel's
// voice. partMpe: the member bend range in semitones for a full deflection (0..96) and the pressure amount 0..1 (0: pressure does nothing). partNoteBend:
// the raw 14 bit bend -8192..8191 of member channel `chan` (its voices and the next note on it). partNotePress: the pressure 0..127 of a member
// channel's key that is down, chan 1 (the master) puts it on every voice of the part. A level of 127 is no change, 0 is 1 - amount. Audio callback or
// main thread (plain stores), no-ops for a part that does not exist.
void mpeNote(uint8_t chan);
void partMpe(int part, float memberRange, float pressAmount);
void partNoteBend(int part, uint8_t chan, int raw);
void partNotePress(int part, uint8_t chan, uint8_t press);
// Synth play page, A/B morph: the gain 0..1 of a part's voices (a factor on the voice gain target, so it ramps over the block like every
// other gain change and the part's sends follow it). The page sets it on both parts and puts 1 back at exit. A plain float store, any
// thread. No-op for a part that does not exist.
void partGain(int part, float gain);
// Voices of the part that sound (on and not fading), for the page's voice line. 0 for a part that does not exist.
int partSounding(int part);
// True while a voice of the part is on, a releasing one included (no side effect, unlike partIdle). Tables: a live note's table ends when
// the part has gone silent. False for a part that does not exist.
bool partActive(int part);
// Note ids. The sequencer gives every note it sends a number (1..65535) and calls noteTag(id) right before the send of the note-on and the
// note-off of that note and noteTag(0) right after it (the same call chain, so the synth parts see the id in onMidi). A voice remembers the id of
// the note that started it, and a note-off that carries an id releases only the voice with that id and pitch (before: every gated voice of the
// pitch, so two live notes of one pitch from two tracks ended together). Id 0 is "no id": the pad, MIDI in and the record path send notes
// that way, their note-offs match by pitch alone and release any voice of the pitch, as before. Same thread as the sends that follow.
void noteTag(uint16_t id, uint8_t endMode = 0);

// Audio callback (Sequencer::tick, before the synth renders): the Tape Stop (start = false) and Tape Start (start = true) step FX
// (core/source/tapeFx.h). The part's voices run down to a standstill or spin up from one over `frames` audio frames: pitch and level
// follow the tape speed. A note-on that comes with the FX (same step, this or the next block) is part of the run, any other note-on
// puts the speed back to normal. Nothing sounds and no note comes: the request is dropped after three idle blocks. No allocation.
void partTape(int part, bool start, uint32_t frames);
// The row of a table (core/source/tables.h) on the part: transpose in semitones on every voice's bend, gain 0..1 on every voice's level.
// (0, 1) is no table. Audio callback, before the part renders
void partTable(int part, int semis, float gain);

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

// Whether `slot` of the part can be applied: the open slot, or a slot with a file of the part's engine in the bank (the page's Bank columns)
bool patchBankHas(int part, int slot);

// Audio callback (Sequencer::tick, before the synth renders): the Patch step FX. Copies the values of `slot` into the part's
// live patch. The slot that is open in the editor puts the edited values back. False (nothing changed) when the slot is out of
// range or has no file of this engine. No allocation, no file access, no shared_ptr, patchDirty untouched.
bool patchBankApply(int part, int slot);

// Morph step FX. kMorphMax is the value of the FX that plays the target alone.
constexpr int kMorphMax = 100;
// Audio callback (as patchBankApply): the live values of the part become a blend of the open slot (A, its edited values) and `slot`
// (B, -1: the part's Morph target), amount 0..kMorphMax. Numeric parameters lerp, discrete ones and the macros and automation are
// A's below the half and B's from it. 0 is the Patch FX to the open slot, kMorphMax the Patch FX to B. False (nothing changed) when
// B is out of range or has no file of this engine.
bool patchBankMorph(int part, int slot, int amount);
// The part's Morph target, the slot a Morph FX blends to when the other FX slot of the step has no Patch FX. Kept in the part's
// names file (m= line, open_slotnames.h), kMorphDefault when the file has none. Main thread sets, any thread reads.
int patchBankMorphTarget(int part);
void patchBankSetMorphTarget(int part, int slot);

// Global synth voice pool shared by all slots: default 4 (OpenTracker 2.2.4), max 16 (the A/B Synth play page: two parts of 8).
constexpr int kDefaultVoiceBudget = 4;
constexpr int kMaxVoiceBudget = 16;
constexpr int kPageVoicesPerPart = 8;   // what one part takes on the page (the voice records of an instrument, open_engine.h)
void setVoiceBudget(int voices);
int voiceBudget();
// The voices a part may have, from the engine's own cap. A plugin engine declares at most 4 voices (plug_abi.h max_voices 1..4, chosen when
// the sequencer could run 3 synth parts), a built-in one 8 (the voice records, for the A/B pair). While the pool is raised above the default 4,
// which only the Synth play page does (up to two parts play, playback is off), a 4 engine takes up to 8 and a built-in keeps its 8. Outside
// the page (the sequencer and the Instrument Parameters page, budget 4) every engine stops at 4 as in 1.3.0, the pool sounds only 4 anyway.
// An engine that declares 1 or 2 keeps its number everywhere.
inline int pageVoiceCap(int cap)
{
    return voiceBudget() > kDefaultVoiceBudget ? (cap == 4 ? kPageVoicesPerPart : cap) : (cap < kDefaultVoiceBudget ? cap : kDefaultVoiceBudget);
}
// The most voices any part has right now: 8 while the Synth play page has raised the pool, else 4. A stored Voices above it (set on a
// 1.4.0 test build) stays in the project and counts as this.
inline int partVoiceLimit() { return voiceBudget() > kDefaultVoiceBudget ? kPageVoicesPerPart : kDefaultVoiceBudget; }
int voiceCap();       // budget lowered by the governor
int voicesSounding(); // synth voices counted by the pool (stolen ones excluded)
uint32_t governorCuts();

} // namespace polysynth::open
