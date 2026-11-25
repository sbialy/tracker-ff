#ifndef FF_HOOKS_H_
#define FF_HOOKS_H_

// Feature hooks of tracker-ff into stock files, the same pattern as devhooks.h: a stock file gets one hook line and this include, the feature
// code lives in its own files. Every hook is an always inline function, so the image compiles exactly the calls the inline blocks made, in
// the same order, and a build without the features (stock, IMX) compiles an empty function.
//
// Rule for new work: a feature that needs a stock call site adds a hook here (coarse: an event and the object it is about, not a list of
// parameters) and one line in the stock file. synth-open/tools/ff_ifdef_ratchet.sh counts the conditional lines of our feature flags in the
// stock dirs, the count may only go down.

#include <stdint.h>

#include "ff_config.h"
#include "feature_flags.h"
#ifdef FEATURE_FEATURES
#include "features.h"
#include "midiRemote.h"
#endif
#ifdef FEATURE_EXT_HOOKS
#include "skipback_ui.h"
#endif
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif
#ifdef FEATURE_TABLES
#include "tables.h"
#endif
#include "synthPlayRec.h"
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
#include "programIn.h"
#include "synthPlayRouter.h"
#endif
#ifdef FEATURE_PARTS
#include "parts.h"
#endif
#ifdef FEATURE_PROJECT_STORE
#include "projectSidecar.h"
#endif
#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
#include "partAuto.h"
#endif

#define FF_HOOK_INLINE static inline __attribute__((always_inline))

#include "Platform.h"
#include "mtCapture.h"
#include "mtSequencer.h"

// TrackerImpl::init (Tracker.cpp), before the synth starts: the project store, the synth engine plugins, installed into the FlexNVM bank and added to the
// engine list
FF_HOOK_INLINE void ffHookBootBeforeSynth(Platform* platform)
{
#ifdef FEATURE_PROJECT_STORE
    projstore::boot(platform);   // the card and the build id of project.mtff, before any project file is touched
#endif
#ifdef FEATURE_PLUG_HOST
    plug::synthBoot(platform->getFileSystem());
#endif
    (void)platform;
}

// TrackerImpl::init, after the file manager, the interface and the Features registry: the plugin host's boot (inserts, the plugin index)
FF_HOOK_INLINE void ffHookBootLate(Platform* platform)
{
#ifdef FEATURE_PLUG_HOST
    plug::boot(platform);
#else
    (void)platform;
#endif
}

// TrackerImpl::update (Tracker.cpp), main loop, after the interface and before the SD stream: the plugin host's tick and the synth pages that
// wait for a plugin engine to install
FF_HOOK_INLINE void ffHookMainLoopEarly()
{
#ifdef FEATURE_PLUG_HOST
    plug::tick();
    synthModelTick(); // SynthEditor.cpp: the model list rested on a plugin engine that is not installed
    synthViewTick();  // SynthPatchEditor.cpp: the cycle view of a plugin engine (ABI 1.2 plug_ext_synth.view)
#endif
}

// midiUpdate (mtMidi.cpp), audio interrupt, after the USB queue was read: the USB cable was pulled when the bus went idle, no note off comes for
// what it held. The MIDI remote lets go of its keys and pads, the Synth play page of the notes (and a pedal or bend) that came over USB.
// Returns true when the bus was lost, midiUpdate then re-sends the MPE out zone configuration
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
void synthPlayCaptureReleaseUsb(); // mtMidi.cpp: the Capture ring gets the note off of every USB key the Synth play page still holds (SP-U3)
#endif
FF_HOOK_INLINE bool ffHookUsbMidiPolled(UsbMidi* usb)
{
    bool lost = false;
#if defined(FEATURE_FEATURES) || defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    if (usb->takeBusLost())
    {
        lost = true;
#ifdef FEATURE_FEATURES
        midi_remote::releaseSource(midi_remote::kSourceUsb);
#endif
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
        synthPlayCaptureReleaseUsb(); // first: it reads the cells releaseSource frees
        synthplay::releaseSource(synthplay::kUsb);
#endif
    }
#endif
    (void)usb;
    return lost;
}

// TrackerImpl::update, main loop, after the sequencer: the Features registry and the next card chunk of a running Skip-back save
FF_HOOK_INLINE void ffHookMainLoopLate()
{
#ifdef FEATURE_FEATURES
    features::tick();
#endif
#ifdef FEATURE_SKIPBACK
    skipback::uiTick(); // Skip-back: the next card chunk of a running save
#endif
}

// TrackerImpl::updateAudioBuffers (Tracker.cpp), audio interrupt: the block the codec gets, after the engine rendered it
FF_HOOK_INLINE void ffHookAudioBlockOut(void* left, void* right, int length)
{
#ifdef FEATURE_SKIPBACK
    // Skip-back: the block the codec gets into the SDRAM ring (returns at the first test while the feature is off)
    if (length == int(skipback::kBlockFrames))
    {
        skipback::block(static_cast<const int16_t*>(left), static_cast<const int16_t*>(right));
    }
#endif
#ifdef FEATURE_SYNTH_REC
    // Synth play page REC: the same block as a mono take into the free tail of the sample memory (returns at the first test while idle)
    if (length == int(synthplayrec::kBlockFrames))
    {
        synthplayrec::block(static_cast<const int16_t*>(left), static_cast<const int16_t*>(right));
    }
#endif
    (void)left;
    (void)right;
    (void)length;
}

// Sequencer::update (mtSequencer.cpp), main loop: the next Part image, one Part ahead, and the user's pattern back after a Parts playback
FF_HOOK_INLINE void ffHookSeqUpdate()
{
#ifdef FEATURE_PARTS
    parts::update();
#endif
}

// the sequencer's song logic asks whether a Parts song plays (its next pattern comes from the Parts image, not from the song list)
FF_HOOK_INLINE bool ffPartsPlaying()
{
#ifdef FEATURE_PARTS
    return parts::playing();
#else
    return false;
#endif
}

// the armed pattern switch took place (mtSequencer.cpp): the pattern the user switched to plays now, a Part image is gone
FF_HOOK_INLINE void ffHookSeqPatternSwitched()
{
#ifdef FEATURE_PARTS
    parts::imageReplaced();
#endif
}

// ---- Tables (Table lite, core/source/tables.h, tables_run.inc): one runner per track in the sequencer. The hooks run in the audio tick (the
// tick, play_microStep) and in the main loop and the MIDI callbacks (the live notes), so each one is an always inline test of a flag word or
// a pool byte and a call. A build without Table lite (ff_config.h FEATURE_TABLES) compiles empty functions.

// Sequencer::init: the table pool and runners (SDRAM), before the first tick
FF_HOOK_INLINE void ffHookSeqInit()
{
#ifdef FEATURE_TABLES
    tables::init();
#endif
}

// Sequencer::tick, after an external clock block: stopped on an external clock that sends no pulses, a table a live note started has no time
// base, it keeps time on the last known tempo (clock pulses stopped for 0.5 s). With pulses tick_uStep advances it
FF_HOOK_INLINE void ffHookSeqClockGap(Sequencer& s, bool playing, float secWithoutPulse, float intervalSeconds)
{
#ifdef FEATURE_TABLES
    if (tables::g_runMask && !playing && secWithoutPulse > Sequencer::kTableIdleClockGapSec)
    {
        s.tableIdleFallback(intervalSeconds);
    }
#else
    (void)s;
    (void)playing;
    (void)secWithoutPulse;
    (void)intervalSeconds;
#endif
}

// Sequencer::tick_uStep, the sequencer is stopped: what a pad or MIDI note started keeps running
FF_HOOK_INLINE void ffHookSeqStoppedTick(Sequencer& s)
{
#ifdef FEATURE_TABLES
    if (tables::g_runMask)
    {
        s.tableIdleTick();
    }
#else
    (void)s;
#endif
}

// MidiAdapter::sendNoteOn / sendNoteOff (core/synth): a note of synth part `part` (0 based) goes out to the stock synth. The Synth
// Automation slots of Tracker+ and Mini start, restart or release with it (partAuto.h). on: a note on with a velocity above 0
FF_HOOK_INLINE void ffHookSynthNote(int part, uint8_t note, bool on)
{
#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
    if (on)
        partauto::noteOn(part, note);
    else
        partauto::noteOff(part, note);
#else
    (void)part;
    (void)note;
    (void)on;
#endif
}

// play_microStep, the FX of a step that has the TBL type (FX_TYPE_TABLE, R12) in a slot: true when the build takes it (the stock build leaves
// the type to the default FX handling). noteSounds: the track has a note that sounds
FF_HOOK_INLINE bool ffHookSeqFxOnTrack(Sequencer& s, int track, int fxIndex, uint8_t value, bool noteSounds, int instrument)
{
#ifdef FEATURE_TABLES
    s.tableFxStep(uint8_t(track), fxIndex, value, noteSounds, instrument);
    return true;
#else
    (void)s;
    (void)track;
    (void)fxIndex;
    (void)value;
    (void)noteSounds;
    (void)instrument;
    return false;
#endif
}

// play_microStep, a step note went out on the track (after the note-on: the voice is reset, the table starts from row 0 on top)
FF_HOOK_INLINE void ffHookSeqNoteSent(Sequencer& s, uint8_t track, const Sequencer::strPlayer::strPlayerTrack::strSendStep& step, int fxSlots)
{
#ifdef FEATURE_TABLES
    s.tableNoteOn(track, step, fxSlots);
#else
    (void)s;
    (void)track;
    (void)step;
    (void)fxSlots;
#endif
}

// play_microStep, an Arp, Roll or Stutter note went out again: the running table again from row 0
FF_HOOK_INLINE void ffHookSeqNoteRepeated(Sequencer& s, int track, const Sequencer::strPlayer::strPlayerTrack::strSendStep& step)
{
#ifdef FEATURE_TABLES
    if (tables::run(track).table || tables::pool().bind[step.instrument < tables::kInstruments ? step.instrument : 0])
    {
        s.tableRetrigger(track, step);
    }
#else
    (void)s;
    (void)track;
    (void)step;
#endif
}

// play_microStep, last: the track's table advances one uStep (a new row when its tic is reached, a 2 B internal RAM test for a track without one)
FF_HOOK_INLINE void ffHookSeqTrackTick(Sequencer& s, int track)
{
#ifdef FEATURE_TABLES
    if (tables::g_runMask & (1u << track))
    {
        s.tableAdvance(uint8_t(track));
    }
#else
    (void)s;
    (void)track;
#endif
}

// a live note (pads, MIDI in on a track, the edit preview) went out on the track
FF_HOOK_INLINE void ffHookSeqLiveNoteOn(Sequencer& s, int track, int instrument)
{
#ifdef FEATURE_TABLES
    s.tableLiveNoteOn(uint8_t(track), int(instrument));
#else
    (void)s;
    (void)track;
    (void)instrument;
#endif
}

// the live note on the track went off (pad release, MIDI note off)
FF_HOOK_INLINE void ffHookSeqLiveNoteOff(Sequencer& s, int track)
{
#ifdef FEATURE_TABLES
    s.tableLiveNoteOff(uint8_t(track));
#else
    (void)s;
    (void)track;
#endif
}

// Sequencer::cancelFxes: the running FX end (Stop, pattern change), so do the tables of every track
FF_HOOK_INLINE void ffHookSeqCancelFx(Sequencer& s)
{
#ifdef FEATURE_TABLES
    s.tableStopAll();
#else
    (void)s;
#endif
}

// ---- project files (core/FileManager). Two sidecars travel with a project: project.mtff (the project store, projectSidecar.h) and
// plugins.txt (the plugin host, plug_host.h). They share the path buffers (projectStoreIo.h): every hook runs them one after the other.

// a project is closing (open or new, before its files load): the settings that act while files load go back to the old behaviour
FF_HOOK_INLINE void ffHookProjectClosing()
{
    capspill::forget(); // the Capture spill's Undo group belongs to the old project
#ifdef FEATURE_PROJECT_STORE
    projstore::projectClosing();
#endif
}

// the stereo pair links of project.mtff, before the instruments load: 0 without the store, a file or the key
FF_HOOK_INLINE uint8_t ffProjectPairsPreload(const char* dir)
{
#ifdef FEATURE_PROJECT_STORE
    return projstore::pairsPreload(dir);
#else
    (void)dir;
    return 0;
#endif
}

// after a project load from the Workspace (newProject: an empty Workspace, not an open)
FF_HOOK_INLINE void ffHookProjectLoaded(const char* dir, bool newProject)
{
#ifdef FEATURE_PROJECT_STORE
    projstore::loaded(dir, newProject);
#endif
#ifdef FEATURE_PLUG_HOST
    plug::projectLoaded(dir, newProject);   // after project.mtff: its host.txt names the project.mtff note of this load
#endif
    (void)dir;
    (void)newProject;
}

// Workspace save: both files are tried, whatever the first said. False: a setting is not on the card (the save failed)
FF_HOOK_INLINE bool ffHookWorkspaceSave(const char* dir)
{
    bool ok = true;
#ifdef FEATURE_PLUG_HOST
    ok = plug::projectSave(dir);
#endif
#ifdef FEATURE_PROJECT_STORE
    ok = projstore::save(dir) && ok;
#endif
    (void)dir;
    return ok;
}

// project open, before the load: the project's files to the Workspace (project.mtff first, as it always was)
FF_HOOK_INLINE bool ffHookProjectOpen(const char* projectDir, const char* workspaceDir)
{
    bool ok = true;
#ifdef FEATURE_PROJECT_STORE
    ok = projstore::open(projectDir, workspaceDir);
#endif
#ifdef FEATURE_PLUG_HOST
    ok = plug::sidecarOpen(projectDir, workspaceDir) && ok;
#endif
    (void)projectDir;
    (void)workspaceDir;
    return ok;
}

// Save As: the Workspace files to the project folder (project.mtff first, as it always was)
FF_HOOK_INLINE bool ffHookProjectSaveAs(const char* workspaceDir, const char* projectDir)
{
    bool ok = true;
#ifdef FEATURE_PROJECT_STORE
    ok = projstore::toProject(workspaceDir, projectDir);
#endif
#ifdef FEATURE_PLUG_HOST
    ok = plug::sidecarToProject(workspaceDir, projectDir) && ok;
#endif
    (void)workspaceDir;
    (void)projectDir;
    return ok;
}

// the project.mt just read or written: project.mtff keeps its CRC and size (the stamp)
FF_HOOK_INLINE void ffHookProjectStamp(uint32_t crc, uint32_t size)
{
#ifdef FEATURE_PROJECT_STORE
    projstore::stamp(crc, size);
#endif
    (void)crc;
    (void)size;
}

// a project open has just read the project.mt of the project folder: its stamp, not the one of the Workspace copy (which gets the project's
// name and path written into it next, another CRC), is what that open's project.mtff is compared with
FF_HOOK_INLINE void ffHookOpenStampKeep()
{
#ifdef FEATURE_PROJECT_STORE
    projstore::openStampKeep();
#endif
}

// a new project's first project.mt is written: its project.mtff (the new project's defaults) with that stamp
FF_HOOK_INLINE void ffHookNewProjectSaved(const char* dir)
{
#ifdef FEATURE_PROJECT_STORE
    projstore::newProjectSaved(dir);
#endif
    (void)dir;
}

// mtMidi.cpp, USB and DIN Program Change in: a bank slot of a synth part (Config > MIDI > Program in). Dropped without the synth engines
FF_HOOK_INLINE void ffHookProgramChangeIn(bool usb, uint8_t channel, uint8_t program)
{
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    programin::received(usb, channel, program);
#else
    (void)usb;
    (void)channel;
    (void)program;
#endif
}

// mtMidi.cpp, USB and DIN channel pressure in (0xD0): only the Synth play page uses it (MPE pressure, synthplay::channelPressure), every other
// page and a build without the synth engines drops it as before
FF_HOOK_INLINE void ffHookChannelPressureIn(bool usb, bool playPage, uint8_t channel, uint8_t pressure)
{
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    if (playPage)
        synthplay::channelPressure(usb ? synthplay::kUsb : synthplay::kDin, channel, pressure);
#else
    (void)usb;
    (void)playPage;
    (void)channel;
    (void)pressure;
#endif
}

// configMenu.cpp: the rows our features add to the Config > MIDI group (Program in), 0 where the feature is not built, the row object then
// sits past the group's count and is not shown
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
constexpr uint8_t kFfMidiMenuRows = 1;
#else
constexpr uint8_t kFfMidiMenuRows = 0;
#endif

// ---- pattern Undo (fileManagerUndo.cpp): the Capture spill (mtCapture.h capspill) ties its group record to the Undo snapshot of the first pattern

// a pattern Undo snapshot went into this slot of the ring
FF_HOOK_INLINE void ffHookPatternUndoStored(uint8_t slot)
{
    capspill::undoStored(slot);
}

// Undo restored the snapshot of this slot. True: it was a Capture spill, its new patterns and song slots are gone too, so there is no redo
FF_HOOK_INLINE bool ffHookPatternUndone(uint8_t slot)
{
    return capspill::undone(slot);
}

#endif // FF_HOOKS_H_
