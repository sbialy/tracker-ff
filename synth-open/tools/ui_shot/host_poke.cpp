// Host-side actions for the crash-guard checks. Host only, NOT firmware.
//
// UISHOT_POKE=PATH: the main loop polls (hostPokePoll, called from the host timer stand-in, every 256th millis()) for the file PATH. When it exists its lines are
// run once and the file is removed, results are appended to PATH.out. Everything goes through the public API the UI itself calls:
//   lastinstr N                          mtProject.values.lastUsedInstrument = N (a synth editor key then meets a non-synth instrument)
//   browsegames                          newFileManager.browseGames()   (no UI page calls it in this build)
//   dumpgames                            the list browseGames produced
//   savestereo NAME FRAMES [SLOT]        newFileManager.saveRecordedSound(...) with two different channel buffers, a ramp each;
//                                        with SLOT the recording is imported into that instrument as the sample recorder does
//   savemono NAME FRAMES                 the same with one buffer (left == right)
//   importlabels                         the three importer label buffers byte by byte (start/end/play time)
//   importplayend                        redraw the three importer labels with the play position at the end of the sample
//   sizes                                sizeof(strProjectFile)
//   seeffects                            the effect list of the Sample Editor: "seeffects rows N sel S apply 0|1 [row|row|...]", a row of a moved effect without its card file reads Missing: <name>
//   samplestat N                         "samplestat N len L peak P sum S fnv H": instrument N's sample after an Apply (septfx_sim.sh)
//   recstat                              the sample recorder: source, planes the take records ("recstat source S stereotake T planes P len L size B"), for stereo_io_sim.sh
//   recproj N                            the recorder source stored in the project (mtProject.values.source, saved with the next autosave), what a project load reads
//   linein N                             Config > Line in channel, 0 Stereo, 1 Mono Left, 2 Mono Right
//   recfill FRAMES                       writes a ramp into the left plane and its negative into the right plane of the take, sets the length (what a take holds when it ends)
//   recsave NAME [SLOT]                  saves (WAV, and imports into SLOT when given) the take as the Sample Recorder does (newFileManager.saveRecordedSound with the take's planes)
//   status                               file manager status and current operation
//   paramid POS VALUE                    the synth patch editor's section parameter id list at POS gets VALUE (corrupt patch metadata, a value out of range)
//   macroid POS VALUE                    the same for the section id list, which holds the macro index on a macro section
//   picker                               the Model list of the Synth screen as the control holds it (rows, cursor, the row texts) and the
//                                        engine every synth part stores (synth-open/tools/ui_shot/synth_order_sim.sh), and a "listbar BAR pos POS" line
//                                        after the picker line: BAR 0 when the list draws no highlighted row (a Missing part)
//   rowpx                                the pixel width of every row text of the Model list in the font the list draws (font 1, getTextWidth), then the
//                                        width of the clip the rows are drawn in. Output line "rowpx clip N px W1,W2,..."
//   textw TEXT                           the pixel width of TEXT in that font ("textw HANDTYPE" -> "textw HANDTYPE px N")
//   masterfx FEEDBACK DAMP_PERMILLE      the delay feedback byte and the reverb low pass coefficient after a project load and after audioEngine::init's
//                                        own code (beginDelay, beginReverb), "same" or "DIFFER" (master_fx_sim.sh)
//   patchvals [PART]                     the parameter values of the open patch of a synth part (default 0): "patchvals part 0 n 32 v v0,v1,..." (vary_sim.sh)
//   slotfill P SLOT T                    (SYNTH_OPEN, and SYNTH_PATCHFX on Tracker+ / Mini) slot SLOT of part P at T % of every range, saved, slot 0 open again
//   rawvals P                            the values part P plays now and the Patch FX bank state (SYNTH_OPEN: with the Morph target, PATCHFX: applied slot, rows, target)
//   slotsel P SLOT                       (PATCHFX) Synth::selectPatchSlot as Shift + Up / Down: "slotsel part P slot S rc R name N"
//   slotnames P                          (PATCHFX) the slot names and Morph target of part P (<P>.names): "slotnames part P target T open S names 1=..|3=.."
//   vary P PERCENT SEED / varyundo P   (PATCHFX) Synth::varyPatch / undoVary (F4 / Shift + F4 of the patch editor)
//   morphkinds P                         (PATCHFX) the Morph kind of every param (l blend, r rounded, s switch) and the stock names
//   popup                                the text popup the UI shows now (first line, "popup none" when hidden), for model_rest_sim.sh
//   popuplines                           every line of that popup, "popuplines 'L1' 'L2'" ("popuplines none" when hidden), for skipback_sim.sh
//   rhs                                  the right-hand side of the Synth screen as the UI holds it (the engine the part plays and its voice limit,
//                                        patch name, polyphony, gain, panning, sends), for model_rest_sim.sh
//   imode                                the page of the instrument editor: synth (1/3), macros, automation or other (synth_order_sim.sh scenario 12)
//   watch [reset|mark]                   what the screen showed since the last "watch reset", sampled at every millis() of the main loop: how many
//                                        times the right-hand side changed and the first change, how many popups came up and the first one with its
//                                        text, times in ms after the last "watch mark" (-1 for none) (model_rest_sim.sh)
//   loads [reset|mark]                   engine changes of synth part 0 seen since the last "loads reset" (sampled at every millis() of the
//                                        main loop) and the time of the last one in ms after the last "loads mark" (model_rest_sim.sh)
//   inserts                              "inserts T=<virtual ms> 1=<state>:<name>[values] 2=<state>:<name>[values]" (plug_host.h SlotState), for insert_defer_sim.sh
//   saveproj NAME                        newFileManager.saveProject("Projects", NAME): Save As without the name editor (the copy runs on in the main loop,
//                                        give it a wait), for workspace_backup_sim.sh
//   synthval                             the synth patch editor's selected parameter: its position, the knob scale value (edit mapped, 0 to 1) or the
//                                        list position, for ui_niceties_sim.sh
//   playing                              "playing 0|1": sequencer.isPlay() (ui_niceties_sim.sh)
//   table N TIC LOOP LEN / table row N R NOTE VOL FXTYPE FXVAL / table bind INSTR N   (FEATURE_TABLES) the table setup of a flow (plus_flows.sh)
//   partauto P [N X|N off]               (Tracker+ / Mini) part destination N of synth part P (core/AudioEngine/partAuto.h), prints the partauto line
//   at MS LINE                           LINE runs once the virtual clock reaches MS ms
//   stepnote TRACK STEP NOTE INSTR       a note on a step of the playing pattern (a Mini flow's setup)
//   pat MS LINE                          LINE runs MS ms after the pattern screen first showed (a flow's setup after the boot project load)
// UISHOT_TRACE_TABLES=1 writes the tablerun line, UISHOT_TRACE_PARTAUTO=1 the partauto line of part 0 at every change (*_tables.flow, *_partauto.flow)
// UISHOT_TRACE_PLAY=1 writes "playing 0|1 T=<ms>" at every change of sequencer.isPlay(), UISHOT_TRACE_SYNTHVAL=1 the synthval line at every
// change while the synth patch editor is open on a synth part (plus_flows.sh: the poke read that ends *_playlock.flow and *_patch.flow)
// UISHOT_TRACE_FOLLOW=1 writes "follow track N mode M src S T=<ms>" at every change of a track's Follow chord setting (followchord::setting),
// UISHOT_TRACE_PARTS=1 "parts count N sel S view V build B mode M play P T=<ms>" at every change of the Parts list or the Parts view (the list
// size, the selected Part, the view on, Build, the list's Parts mode, the Part that plays or -1), UISHOT_TRACE_FEATURES=1 "playstoplock 0|1 T=<ms>"
// at every change of Config > Features > Play stop lock (plus_flows.sh: the poke read that ends *_follow.flow, *_parts.flow and *_features.flow)
//   projload                             ffHookProjectLoaded(Workspace): what the end of a project load does to the inserts (insert_defer_sim.sh)
//   duck                                 send ducking: "duck engine=SRC,DEPTH,RELEASE,THRESH sidecar=SRC,DEPTH,RELEASE,THRESH gain=G page=P place=N" (the engine's settings and
//                                        gain, the plug host's copy that the sidecar is written from, the master screen's page: none, limiter or duck) for duck_sim.sh
//   projsave                             ffHookWorkspaceSave(Workspace): the Workspace save writes (or removes) both sidecars, "projsave ok" or "projsave FAILED"
//   sidecar open SRCDIR DSTDIR          ffHookProjectOpen: the project folder's plugins.txt to the Workspace (project open), "sidecar open rc=0|1"
//   sidecar save WSDIR PROJDIR          ffHookProjectSaveAs: the Workspace file to the project folder (project save), "sidecar save rc=0|1"
//   sidecar wssave DIR                  ffHookWorkspaceSave: the Workspace save of the sidecar in DIR, "sidecar wssave rc=0|1" (sidecar_path_sim.sh)
//   mtffver                             the header of the project.mtff the last project load read: "mtffver <format> <writer as hex (major<<16|minor<<8|patch), 0 = unknown>" (plug::mtffFormatVersion, plug::mtffWriterVersion, mtff_sim.sh)
//   strumdepth [N]                      the project's Strum ramp depth (plug::strumRampDepth), set to N (0..100) when given: "strumdepth N" (mtff_sim.sh)
//   macromode [N]                       the project's macro depth mode (plug::macroMode: 0 per patch, 1 Add, 2 Scale), set to N when given: "macromode N" (projset_sim.sh)
//   patchmode PART                      "patchmode part P scale|add": the macro depth mode of the patch of synth part P (OpenPatch::macroScale)
//   synthlib save|load PART NAME        the patch library (Patches folder) as the Synth screen uses it: Synth::savePatch / Synth::loadPatch of synth part PART
//   synthlib init PART                  Synth::initPatch (the Init action), "synthlib engine PART N": Synth::setInstrumentType to the Nth engine of the list
//   fmsave PATH NAME                    newFileManager.saveProject(PATH, NAME): Save As into Projects folder PATH ("-" for the empty path), the
//                                       whole save runs in the main loop, "wait" for it. fmopen PATH NAME: newFileManager.openProject, the
//                                       open the USB console and the project list call (mtff_sim.sh)
//   nna P [MODE]                         the New Note Action of synth part P (0 Release, 1 Continue, 2 Cut, 3 Fade), set when MODE is given: "nna P mode M raw A B"
//                                        (the two stored bytes). nnaraw P A B writes the two bytes as they are (garbage, an old file). structsizes prints sizeof
//   nnapark                              "nnapark N": the Continue notes the sequencer holds parked (not released yet)
//   nnapreempt on|off                    while on, every point inside a park list pop where the audio callback could preempt the main loop (outside the
//                                        interrupt mask) runs the tick net of every track, as the callback would (S-K10)
//   voices                               the voices of the three synth parts as the shim holds them, one line per part:
//                                        "voices P cap C n=N [slot:gate:note:id:fade ...]" (voices that are on, in record order, id 0 = untagged), for synth_nna_sim.sh
//   renderhash [reset]                   "renderhash H0 H1 H2 blocks B0 B1 B2 loud L0 L1 L2": FNV-1a 64 of the bit patterns of every block the shim rendered for each part
//                                        since the last reset, the blocks and how many of them were not silent (the bit exact reference of synth_nna_sim.sh)
//   voicecount P N                       Voices (Polyphony) of part P, the way the editor row sets it
//   midiwatch [on|reset]                 counts the events the virtual MIDI port hands to the synth parts (note on, note off, stop) since "midiwatch reset",
//                                        "midiwatch on N off N stop N all N" (what reaches the shim's onMidi, synth_nna_sim.sh F13)
//   mute T N                             track T muted (N 1) or not (N 0), the project's trackMute / midiTrackMute as the Mute key sets it
//   clockstate                           "clockstate toplay=N pulses=N ticks=N laststop=N": external clock backlog, uncounted pulses, nano ticks played (midi_isr_cap_sim.sh)
//   clockpulses                          "clockpulses N": the external MIDI clock pulses the sequencer counted and has not used yet (synth_play_sim.sh P7)
//   perfpages                            Performance mode and the Synth play page as the firmware holds them, for perform_pages_sim.sh: one line
//                                        "perfpages screen S perfmode P page G open O keep K tracks <16 states> rows <12 selected rows>", S 0 other, 1 Performance, 2 Synth play
//   perffx SLOT                          gives a Performance slot the next FX of the list (perform_pages_sim.sh)
//   synthset K V                         sets page value K (synthplay::Param) to V, for the ones with no column: Input 0, Fixed vel 4 (synth_play_sim.sh P9)
//   synthplay                            the Synth play page as the firmware holds it, for synth_play_sim.sh: one line "synthplay mode M instr I budget B open O confirm C",
//                                        then per part "synthplay part P bend B range R voices V" (bend in semitones as the shim applies it, range = semitones for a full wheel)
//                                        and "synthplay cells <note:state ...>" (the router's table, state 1 held 2 pedal) and "synthplay pedal 0|1"
//   modelrow PART                       stock IMX Model list only: the row the list shows as PART's engine (-1: none, a Missing part) and its length
//   logos                               stock IMX list only (P7): every listed plugin engine and the RAM_G slot of its list / editor logo, -1 not loaded
//                                       ("logos n=N NAME:stored:list:editor ...", UISHOT_TRACE_LOGOS=1 writes it at every change)
//   ptype PART N                         the stored engine number of PART in the project becomes N, the engine stays (the page's pair cap reads the number, synth_play_sim.sh)
//   paircap STORED [N]                   the row of STORED in the page's same engine pair cap table (kPairCap) reads N from now on, N -1 clears the override. Prints "paircap
//                                        stored S cap C" (C = the cap the page reads for that engine). The shipped table is untouched (synth_play_sim.sh AB6)
//   programin                            "programin N": Config > MIDI > Program in (midi_pc_sim.sh)
//   abstate                              the A/B page: "abstate partb B morph M banka A bankb B" then per part "abstate part P gain G vgain V" (the morph factor, the gain
//                                        of the part's first voice that is on, -1 none) and "abstate slots P cur C bank <slots with a file of the engine>" (synth_play_sim.sh)
//   capsim PART [N]                      N: the engine of PART declares N voices (a plugin with max_voices 4), prints "capsim part P declared D cap C" (synth_play_sim.sh P14)
//   govcap                               the voice pool the shim counts: "govcap budget B cap C sounding S cuts N"
//   ext [LINE]                           Tracker+ / Mini extension module (PLATFORM=IMX BRIDGE=1, synth-open/ext_imx.cpp): alone "ext status=S cache clean=C inval=I
//                                        barrier=B pool=P max=M slot=O fx=F" (the sample pool bytes, the project limit, the slot's and the edit
//                                        buffers' byte offsets in the pool, slot -1 outside develop mode), with LINE the module's console reply "ext rc=N reply=TEXT" (ext_imx_rows.sh)
// The same lines run from a ui_shot script as "poke LINE" (ui_shot_main.cpp), at that point of the script.
#if defined(UISHOT_EXT_IMX)
#include "ext_imx.h"
extern int16_t* externalRamEffectsBuff;   // core/source/Tracker.cpp
#endif
#define private public // host only: "clockpulses" reads the external clock pulse counter of the sequencer
#include "mtSequencer.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "Recorder.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"
#include "Synth.h"
#include "open_patch.h"
#include "host_platform.h"
#include "displayControls.h"
#include "synth_order.h"
#include "plug_host.h"
#include "ff_hooks.h"
#include "stereoPairs.h"
#include "fx34.h"
#include "fx34_io.h"
#include "mtFollowChord.h"
#ifdef FEATURE_PARTS
#include "parts.h"
#include "songEditor/songEditor.h"
#endif
#include "mtAudioEngine.h"
#include "partAuto.h"
#include "masterParams/masterParams.h"
#include "sampleEditor/sampleEditor.h"
#include "sampleRecorder/sampleRecorder.h"
extern cSampleRecorder sampleRecorder;
uint32_t hostSeqTimerTicks() { return sequencer.timerTicks; }   // the fake micros clock of "clockcost" (host_platform.cpp)

namespace hostp { extern int g_radioStereo; }
extern cMasterParams masterParams;
#define extMasterParams masterParams
#include "open_load.h"
#include "mtSequencer.h"
#include "synthPlay/synthPlay.h"
#include "synthPlayRouter.h"
#include "MidiAdapter.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/MidiEvent.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <variant>
#define private public // host only: the poke actions reach the editor state the UI keeps private
#include "core/interfacePopups.h"
#include "instrumentEditor/instrumentEditor.h"
#include "core/interface.h"
#include "performanceMode/performanceMode.h"

#ifdef UISHOT_PLUS_SYNTH_DEFAULT
int uishotPlugMpeVoice(int i, int f);   // host_plusproxy.cpp, the plugmpe poke
#endif
// The plus pin (PLATFORM=IMX BRIDGE=1, the plus sim on the stock synth stack, no FEATURE_SYNTH_OPEN): the FF only Synth members, the Synth Play page and the
// open patch bank do not exist there. Their commands print "n/a", the shared ones use these stand-ins
#ifdef FEATURE_SYNTH_OPEN
static const char* uishotStoredName(int st) { return Synth::storedName(st); }
#define UISHOT_SPL_OPEN() synthPlay.isOpen()
#define UISHOT_IS_AUTOMATION(m) ((m) == mtInstEditModeSynthAutomation)
#else
static const char* uishotStoredName(int st)
{
    static char b[16];
    snprintf(b, sizeof b, "%d", st);
    return b;
}
#ifdef FEATURE_SYNTH_PATCHFX
#define UISHOT_SPL_OPEN() synthPlay.isOpen()   // the page is on Tracker+ and Mini too (P12 round 7)
#else
#define UISHOT_SPL_OPEN() false
#endif
#define UISHOT_IS_AUTOMATION(m) false
#endif
#include "performanceMode/performanceEngine.h"
#undef private

// The Model list of the Synth screen as the list control has it (MK66 and IMX): the column label ("Model", or "Edit list" in the edit mode
// of synth order), the selected row and the row texts in order, a hidden engine of the edit mode with its "-" (synth_order_flow rows,
// plus_flows.sh *_order.flow, P12 round 6)
extern "C" void hostPokeLabelText(int ctrl, std::string* out);
static std::string orderLine()
{
    std::string label;
    hostPokeLabelText(instrumentEditor.label[0], &label);
    const strNewListData& d = instrumentEditor.modelListData;
    std::string l = "order label " + label + " start " + std::to_string(d.start) + " n " + std::to_string(d.length) + " rows ";
    for (int i = 0; d.params && i < d.length; i++)
    {
        l += (i ? "|" : "");
        l += d.params[i] ? d.params[i] : "?";
    }
    return l;
}
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
// The Synth play page on Tracker+ / Mini (P12 round 7): "splay open O mode M part P partb B voices V0 V1 V2 vel F bend N cells note:state ..."
// (mode: the sequencer's MIDI in mode, 5 = the page, voices: what the page gave each part, cells: the router's table, 1 held 2 pedal)
static std::string splayLine()
{
    char b[200];
    int at = snprintf(b, sizeof b, "splay open %d mode %d part %d partb %d voices %d %d %d vel %d bend %d cells", (int)synthPlay.isOpen(),
                      (int)sequencer.getMidiInVoiceMode(), (int)synthplay::value[synthplay::kPart], (int)synthplay::value[synthplay::kPartB],
                      synthPlay.voicesOf(0), synthPlay.voicesOf(1), synthPlay.voicesOf(2), (int)synthplay::value[synthplay::kFixedVel],
                      (int)synthplay::value[synthplay::kBend]);
    for (int i = 0; i < synthplay::kCells; i++)
        if (synthplay::cells()[i].state)
            at += snprintf(b + at, sizeof b - at, " %d:%d", synthplay::cells()[i].out, synthplay::cells()[i].state);
    return b;
}
#endif
#ifndef FEATURE_SYNTH_OPEN
int synthModelRow(int part); // SynthEditor.cpp, the stock IMX Model list
#if !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
// The card logos of the stock IMX list (P7): "logos n=N NAME:stored:listSlot:editorSlot ..." (-1: not in RAM_G)
static std::string logosLine()
{
    const synthtypes::List& l = Synth::typeList();
    std::string r;
    int n = 0;
    char b[64];
    for (int ui = 0; ui < l.count(); ui++)
    {
        const synthtypes::Entry& e = l.at(ui);
        if (e.builtin >= 0)
            continue;
        n++;
        snprintf(b, sizeof b, " %s:%d:%d:%d", e.name, e.stored, Synth::cardLogoResident(e.stored, false), Synth::cardLogoResident(e.stored, true));
        r += b;
    }
    return "logos n=" + std::to_string(n) + r;
}
#endif
#endif
namespace polysynth::open { int hostVoices(int part, char* buf, unsigned n); } // open_synth.cpp (ui_shot overlay, gen_overlay.py)
namespace polysynth::open { int hostPart(int part, float* bend, float* range, int* voiceCount); int hostMpeVoices(int part, char* buf, size_t n); }
namespace polysynth::open { int hostCapSim(int part, int maxVoices); }
namespace polysynth::open { int hostGain(int part, float* gain, float* voiceGain); }
int synthPlayPairCapSim(int stored, int cap);   // fSynthPlay.cpp (UI_SHOT_HOST): override one row of the page's pair cap table
extern "C" void hostPokeImporterLabels(std::string* out);
extern "C" void hostPokeImporterPlayEnd();
extern "C" void hostPokeListBar(int ctrl, std::string* out);
extern "C" void hostPokeLabelText(int ctrl, std::string* out);
extern "C" void hostPokeMasterFx(unsigned feedback, float damp, std::string* out);
extern "C" void hostPokeFxMute(std::string* out);
extern "C" void hostPokeFxAuto(std::string* out);
extern "C" void hostPokeFx34(const char* args, std::string* out);

extern "C" void uishotTapReset();
extern "C" int uishotTapDump(char* buf, int cap);
namespace
{
void out(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void out(const char* fmt, ...)
{
    const char* base = getenv("UISHOT_POKE");
    if (!base)
        return;
    std::string path = std::string(base) + ".out";
    FILE* f = fopen(path.c_str(), "a");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// Engine changes of part 0 (model_rest_sim.sh): the stored engine is sampled at every millis() call, a change is one load
int g_loadsSeen = 0;
int g_loadsPrev = -1;
uint32_t g_loadsLastMs = 0, g_loadsMarkMs = 0;

// Screen watch (model_rest_sim.sh): the right-hand side of the Synth screen and the popup, sampled at every millis() call
std::string g_rhsPrev, g_popPrev, g_popFirst;
int g_rhsChanges = 0, g_popCount = 0;
uint32_t g_rhsFirstMs = 0, g_popFirstMs = 0, g_watchMarkMs = 0;
bool g_watching = false;

std::string rhsNow()
{
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    if (part < 0 || part >= kSynthInstrumentsCount)
        return "no part";
    const SynthPartSettings& sp = mtProject.values.synthPart[part];
    char b[512];
    snprintf(b, sizeof b, "engine[%s] maxvoices[%d] name[%s] stored[%s] voices[%d] gain[%.2f] pan[%.0f] rev[%.2f] del[%.2f] labels[%s|%s|%s|%s|%s|%s]",
             uishotStoredName(sp.synthType), instrumentEditor.getSynth()->getMaxVoiceCount(part), sp.patchName,
             instrumentEditor.synthPatchNameText, (int)sp.voiceCount, (double)sp.gainDb, (double)sp.panning, (double)sp.reverbSend,
             (double)sp.delaySend, instrumentEditor.labelText[0], instrumentEditor.labelText[1], instrumentEditor.labelText[2],
             instrumentEditor.labelText[3], instrumentEditor.labelText[4], instrumentEditor.labelText[5]);
    return b;
}

std::string popNow()
{
    if (mtPopups.globalPopupDisplayTime && mtPopups.popupData.multiLineText && mtPopups.popupData.textLinesCount)
        return mtPopups.popupData.multiLineText[0] ? mtPopups.popupData.multiLineText[0] : "";
    return "none";
}

void watchSample()
{
    if (!g_watching)
        return;
    const std::string r = rhsNow(), p = popNow();
    if (r != g_rhsPrev)
    {
        if (!g_rhsChanges)
            g_rhsFirstMs = hostp::g_nowMs;
        g_rhsChanges++;
        g_rhsPrev = r;
    }
    if (p != g_popPrev)
    {
        if (p != "none")
        {
            if (!g_popCount)
            {
                g_popFirstMs = hostp::g_nowMs;
                g_popFirst = p;
            }
            g_popCount++;
        }
        g_popPrev = p;
    }
}

// "inserts T=<ms> 1=<state>:<name>[values] 2=..." the state of both master insert slots now
std::string insertsLine()
{
    static const char* const kState[] = {"empty", "pending", "running", "missing", "failed", "safe", "tripped", "bypassed", "noroom"};
#ifndef FEATURE_PLUG_HOST
    return "inserts T=" + std::to_string((unsigned long)hostp::g_nowMs) + " none";   // no master insert host (plus sim)
#else
    char v[2][160] = {};
    for (int k = 0; k < 2; k++)
        for (uint32_t i = 0, n = plug::paramCount(k), at = 0; i < n && at + 16 < sizeof(v[k]); i++)
            at += unsigned(snprintf(v[k] + at, sizeof(v[k]) - at, "%s%g", i ? "," : "", double(plug::paramValue(k, i))));
    char l[480];
    snprintf(l, sizeof(l), "inserts T=%lu 1=%s:%s[%s] 2=%s:%s[%s]", (unsigned long)hostp::g_nowMs, kState[int(plug::slotState(0))], plug::slotName(0), v[0],
             kState[int(plug::slotState(1))], plug::slotName(1), v[1]);
    return l;
#endif
}

extern "C" int hostTrackPlaying(int track);   // host_poke_fx.cpp
// midiwatch: an observer on the virtual MIDI port (MidiSource::subscribeMidiEvents), it sees what every synth part sees
int g_mwNoteOn = 0, g_mwNoteOff = 0, g_mwStop = 0, g_mwAll = 0;
int g_mwCnt[128] = {};   // note ons per note (the UISHOT_TRACE_FFSTATE read of the IMX round 4 flows)
std::shared_ptr<polysynth::MidiSource::MidiEventCallback> g_mwCb;
void midiWatchOn()
{
    if (g_mwCb)
        return;
    auto* src = static_cast<MidiAdapter*>(instrumentEditor.getSynth()->getVirtualMidiPort());
    if (!src)
        return;
    g_mwCb = std::make_shared<polysynth::MidiSource::MidiEventCallback>([](const polysynth::MidiEvent& e, int) {
        g_mwAll++;
        if (e.type == polysynth::MidiEvent::Type::kNoteOn)
        {
            e.data.note.velocity ? g_mwNoteOn++ : g_mwNoteOff++;
            if (e.data.note.velocity && e.data.note.note < 128)
                g_mwCnt[e.data.note.note]++;
        }
        else if (e.type == polysynth::MidiEvent::Type::kNoteOff)
            g_mwNoteOff++;
        else if (e.type == polysynth::MidiEvent::Type::kStop)
            g_mwStop++;
    });
    src->subscribeMidiEvents(9999, g_mwCb);
}

// renderhash: FNV-1a 64 over the bit patterns of every block the shim renders for a part (gen_overlay.py hooks it into open_synth.cpp)
uint64_t g_rhHash[3] = { 0xcbf29ce484222325ull, 0xcbf29ce484222325ull, 0xcbf29ce484222325ull };
uint32_t g_rhBlocks[3] = {}, g_rhLoud[3] = {};

std::vector<int16_t> g_left, g_right;
char g_name[64];
char g_dir[] = "Recorded";

// The selected parameter of the synth patch editor as its value text: "synthtext pos P id I NAME = TEXT" (Metadata::valueFormatter of the
// patch value, "-" without one), the units the editor draws (P7 polish, plus_flows.sh *_units.flow)
std::string synthtextLine()
{
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    auto& patch = instrumentEditor.getSynth()->getPatch(part);
    const int idx = instrumentEditor.sectionParamsIds[instrumentEditor.paramListPos];
    const auto& md = patch.getMetadata();
    if (idx < 0 || idx >= int(md.params.size()))
        return "synthtext pos " + std::to_string(instrumentEditor.paramListPos) + " id " + std::to_string(idx) + " none";
    const auto& q = md.params[size_t(idx)];
    const float v = patch.getParamValue(idx).value_or(0.0f);
    return "synthtext pos " + std::to_string(instrumentEditor.paramListPos) + " id " + std::to_string(idx) + " " + q.displayName + " = "
           + (q.valueFormatter ? q.valueFormatter(patch, v) : std::string("-"));
}

// The section tiles of the synth patch editor grid: "synthgrid n=N | NAME:LABEL/ICON | ..." (the section name, the grid label and the bitmap
// index, -1: no icon), the macro tiles left out (P7 polish 2, plus_flows.sh *_grid.flow)
std::string synthgridLine()
{
    std::string l = "synthgrid n=" + std::to_string(int(instrumentEditor.firstMacroIdx));
    for (int i = 0; i < int(instrumentEditor.firstMacroIdx); i++)
    {
        const char* t = instrumentEditor.sectionNamesList && instrumentEditor.sectionNamesList[i] ? instrumentEditor.sectionNamesList[i] : "";
        const char* nm = instrumentEditor.sectionNamesBufor ? &instrumentEditor.sectionNamesBufor[cInstrumentEditor::kMaxSynthListStringLen * i] : "";
        l += " | " + std::string(nm) + ":" + std::string(t) + "/" + std::to_string(int(instrumentEditor.sectionImgList[i]));
    }
    return l;
}

std::string synthvalLine()
{
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    auto& patch = instrumentEditor.getSynth()->getPatch(part);
    const int idx = instrumentEditor.sectionParamsIds[instrumentEditor.paramListPos];
    const bool list = instrumentEditor.paramValueMode == instrumentEditor.kList;
    char b[128];
    snprintf(b, sizeof b, "synthval pos %d id %d mode %s list %d norm %.3f", instrumentEditor.paramListPos, idx, list ? "list" : "value",
             instrumentEditor.paramValueListPos, double(patch.getParamValueEditMapped(idx).value_or(-1.0f)));
    return b;
}

#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
// "partauto part P mask M auto PAN,GAIN,REV,DEL now PAN,GAIN,REV,DEL engine E rm R rec C slot0 T:D": the store's destinations of synth part P and the value each part
// param has now with every source added (audioEngine::synthParamNow, what the stock part path applies)
static std::string partautoLine(int p)
{
    if (p < 0 || p >= partauto::kStoreParts)
        return "partauto bad part";
    using SP = audioEngine::SynthParam;
    char l[260];
    snprintf(l, sizeof l, "partauto part %d mask %d auto %.1f,%.1f,%.1f,%.1f now %.1f,%.1f,%.1f,%.1f", p, int(partauto::g_store.mask[p]),
             double(partauto::g_store.v[p][0]), double(partauto::g_store.v[p][1]), double(partauto::g_store.v[p][2]),
             double(partauto::g_store.v[p][3]), double(engine.synthParamNow(p, SP::kPanning)), double(engine.synthParamNow(p, SP::kVolume)),
             double(engine.synthParamNow(p, SP::kReverbSend)), double(engine.synthParamNow(p, SP::kDelaySend)));
    // engine: the mask the audio interrupt took last (0 while the store has one: the interrupt never ran updateSynthAuto), rm / rec: the two
    // early returns of the audio path (Tracker::updateAudioBuffers in render mode, audioEngine::update while recording)
    snprintf(l + strlen(l), sizeof l - strlen(l), " engine %d rm %d rec %d", int(engine.synthAutoNow(p)), int(AudioStream::renderMode),
             int(recorder.isRecordingActive()));
    // slot0 TYPE:DEST: the first Synth Automation slot of the part (partAuto.h, 0 Off 1 LFO 2 Envelope, dest 0 Pan .. 3 Delay, -1 none)
    snprintf(l + strlen(l), sizeof l - strlen(l), " slot0 %d:%d", int(partauto::slot(p, 0).type), int(partauto::slot(p, 0).dest));
    // slots TYPE:DEST:SHAPE:SPEED:AMOUNT:SUSTAIN:ATTACK:DECAY:RELEASE of the part's four slots (the project.mtff round trip, plus_flows.sh *_synthauto_save.flow)
    snprintf(l + strlen(l), sizeof l - strlen(l), " slots");
    for (int k = 0; k < partauto::kSlots; k++)
    {
        const partauto::Slot& r = partauto::slot(p, k);
        snprintf(l + strlen(l), sizeof l - strlen(l), "%s%d:%d:%d:%d:%d:%d:%d:%d:%d", k ? "," : " ", int(r.type), int(r.dest), int(r.shape), int(r.speed),
                 int(r.amount), int(r.sustain), int(r.attack), int(r.decay), int(r.release));
    }
    return l;
}
#endif

// the "at MS LINE" lines of the poke file that wait for the virtual clock
static std::vector<std::pair<unsigned long, std::string>> s_pokeLater;
static std::vector<std::pair<unsigned long, std::string>> s_pokePat;   // "pat MS LINE" before the pattern screen showed

void run(const char* line)
{
    char cmd[32] = {};
    char a[64] = {};
    long n = 0, slot = -1;
    int got = sscanf(line, "%31s %63s %ld %ld", cmd, a, &n, &slot);
    if (got < 1)
        return;
    if (!strcmp(cmd, "lastinstr"))
    {
        mtProject.values.lastUsedInstrument = atoi(a);
        out("lastUsedInstrument = %d", (int)mtProject.values.lastUsedInstrument);
    }
    else if (!strcmp(cmd, "browsegames"))
    {
        out("browseGames() = %d", (int)newFileManager.browseGames());
    }
    else if (!strcmp(cmd, "dumpgames"))
    {
        char** list = nullptr;
        const int cnt = newFileManager.getGamesList(&list);
        out("games %d status %d", cnt, (int)newFileManager.getStatus());
        for (int i = 0; i < cnt; i++)
            out("game[%d] len %zu '%s'", i, strlen(list[i]), list[i]);
    }
    else if (!strcmp(cmd, "savestereo") || !strcmp(cmd, "savemono"))
    {
        const bool stereo = !strcmp(cmd, "savestereo");
        const uint32_t frames = (uint32_t)(got >= 3 ? n : 4096);
        g_left.assign(frames, 0);
        g_right.assign(frames, 0);
        for (uint32_t i = 0; i < frames; i++)
        {
            g_left[i] = (int16_t)(i & 0x7fff);
            g_right[i] = (int16_t)(-(int)(i & 0x7fff));
        }
        snprintf(g_name, sizeof g_name, "%s", a);
        int16_t* r = stereo ? g_right.data() : g_left.data();
        if (got >= 4 && slot >= 0)
        {
            // what the recorder holds when a take ends: both channel buffers and the length
            Recorder::Config cfg;
            cfg.destinationBufferAddress = g_left.data();
            cfg.destinationBufferSize = (uint32_t)(frames * sizeof(int16_t) * 2);
            cfg.destinationChannels = 2;
            cfg.sourceChannels = 2;
            recorder.setConfig(cfg);
            recorder.undo({ g_left.data(), r }, frames);
        }
        const bool started =
          newFileManager.saveRecordedSound(g_dir,
                                           g_name,
                                           (got >= 4 && slot >= 0) ? std::optional<int8_t>((int8_t)slot) : std::nullopt,
                                           g_left.data(),
                                           r,
                                           frames);
        out("%s %s %u frames slot %ld started=%d", cmd, a, frames, slot, (int)started);
    }
    else if (!strcmp(cmd, "importlabels"))
    {
        std::string t;
        hostPokeImporterLabels(&t);
        out("%s", t.c_str());
    }
    else if (!strcmp(cmd, "importplayend"))
    {
        hostPokeImporterPlayEnd();
        out("importplayend done");
    }
    else if (!strcmp(cmd, "masterfx"))
    {   // masterfx FEEDBACK DAMP_PERMILLE: the delay and the reverb after init and after a project load (master_fx_sim.sh)
        std::string t;
        hostPokeMasterFx((unsigned)atoi(a), (float)n / 1000.0f, &t);
        out("%s", t.c_str());
    }
    else if (!strcmp(cmd, "fx34play"))
    {   // fx34play player T | synth P | slots T | custom T TYPE VALUE: what the 4 FX playback did (host_poke_fx34.cpp, fx34_play_sim.sh)
        std::string t;
        hostPokeFx34(strstr(line, "fx34play") + 8, &t);
        out("%s", t.c_str());
    }
    else if (!strcmp(cmd, "fxmute"))
    {   // fxmute: the Mixer's Mute of delay and reverb, the project flags and the gates (fx_mute_gate.h, fx_mute_sim.sh)
        std::string t;
        hostPokeFxMute(&t);
        out("%s", t.c_str());
    }
    else if (!strcmp(cmd, "fxauto"))
    {   // fxauto: the USB 8+3 auto mute (fx_auto_mute.h, umc_automute_sim.sh): the override, the flags, the gates, the Mixer page labels
        std::string t;
        hostPokeFxAuto(&t);
        char b[96];
        snprintf(b, sizeof b, " | labels delay '%s' reverb '%s'", masterParams.mixerLabel[masterParams.delayVolumeScreenPosition],
                 masterParams.mixerLabel[masterParams.reverbVolumeScreenPosition]);
        out("%s%s", t.c_str(), b);
    }
#ifdef FEATURE_SYNTH_OPEN
    else if (!strcmp(cmd, "capsim"))
    {   // capsim PART [N]: N = the engine of PART declares N voices (what a plugin engine with max_voices 4 does): the engine definition and the part's
        // patch metadata. Prints "capsim part P declared D cap C" (C = Synth::getMaxVoiceCount, what Voices may ask for right now, synth_play_sim.sh P14)
        const int part = atoi(a);
        if (part >= 0 && part < kSynthInstrumentsCount)
        {
            auto& patch = instrumentEditor.getSynth()->getPatch(part);
            if (got >= 3 && n >= 1 && n <= 8)
            {
                polysynth::open::hostCapSim(part, int(n));
                const_cast<int&>(patch.getMetadata().maxVoices) = int(n);
            }
            out("capsim part %d declared %d cap %d", part, patch.getMetadata().maxVoices, instrumentEditor.getSynth()->getMaxVoiceCount(part));
        }
        else
            out("capsim: usage capsim PART [N]");
    }
#endif
    else if (!strcmp(cmd, "synthvoices"))
    {   // synthvoices PART N: the voices of synth part PART, as the Synth screen's Voices row sets them
        if (n >= 1 && n <= 4 && atoi(a) >= 0 && atoi(a) < kSynthInstrumentsCount)
        {
            const int part = atoi(a);
            mtProject.values.synthPart[part].voiceCount = uint8_t(n);
            instrumentEditor.getSynth()->setVoiceCount(part, int(n));
            out("synthvoices part %d = %ld", part, n);
        }
        else
            out("synthvoices: usage synthvoices PART 1..4");
    }
#ifdef FEATURE_SYNTH_OPEN // the automation slots are the open stack's (Synth::setModSlot), the stock Tracker+ synth has none
    else if (!strcmp(cmd, "partslot"))
    {   // partslot PART SLOT TYPE CODE AMOUNT SHAPE SPEED ATTACK DECAY SUSTAIN RELEASE: one automation slot of the open patch of a synth part, the way the
        // editor sets it (open::modSetDest on the engine's picker code, Synth::setModSlot). TYPE 0 Off, 1 LFO, 2 Envelope. CODE is the picker code (a param
        // index, 256 + n a macro, 264 + n a part destination: 0 Pan, 1 Gain, 2 Reverb send, 3 Delay send). Times in ms (part_auto_sim.sh)
        int part = -1, sl = -1, type = 0, code = 0, amount = 50, shape = 2, speed = 14, att = 10, dec = 500, sus = 50, rel = 500;
        if (sscanf(line, "%*s %d %d %d %d %d %d %d %d %d %d %d", &part, &sl, &type, &code, &amount, &shape, &speed, &att, &dec, &sus, &rel) < 4 || part < 0 ||
            part >= kSynthInstrumentsCount || sl < 0 || sl >= polysynth::open::kModSlots || type < 0 || type > 2)
        {
            out("partslot: usage partslot PART SLOT TYPE CODE [AMOUNT SHAPE SPEED ATTACK DECAY SUSTAIN RELEASE]");
        }
        else
        {
            polysynth::open::ModSlot m;
            m.type = (polysynth::open::ModType)type;
            m.amount = (int8_t)amount;
            m.shape = (uint8_t)shape;
            m.speed = (uint8_t)speed;
            m.attack = (uint16_t)att;
            m.decay = (uint16_t)dec;
            m.sustain = (uint8_t)sus;
            m.release = (uint16_t)rel;
            const polysynth::open::EngineDef* def = instrumentEditor.getSynth()->getModEngine(part);
            if (type != 0 && def)
            {
                polysynth::open::modSetDest(*def, m, code);
            }
            instrumentEditor.getSynth()->setModSlot(part, sl, m);
            out("partslot part %d slot %d type %d kind %d dest %d", part, sl, type, (int)m.kind, (int)m.dest);
        }
    }
#endif
    else if (!strcmp(cmd, "synthsend"))
    {   // synthsend PART reverb|delay VALUE (0..100): the synth part's own send level, as its MIDI CC (fIinstrEdit.cpp kSynthReverbSend and
        // kSynthDelaySend) sets it: the project value, then the pre FX mixer gain. A part's aux sends reach reverb and delay only through it
        int part = -1;
        char which[16] = {};
        float v = 0.0f;
        if (sscanf(line, "%*s %d %15s %f", &part, which, &v) != 3 || part < 0 || part >= kSynthInstrumentsCount || v < 0.0f || v > 100.0f
            || (strcmp(which, "reverb") && strcmp(which, "delay")))
        {
            out("synthsend: usage synthsend PART reverb|delay 0..100");
        }
        else
        {
            if (!strcmp(which, "reverb"))
            {
                mtProject.values.synthPart[part].reverbSend = roundf(v);
                instrumentEditor.setSynthReverbSend(part, v, false);
            }
            else
            {
                mtProject.values.synthPart[part].delaySend = roundf(v);
                instrumentEditor.setSynthDelaySend(part, v, false);
            }
            out("synthsend part %d %s = %g", part, which, (double)v);
        }
    }
    else if (!strcmp(cmd, "synthfmt"))
    {   // synthfmt PART NAME VALUE: the text the parameter's Metadata::valueFormatter gives VALUE (what the param pages show), "-" without one.
        // On the plus pin the formatter is the closed lib's own code (host_plusproxy.cpp, h2_format)
        int part = -1;
        char nm[48] = {};
        float v = 0.0f;
        if (sscanf(line, "%*s %d %47s %f", &part, nm, &v) != 3 || part < 0 || part >= kSynthInstrumentsCount)
        {
            out("synthfmt: usage synthfmt PART NAME VALUE");
        }
        else
        {
            for (char* c = nm; *c; ++c) *c = *c == '_' ? ' ' : *c;
            auto& patch = instrumentEditor.getSynth()->getPatch(part);
            const auto& md = patch.getMetadata();
            std::string t = "?";
            for (size_t i = 0; i < md.params.size(); i++)
                if (md.params[i].displayName == nm)
                {
                    t = md.params[i].valueFormatter ? md.params[i].valueFormatter(patch, v) : std::string("-");
                    break;
                }
            out("synthfmt part %d %s %g = %s", part, nm, double(v), t.c_str());
        }
    }
    else if (!strcmp(cmd, "synthmod"))
    {   // synthmod PART MACRO [IDX AMOUNT]: the modifiers of macro MACRO (6 = M7 Velocity) of the patch of synth part PART, "synthmod part P
        // macro M n N id:amount ...", after setting modifier IDX's amount to AMOUNT when given (Patch::setMacroModifierAmount, as the M7 page)
        int part = -1, m = -1, idx = -1;
        float amt = 0.0f;
        const int got2 = sscanf(line, "%*s %d %d %d %f", &part, &m, &idx, &amt);
        if (got2 < 2 || part < 0 || part >= kSynthInstrumentsCount || m < 0 || m >= polysynth::kMacroCount)
        {
            out("synthmod: usage synthmod PART MACRO [IDX AMOUNT]");
        }
        else
        {
            auto& patch = instrumentEditor.getSynth()->getPatch(part);
            if (got2 == 4 && idx >= 0) patch.setMacroModifierAmount(m, idx, amt);
            const auto& mods = patch.getMetadata().macros[size_t(m)].modifiers;
            std::string t;
            char b[48];
            for (const auto& mod : mods)
            {
                snprintf(b, sizeof b, " %d:%g", mod.paramId ? *mod.paramId : -1, double(mod.amount));
                t += b;
            }
            out("synthmod part %d macro %d n %zu%s", part, m, mods.size(), t.c_str());
        }
    }
    else if (!strcmp(cmd, "synthparam"))
    {   // synthparam PART NAME VALUE: a parameter of the patch of synth part PART (0..2) as a knob sets it. NAME is the display name with
        // _ for spaces (Chord_Mode). Used by the strum velocity renders (Chord Mode 1 = Para)
        int part = -1;
        char nm[48] = {};
        float v = 0.0f;
        if (sscanf(line, "%*s %d %47s %f", &part, nm, &v) != 3 || part < 0 || part >= kSynthInstrumentsCount)
        {
            out("synthparam: usage synthparam PART NAME VALUE");
        }
        else
        {
            for (char* c = nm; *c; ++c) *c = *c == '_' ? ' ' : *c;
            // NAME@N: the Nth parameter of that name (ACD has two Attack, Decay, Sustain, Release: the filter envelope first, the amp envelope second)
            int nth = 1;
            if (char* at = strchr(nm, '@')) { nth = std::max(1, atoi(at + 1)); *at = 0; }
            auto& patch = instrumentEditor.getSynth()->getPatch(part);
            const auto& md = patch.getMetadata();
            bool found = false;
            for (size_t i = 0; i < md.params.size() && !found; i++)
            {
                if (md.params[i].displayName == nm && --nth == 0)
                {
                    patch.setParamValue(int(i), v, polysynth::Patch::ParamChangeSource::kKnob);
                    found = true;
                    out("synthparam part %d %s = %g (id %zu)", part, nm, (double)v, i);
                }
            }
            if (!found)
            {
                std::string all;
                for (size_t i = 0; i < md.params.size(); i++) all += (i ? "|" : "") + md.params[i].displayName;
                out("synthparam: no parameter %s, the parameters are %s", nm, all.c_str());
            }
        }
    }
    else if (!strcmp(cmd, "seeffects"))
    {   // the effect list of the Sample Editor as it holds it: the row texts, the selected row and whether Apply works for it (septfx_sim.sh)
        auto& d = sampleEditor.effectListData;
        std::string rows;
        for (uint16_t i = 0; i < d.length; i++)
            rows += std::string(i ? "|" : "") + (d.data[i] ? d.data[i] : "(null)");
        out("seeffects rows %u sel %u apply %d [%s]", unsigned(d.length), unsigned(sampleEditor.selectedEffectListIdx), int(sampleEditor.canApplyCurrentEffect()), rows.c_str());
    }
    else if (!strcmp(cmd, "samplestat"))
    {   // samplestat N: the sample of instrument N now: length, peak, sum and an FNV-1a 64 of the samples (septfx_sim.sh)
        const auto& in = mtProject.instrument[atoi(a)];
        uint64_t h = 0xcbf29ce484222325ull;
        int peak = 0;
        long long sum = 0;
        for (uint32_t i = 0; in.sample.address && i < in.sample.length; i++)
        {
            const int v = in.sample.address[i];
            h = (h ^ uint16_t(v)) * 0x100000001b3ull;
            peak = std::max(peak, v < 0 ? -v : v);
            sum += v;
        }
        out("samplestat %d len %u peak %d sum %lld fnv %016llx", atoi(a), unsigned(in.sample.length), peak, sum, (unsigned long long)h);
    }
    else if (!strcmp(cmd, "samplelr"))
    {   // samplelr N: both channels of instrument N: "samplelr N stereo S len L peakL peakR zcL zcR fnvL fnvR" (stereo_edit_sim.sh)
        const int n = atoi(a);
        const auto& in = mtProject.instrument[n];
        const auto& sm = mtProject.sampleMemory[n];
        int peak[2] = { 0, 0 };
        int zc[2] = { 0, 0 };
        uint64_t h[2] = { 0xcbf29ce484222325ull, 0xcbf29ce484222325ull };
        for (int c = 0; c < 2; c++)
        {
            const int16_t* p = sm.address[c];
            for (uint32_t i = 0; p && i < in.sample.length; i++)
            {
                const int v = p[i];
                h[c] = (h[c] ^ uint16_t(v)) * 0x100000001b3ull;
                peak[c] = std::max(peak[c], v < 0 ? -v : v);
                if (i && ((p[i - 1] < 0) != (v < 0)))
                    zc[c]++;
            }
        }
        out("samplelr %d stereo %d len %u peakL %d peakR %d zcL %d zcR %d fnvL %016llx fnvR %016llx", n, int(sm.isStereo), unsigned(in.sample.length),
            peak[0], peak[1], zc[0], zc[1], (unsigned long long)h[0], (unsigned long long)h[1]);
    }
    else if (!strcmp(cmd, "sizes"))
    {
        out("sizeof(strProjectFile)=%zu", sizeof(strProjectFile));
    }
    else if (!strcmp(cmd, "synthval"))
        out("%s", synthvalLine().c_str());
    else if (!strcmp(cmd, "synthtext"))
        out("%s", synthtextLine().c_str());
    else if (!strcmp(cmd, "synthgrid"))
        out("%s", synthgridLine().c_str());
    else if (!strcmp(cmd, "playing"))
        out("playing %d", int(sequencer.isPlay()));
    else if (!strcmp(cmd, "stepnote"))
    {   // "stepnote TRACK STEP NOTE INSTR": a note on a step of the pattern that plays (a flow's setup on the Mini, which has no pads)
        int v[4] = {0, 0, 0, 0};
        if (sscanf(line, "%*s %d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && v[0] >= 0 && v[0] < 8 && v[1] >= 0 && v[1] < 128)
        {
            auto& st = sequencer.getPatternToUI()->track[v[0]].step[v[1]];
            st.note = int8_t(v[2]);
            st.instrument = uint8_t(v[3]);
            out("stepnote %d %d %d %d ok", v[0], v[1], int(st.note), int(st.instrument));
        }
        else
            out("stepnote FAILED");
    }
#ifdef FEATURE_TABLES
    else if (!strcmp(cmd, "table"))
    {   // the table setup of the console's table pokes (ui_shot_main.cpp) from the poke file, so a flow starts on a table (plus_flows.sh
        // *_tables.flow): "table N TIC LOOP LEN", "table row N R NOTE VOL FXTYPE FXVAL" (NOTE -128 empty, VOL 255 empty), "table bind INSTR N"
        int v[6] = {0, 0, 0, 0, 0, 0};
        if (!strcmp(a, "row") && sscanf(line, "%*s %*s %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6 && v[0] >= 1
            && v[0] <= tables::kTables && v[1] >= 0 && v[1] < tables::kRows)
        {
            tables::pool().table[v[0] - 1].row[v[1]] =
              tables::rowSanitize(tables::Row{int8_t(v[2]), uint8_t(v[3]), uint8_t(v[4]), uint8_t(v[5])});
            out("table row %d %d ok", v[0], v[1]);
        }
        else if (!strcmp(a, "bind") && sscanf(line, "%*s %*s %d %d", &v[0], &v[1]) == 2 && v[0] >= 0 && v[0] < tables::kInstruments)
        {
            tables::pool().bind[v[0]] = uint8_t(v[1]);
            out("table bind %d %d ok", v[0], v[1]);
        }
        else if (sscanf(line, "%*s %d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && v[0] >= 1 && v[0] <= tables::kTables)
        {
            tables::Table& tb = tables::pool().table[v[0] - 1];
            tb.tic = tables::clampTic(uint32_t(v[1]));
            tb.loop = uint8_t(v[2] ? 1 : 0);
            tb.len = uint8_t(v[3] < 1 ? 1 : v[3] > tables::kRows ? tables::kRows : v[3]);
            out("table %d ok", v[0]);
        }
        else
            out("table FAILED");
    }
#endif
    else if (!strcmp(cmd, "ffset"))
    {   // the setup of the IMX round 4 flows (plus_flows.sh): ffset stepfx T S SLOT TYPE VAL | tlen T LAST | plen LAST | seed N | duck SRC DEPTH REL THR |
        // sample N MODE [STEPS] (instrument N becomes a 1 s sine, single shot or MODE (the play mode enum), Repitch Steps STEPS)
        int v[6] = {0, 0, 0, 0, 0, 0};
        auto* pt = sequencer.getPatternToUI();
        if (!strcmp(a, "stepfx") && sscanf(line, "%*s %*s %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5 && v[0] >= 0 && v[0] < kAllTracks &&
            v[1] >= 0 && v[1] < 128 && v[2] >= 0 && v[2] < Sequencer::kStepFxSlots)
        {
            pt->track[v[0]].step[v[1]].fx[v[2]].type = uint8_t(v[3]);
            pt->track[v[0]].step[v[1]].fx[v[2]].value = uint8_t(v[4]);
        }
        else if (!strcmp(a, "plen") && sscanf(line, "%*s %*s %d", &v[0]) == 1)
            sequencer.setPatternLastStep(pt, v[0]);
        else if (!strcmp(a, "tlen") && sscanf(line, "%*s %*s %d %d", &v[0], &v[1]) == 2)
            sequencer.setTrackLastStep(pt, v[0], v[1]);
        else if (!strcmp(a, "seed") && sscanf(line, "%*s %*s %d", &v[0]) == 1)
            sequencer.randomSeed = uint8_t(v[0]);
#ifdef FEATURE_SEND_DUCK
        else if (!strcmp(a, "duck") && sscanf(line, "%*s %*s %d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4)
            engine.setSendDuck(DuckParams{uint8_t(v[0]), uint8_t(v[1]), uint8_t(v[2]), uint8_t(v[3])});
#endif
        else if (!strcmp(a, "sample") && sscanf(line, "%*s %*s %d %d %d", &v[0], &v[1], &v[2]) >= 2 && v[0] >= 0 && v[0] < kSampleInstrumentsCount)
        {
            static int16_t tone[44100 * 4];
            for (size_t i = 0; i < sizeof(tone) / sizeof(tone[0]); i++)
                tone[i] = int16_t(12000.0 * std::sin(6.283185307179586 * 441.0 * double(i) / 44100.0));
            auto& in = mtProject.instrument[v[0]];
            in.isActive = 1;
            in.sample.address = tone;
            in.sample.length = 44100;   // 1 s of the tone
            in.playMode = uint8_t(v[1]);
            in.startPoint = 0;
            in.endPoint = 65535;
            in.volume = 100;
            mtProject.sampleMemory[v[0]].address[0] = tone;
            mtProject.sampleMemory[v[0]].address[1] = tone;
            mtProject.sampleMemory[v[0]].isStreamed = false;
            in.wavetableCurrentWindow = (in.wavetableCurrentWindow & 0x00FFFFFFu) | (uint32_t(v[2]) << 24);
        }
        else
        {
            out("ffset FAILED");
            return;
        }
        out("ffset %s ok", a);
    }
#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
    else if (!strcmp(cmd, "partauto"))
    {   // Tracker+ / Mini part destinations (core/AudioEngine/partAuto.h): "partauto P N X" destination N (0 Pan, 1 Gain, 2 Reverb, 3 Delay) of
        // synth part P gets X, "partauto P N off" takes it off, "partauto P" prints the part's line (partautoLine)
        int p = atoi(a);
        char w[16] = {};
        float x = 0.0f;
        int nn = -1;
        // "partauto P slot K TYPE SHAPE SPEED DEST AMOUNT SUSTAIN ATTACK DECAY RELEASE" sets slot K (0..3) of part P (partauto::setSlot, the Synth
        // Automation page's write), "partauto P slot K off" takes it Off with the defaults of a cleared slot
        int k = -1, ty = 0, sh = 2, sp = 6, de = 0, am = 50, su = 50, at = 10, dc = 500, re = 500;
        char slotWord[8] = {};
        if (sscanf(line, "%*s %*s %7s %d", slotWord, &k) == 2 && !strcmp(slotWord, "slot"))
        {
            polysynth::open::ModSlot d;
            partauto::Slot r = {};
            r.kind = polysynth::open::ModKind::kPart;
            r.dest = -1;
            r.shape = d.shape;
            r.speed = d.speed;
            r.amount = d.amount;
            r.sustain = d.sustain;
            r.attack = d.attack;
            r.decay = d.decay;
            r.release = d.release;
            if (sscanf(line, "%*s %*s %*s %*s %d %d %d %d %d %d %d %d %d", &ty, &sh, &sp, &de, &am, &su, &at, &dc, &re) == 9 && ty >= 1 && ty <= 2)
            {
                r.type = polysynth::open::ModType(ty);
                r.shape = uint8_t(sh);
                r.speed = uint8_t(sp);
                r.dest = int16_t(de);
                r.amount = int8_t(am);
                r.sustain = uint8_t(su);
                r.attack = uint16_t(at);
                r.decay = uint16_t(dc);
                r.release = uint16_t(re);
            }
            partauto::setSlot(p, k, r);
        }
        else
        {
            const int got2 = sscanf(line, "%*s %*s %d %15s", &nn, w);
            if (got2 == 2 && !strcmp(w, "off"))
                partauto::clear(p, nn);
            else if (got2 == 2 && sscanf(w, "%f", &x) == 1)
                partauto::set(p, nn, x);
        }
        out("%s", partautoLine(p).c_str());
    }
#endif
#if defined(UISHOT_EXT_IMX)
    else if (!strcmp(cmd, "ext"))
    {   // Tracker+ / Mini extension module (synth-open/ext_imx.cpp): "ext" alone = loader status and the cache steps so far,
        // "ext LINE" = LINE to the module's console command hook: "ext rc=N reply=TEXT"
        const char* rest = strstr(line, "ext") + 3;
        while (*rest == ' ') rest++;
        if (!*rest)
        {
            uint32_t c = 0, i = 0, b = 0;
            ext_imx_sim_ops(&c, &i, &b);
            const uint8_t* slot = ext_imx_slot();
            const long at = slot ? long(slot - reinterpret_cast<const uint8_t*>(externalRamSampleMemory)) : -1L;
            const long fx = long(externalRamEffectsBuff - externalRamSampleMemory) * 2;
            out("ext status=%s cache clean=%u inval=%u barrier=%u pool=%u max=%u slot=%ld fx=%ld", ext_imx_status_short(), unsigned(c),
                unsigned(i), unsigned(b), unsigned(SAMPLE_POOL_BYTES), unsigned(mtProject.max_memory), at, fx);
        }
        else
        {
            static char reply[1024];
            const int32_t rc = ext_imx_command(rest, reply, sizeof reply);
            out("ext rc=%d reply=%s", int(rc), reply);
        }
    }
#endif
    else if (!strcmp(cmd, "saveproj"))
    {
        static char path[] = "Projects";
        char name[PROJECT_NAME_SIZE] = {};
        strncpy(name, a, sizeof(name) - 1);
        out("saveProject(Projects, %s) = %d", name, (int)newFileManager.saveProject(path, name));
    }
    else if (!strcmp(cmd, "recstat"))
    {
#if defined(FEATURE_STEREO_REC) || defined(FEATURE_RADIO_STEREO)
        const int stereoTake = sampleRecorder.isStereoTake() ? 1 : 0;
#else
        const int stereoTake = -1;
#endif
        const bool planes2 = recorder.getAddress(AudioInput::InputChannels::kLeft) != recorder.getAddress(AudioInput::InputChannels::kRight);
        out("recstat radiostereo %d source %d sources %d stereotake %d planes %d len %u size %u inprogress %d", hostp::g_radioStereo,
            (int)sampleRecorder.recorderConfig.source, (int)cSampleRecorder::availableSourceCount(), stereoTake, planes2 ? 2 : 1, (unsigned)recorder.getLength(), (unsigned)recorder.getSize(),
            (int)sampleRecorder.recordInProgressFlag);
    }
    else if (!strcmp(cmd, "recproj"))
    {   // recproj N: the source a project stores, then the project is dirty so the next autosave writes it
        mtProject.values.source = (uint8_t)atoi(a);
        newFileManager.setProjectStructChanged();
        out("recproj %d", (int)mtProject.values.source);
    }
    else if (!strcmp(cmd, "linein"))
    {   // linein N: Config > Line in channel (0 Stereo, 1 Mono Left, 2 Mono Right)
        mtConfig.general.lineInOutType = (uint8_t)atoi(a);
        out("linein %d", (int)mtConfig.general.lineInOutType);
    }
    else if (!strcmp(cmd, "recsource"))
    {   // recsource N: the recorder source (0 line in, 1 radio, 2 mic low gain, 3 mic high gain) before the Sample Recorder opens
        sampleRecorder.recorderConfig.source = (uint8_t)atoi(a);
        out("recsource %d", (int)sampleRecorder.recorderConfig.source);
    }
    else if (!strcmp(cmd, "recfill"))
    {
        const uint32_t frames = (uint32_t)atoi(a);
        int16_t* l = recorder.getAddress(AudioInput::InputChannels::kLeft);
        int16_t* r = recorder.getAddress(AudioInput::InputChannels::kRight);
        for (uint32_t i = 0; l && i < frames; i++)
        {
            l[i] = (int16_t)(i & 0x7fff);
            if (r != l)
                r[i] = (int16_t)(-(int)(i & 0x7fff));
        }
        std::vector<int16_t*> planes{ l };
        if (r != l)
            planes.push_back(r);
        recorder.undo(planes, frames);
        out("recfill %u frames planes %d", (unsigned)frames, (int)planes.size());
    }
    else if (!strcmp(cmd, "recsave"))
    {
        snprintf(g_name, sizeof g_name, "%s", a);
        const bool started = newFileManager.saveRecordedSound(g_dir, g_name, (got >= 3 && n >= 0) ? std::optional<int8_t>((int8_t)n) : std::nullopt,
                                                              recorder.getAddress(AudioInput::InputChannels::kLeft),
                                                              recorder.getAddress(AudioInput::InputChannels::kRight), recorder.getLength());
        out("recsave %s slot %ld started=%d", a, n, (int)started);
    }
    else if (!strcmp(cmd, "status"))
    {
        out("fm status %d operation %d (fmIdle %d, fmSaveRecordedError %d, fmSaveRecordedSoundEnd %d, fmBrowseGamesEnd %d)",
            (int)newFileManager.getStatus(),
            (int)newFileManager.getCurrentOperation(),
            (int)fmIdle,
            (int)fmSaveRecordedError,
            (int)fmSaveRecordedSoundEnd,
            (int)fmBrowseGamesEnd);
    }
    else if (!strcmp(cmd, "picker"))
    {
        const strNewListData& d = instrumentEditor.modelListData;
        std::string rows;
        for (int i = 0; i < d.length; i++)
        {
            rows += i ? "," : "";
            rows += d.params && d.params[i] ? d.params[i] : "?";
        }
        out("picker rows %d cursor %d list %s", (int)d.length, (int)d.start, rows.c_str());
        {
            std::string bar;
            hostPokeListBar(instrumentEditor.modelListControl, &bar);
            out("%s", bar.c_str());
        }
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            const int st = mtProject.values.synthPart[p].synthType;
            out("part %d stored %d engine %s", p, st, uishotStoredName(st));
        }
    }
    else if (!strcmp(cmd, "patchvals"))
    {
        const int part = a[0] ? atoi(a) : 0;
        auto& patch = instrumentEditor.getSynth()->getPatch(part);
        const int n = (int)patch.getMetadata().params.size();
        std::string v;
        char buf[32];
        for (int i = 0; i < n; i++)
        {
            snprintf(buf, sizeof(buf), "%s%.6g", i ? "," : "", (double)patch.getParamValue(i).value_or(0.0f));
            v += buf;
        }
        out("patchvals part %d n %d v %s", part, n, v.c_str());
    }
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    else if (!strcmp(cmd, "slotfill"))
    {   // "slotfill P SLOT T": open slot SLOT of synth part P, set every parameter to min + T/100 * (max - min), save the slot (it joins
        // the Patch FX bank), open slot 0 again. Test setup for the Patch and Morph FX: the slots get values that differ.
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a), sl = int(n), t = int(slot); // (the line is read as cmd a n slot: here P SLOT T)
        const auto r1 = s->selectPatchSlot(part, sl);
        polysynth::Patch& p = s->getPatch(part);
        const auto& m = p.getMetadata();
        for (size_t i = 0; i < m.params.size(); i++)
        {
            const float lo = float(m.params[i].minValue), hi = float(m.params[i].maxValue);
            p.setParamValue(int(i), lo + (hi - lo) * float(t) / 100.0f, polysynth::Patch::ParamChangeSource::kKnob);
        }
        s->setPatchDirty(part);
        const bool saved = s->savePatchToWorkspace(part);
        const auto r2 = s->selectPatchSlot(part, 0);
        out("slotfill part %d slot %d t %d select %d saved %d back %d", part, sl, t, int(r1), int(saved), int(r2));
    }
#endif
    else if (!strcmp(cmd, "heading"))
    {   // "heading": the patch panel label of the Synth screen ("Slot 0:", "Slot 0  Morph 3:" and the patch name)
        std::string l;
        hostPokeLabelText(instrumentEditor.noteLabel, &l);
        out("heading %s", l.c_str());
    }
#ifdef FEATURE_SYNTH_OPEN
    else if (!strcmp(cmd, "rawvals"))
    {   // "rawvals P": the values synth part P plays now (what the Patch and Morph FX wrote), and its Morph target
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a);
        polysynth::Patch& p = s->getPatch(part);
        float v[polysynth::open::kMaxParams];
        auto& op = static_cast<polysynth::open::OpenPatch&>(p);
        op.getValuesRaw(v);
        std::string l;
        char b[24];
        for (int i = 0; i < op.paramCount(); i++)
        {
            snprintf(b, sizeof(b), "%s%.6g", i ? "," : "", double(v[i]));
            l += b;
        }
        out("rawvals part %d slot %d target %d n %d %s", part, s->getPatchSlot(part), s->getMorphTarget(part), op.paramCount(), l.c_str());
    }
#elif defined(FEATURE_SYNTH_PATCHFX)
    else if (!strcmp(cmd, "rawvals"))
    {   // Tracker+ / Mini (P12): "rawvals part P slot S applied A filled MASK n N v0,v1,...": the values synth part P plays now (the stock patch,
        // what the Patch FX wrote), the open slot, the slot the Patch FX left (-1 none) and the bank's rows (bit n: slot n)
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a);
        polysynth::Patch& p = s->getPatch(part);
        const int cnt = int(p.getMetadata().params.size());
        std::string l;
        char b[24];
        for (int i = 0; i < cnt; i++)
        {
            snprintf(b, sizeof(b), "%s%.6g", i ? "," : "", double(p.getParamValue(i).value_or(0.0f)));
            l += b;
        }
        out("rawvals part %d slot %d applied %d filled %x target %d n %d %s", part, s->getPatchSlot(part), s->patchBankApplied(part),
            unsigned(s->patchBankFilled(part)), s->getMorphTarget(part), cnt, l.c_str());
    }
    else if (!strcmp(cmd, "slotsel"))
    {   // Tracker+ / Mini (P12): "slotsel P SLOT": Synth::selectPatchSlot as Shift + Up / Down does it, "slotsel part P slot S rc R name N"
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a);
        const int rc = int(s->selectPatchSlot(part, int(n)));
        out("slotsel part %d slot %d rc %d name %s", part, s->getPatchSlot(part), rc, s->getPatchName(part).c_str());
    }
    else if (!strcmp(cmd, "slotnames"))
    {   // Tracker+ / Mini (P12): "slotnames part P target T open S names 1=<name>|2=...": the slot names and the Morph target of the part
        // as the bank holds them (<P>.names), every slot 1..15 that has a name
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a);
        std::string l;
        for (int slot = 1; slot < Synth::kPatchSlots; slot++)
        {
            const char* nm = s->getSlotName(part, slot);
            if (nm && nm[0])
            {
                l += (l.empty() ? "" : "|") + std::to_string(slot) + "=" + nm;
            }
        }
        out("slotnames part %d target %d open %d names %s", part, s->getMorphTarget(part), s->getPatchSlot(part), l.empty() ? "-" : l.c_str());
    }
    else if (!strcmp(cmd, "morphkinds"))
    {   // Tracker+ / Mini (P12): "morphkinds part P n N kinds K names a|b|...": the Morph kind of every param of the part's engine (l blend,
        // r rounded blend, s switch at 50) and the stock display names
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a);
        const auto& md = s->getPatch(part).getMetadata();
        std::string k, names;
        for (size_t i = 0; i < md.params.size(); i++)
        {
            k += s->morphKind(part, int(i));
            names += (i ? "|" : "") + md.params[i].displayName;
        }
        out("morphkinds part %d n %zu kinds %s names %s", part, md.params.size(), k.c_str(), names.c_str());
    }
    else if (!strcmp(cmd, "vary"))
    {   // Tracker+ / Mini (P12): "vary P PERCENT SEED": Synth::varyPatch of part P at PERCENT with SEED as the timer entropy (F4 of the
        // patch editor), "vary part P percent X changed N"
        Synth* s = instrumentEditor.getSynth();
        const int part = atoi(a);
        s->setVaryPercent(int(n));
        const int c = s->varyPatch(part, uint32_t(slot));
        out("vary part %d percent %d changed %d", part, s->getVaryPercent(), c);
    }
    else if (!strcmp(cmd, "varyundo"))
    {   // Tracker+ / Mini (P12): "varyundo P": Synth::undoVary (Shift + F4), "varyundo part P ok 0|1"
        const int part = atoi(a);
        out("varyundo part %d ok %d", part, int(instrumentEditor.getSynth()->undoVary(part)));
    }
#endif
    else if (!strcmp(cmd, "imode"))
    {   // the page of the instrument editor (synth_order_sim.sh, scenario 12: the Macros and Automation pages of a Missing part stay closed)
        out("imode %s", instrumentEditor.mode == mtInstEditModeSynth          ? "synth"
                        : instrumentEditor.mode == mtInstEditModeSynthMacros  ? "macros"
                        : UISHOT_IS_AUTOMATION(instrumentEditor.mode) ? "automation"
                                                                                 : "other");
    }
    else if (!strcmp(cmd, "rhs"))
    {
        out("rhs %s", rhsNow().c_str());
    }
    else if (!strcmp(cmd, "watch"))
    {
        if (!strcmp(a, "reset"))
        {
            g_watching = true;
            g_rhsChanges = g_popCount = 0;
            g_rhsPrev = rhsNow();
            g_popPrev = popNow();
            g_popFirst.clear();
        }
        else if (!strcmp(a, "mark"))
            g_watchMarkMs = hostp::g_nowMs;
        else
            out("watch rhs=%d first=%d pops=%d first=%d text='%s'", g_rhsChanges,
                g_rhsChanges ? (int)(g_rhsFirstMs - g_watchMarkMs) : -1, g_popCount,
                g_popCount ? (int)(g_popFirstMs - g_watchMarkMs) : -1, g_popFirst.c_str());
    }
    else if (!strcmp(cmd, "loads"))
    {
        if (!strcmp(a, "reset"))
        {
            g_loadsSeen = 0;
            g_loadsPrev = mtProject.values.synthPart[0].synthType;
        }
        else if (!strcmp(a, "mark"))
            g_loadsMarkMs = hostp::g_nowMs;
        else
            out("loads %d last %d", g_loadsSeen, g_loadsSeen ? (int)(g_loadsLastMs - g_loadsMarkMs) : -1);
    }
#if !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
    else if (!strcmp(cmd, "logos"))
        out("%s", logosLine().c_str());
#endif
    else if (!strcmp(cmd, "order"))
    {   // the Model list of the Synth screen as drawn (MK66 and IMX, P12 round 6): "order label L start S n N rows a|-b|c" (orderLine)
        out("%s", orderLine().c_str());
    }
#ifndef FEATURE_SYNTH_OPEN
    else if (!strcmp(cmd, "modelrow"))
    {   // the stock IMX Model list (SynthEditor.cpp): the row it shows as the part's engine, -1 for none (a Missing part), and its length
        const int part = atoi(a);
        out("modelrow part %d row %d of %d", part, ::synthModelRow(part), (int)instrumentEditor.modelListData.length);
    }
#endif
    else if (!strcmp(cmd, "rowpx"))
    {   // the rows the list holds, in pixels of font 1 (the list's font: controlStyleFont1 is fonts[0])
        const strNewListData& d = instrumentEditor.modelListData;
        std::string w;
        for (int i = 0; i < d.length; i++)
            w += (i ? "," : "") + std::to_string(d.params && d.params[i] ? getTextWidth(&fonts[0], d.params[i], 32) : 0);
        out("rowpx clip %d px %s", synthorder::kRowPx, w.c_str());
    }
    else if (!strcmp(cmd, "textw"))
    {
        out("textw %s px %d", a, (int)getTextWidth(&fonts[0], a, 32));
    }
    else if (!strcmp(cmd, "popup"))
    {   // the popup on screen: globalPopupDisplayTime is non zero from show() until hide()
        if (mtPopups.globalPopupDisplayTime && mtPopups.popupData.multiLineText && mtPopups.popupData.textLinesCount)
            out("popup '%s'", mtPopups.popupData.multiLineText[0] ? mtPopups.popupData.multiLineText[0] : "");
        else
            out("popup none");
    }
    else if (!strcmp(cmd, "popuplines"))
    {
        if (mtPopups.globalPopupDisplayTime && mtPopups.popupData.multiLineText && mtPopups.popupData.textLinesCount)
        {
            std::string t = "popuplines";
            for (int i = 0; i < mtPopups.popupData.textLinesCount; i++)
                t += std::string(" '") + (mtPopups.popupData.multiLineText[i] ? mtPopups.popupData.multiLineText[i] : "") + "'";
            out("%s", t.c_str());
        }
        else
            out("popuplines none");
    }
    else if (!strcmp(cmd, "voices"))
    {
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            char b[256];
            if (polysynth::open::hostVoices(p, b, sizeof b) < 0)
                snprintf(b, sizeof b, "no part");
            out("voices %d %s", p, b);
        }
    }
    else if (!strcmp(cmd, "renderhash"))
    {
        if (!strcmp(a, "reset"))
        {
            for (int p = 0; p < 3; p++)
            {
                g_rhHash[p] = 0xcbf29ce484222325ull;
                g_rhBlocks[p] = g_rhLoud[p] = 0;
            }
        }
        else
            out("renderhash %016llx %016llx %016llx blocks %u %u %u loud %u %u %u", (unsigned long long)g_rhHash[0], (unsigned long long)g_rhHash[1],
                (unsigned long long)g_rhHash[2], g_rhBlocks[0], g_rhBlocks[1], g_rhBlocks[2], g_rhLoud[0], g_rhLoud[1], g_rhLoud[2]);
    }
    else if (!strcmp(cmd, "voicecount"))
    {
        const int p = atoi(a);
        if (p < 0 || p >= kSynthInstrumentsCount || got < 3)
            out("voicecount: bad part or count");
        else
        {
            mtProject.values.synthPart[p].voiceCount = (int8_t)n;
            instrumentEditor.getSynth()->setVoiceCount(p, (int)n);
            out("voicecount %d %d", p, (int)n);
        }
    }
    else if (!strcmp(cmd, "nna"))
    {   // nna P [MODE]: the New Note Action of synth part P (0 Release, 1 Continue, 2 Cut, 3 Fade), set when MODE is given. Prints the stored pair
        const int p = atoi(a);
        if (p < 0 || p >= kSynthInstrumentsCount)
            out("nna: bad part");
        else
        {
            SynthPartSettings& sp = mtProject.values.synthPart[p];
            if (got >= 3)
                synthNnaSet(sp, uint8_t(n));
            out("nna %d mode %d raw %d %d", p, int(synthNnaMode(sp)), int(sp.nna), int(sp.nnaCheck));
        }
    }
#if SEQ_NNA // the park list of the open synth and of the Tracker+ / Mini plugin engines
    else if (!strcmp(cmd, "nnapark"))
    {   // nnapark: how many Continue notes the sequencer holds parked: "nnapark N" (S-K rows of synth_nna_sim.sh)
        out("nnapark %d", int(sequencer.nnaParkedCount()));
    }
    else if (!strcmp(cmd, "nnapreempt"))
    {   // nnapreempt on|off: the host stand-in for the audio callback preempting the main loop inside the park list pops (S-K10 of synth_nna_sim.sh)
        sequencer.nnaHostPreempt(!strcmp(a, "on"));
        out("nnapreempt %s", !strcmp(a, "on") ? "on" : "off");
    }
#endif
    else if (!strcmp(cmd, "nnaraw"))
    {   // nnaraw P NNA CHECK: the two stored bytes as they are (garbage, an old file), the reader must make Release of what does not match
        const int p = atoi(a);
        const int chk = int(slot);
        if (p < 0 || p >= kSynthInstrumentsCount || got < 4)
            out("nnaraw: bad args");
        else
        {
            SynthPartSettings& sp = mtProject.values.synthPart[p];
            sp.nna = uint8_t(n);
            sp.nnaCheck = uint8_t(chk);
            out("nnaraw %d mode %d raw %d %d", p, int(synthNnaMode(sp)), int(sp.nna), int(sp.nnaCheck));
        }
    }
    else if (!strcmp(cmd, "structsizes"))
        out("structsizes SynthPartSettings %d strMtValues %d nna_offset %d", int(sizeof(SynthPartSettings)), int(sizeof(strMtValues)), int(offsetof(SynthPartSettings, nna)));
    else if (!strcmp(cmd, "midiwatch"))
    {
        if (!strcmp(a, "on") || !strcmp(a, "reset"))
        {
            midiWatchOn();
            g_mwNoteOn = g_mwNoteOff = g_mwStop = g_mwAll = 0;
        }
        else
            out("midiwatch on %d off %d stop %d all %d", g_mwNoteOn, g_mwNoteOff, g_mwStop, g_mwAll);
    }
    else if (!strcmp(cmd, "mute"))
    {
        const int t = atoi(a);
        if (t < 0 || t >= kAllTracks || got < 3)
            out("mute: bad track");
        else
        {
            if (t < kSampleTracks)
                mtProject.values.trackMute[t] = (int8_t)n;
            else
                mtProject.values.midiTrackMute[t % 8] = (int8_t)n;
            out("mute %d %d", t, (int)n);
        }
    }
    else if (!strcmp(cmd, "clockstate"))   // the external clock backlog and the nano tick count (midi_isr_cap_sim.sh)
        out("clockstate toplay=%d pulses=%d ticks=%u laststop=%u", sequencer.externalClocksToPlay, sequencer.externalClockPulses, (unsigned)sequencer.timerTicks, (unsigned)sequencer.lastStoppedStepPos);
    else if (!strcmp(cmd, "clockpulses"))
        out("clockpulses %d", sequencer.externalClockPulses);   // MIDI clock pulses counted and not yet taken by the tempo follower (synth_play_sim.sh P7)
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    else if (!strcmp(cmd, "synthset"))
    {
        const int k = atoi(a);   // a page value that has no column (Input, Fixed vel), by its synthplay::Param index
        if (got >= 3 && k >= 0 && k < synthplay::kValues)
        {
            synthplay::value[k] = (int8_t)n;
            if (k >= synthplay::kMpeZone)   // the MPE rows reach the parts as the page's encoder does (the zone lets go of what sounds)
                synthplay::applyMpe(k == synthplay::kMpeZone);
        }
    }
#endif
#ifdef UISHOT_PLUS_SYNTH_DEFAULT
    else if (!strcmp(cmd, "plugmpe"))
    {   // plugmpe PART: the plugin engine voices of the part that are on, " note:chan:gate:nbend:press" (plug_parity_ui_sim.sh), as mpevoices
        char b[320] = "";
        size_t n = 0;
        for (int i = 0; i < 32 && n < sizeof b - 32; i++)
            if (uishotPlugMpeVoice(i, 0) && uishotPlugMpeVoice(i, 1) == atoi(a))
                n += size_t(snprintf(b + n, sizeof b - n, " %d:%d:%d:%d:%d", uishotPlugMpeVoice(i, 2), uishotPlugMpeVoice(i, 3), uishotPlugMpeVoice(i, 4),
                                     uishotPlugMpeVoice(i, 5), uishotPlugMpeVoice(i, 6)));
        out("plugmpe %d%s", atoi(a), b);
    }
#endif
#ifdef FEATURE_SYNTH_OPEN
    else if (!strcmp(cmd, "mpevoices"))
    {   // mpevoices PART: the voices of the part that are on, " slot:note:chan:gate:nbend:press:gain" (synth_mpe_sim.sh)
        char b[320];
        if (got >= 2 && polysynth::open::hostMpeVoices(atoi(a), b, sizeof b) >= 0)
            out("mpevoices %d %s", atoi(a), b);
    }
    else if (!strcmp(cmd, "mpestate"))
    {
        out("mpestate zone %d range %d press %d chg %d", (int)synthplay::value[synthplay::kMpeZone], (int)synthplay::value[synthplay::kMpeRange],
            (int)synthplay::value[synthplay::kMpePress], (int)synthplay::mpeChanged);
    }
    else if (!strcmp(cmd, "synthplay"))
    {
        out("synthplay mode %d instr %d budget %d open %d confirm %d", (int)sequencer.getMidiInVoiceMode(), (int)mtProject.values.lastUsedInstrument,
            polysynth::open::voiceBudget(), (int)synthPlay.isOpen(), (int)synthPlay.isConfirming());
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            float bend = 0, range = 0;
            int vc = 0;
            if (polysynth::open::hostPart(p, &bend, &range, &vc) == 0)
                out("synthplay part %d bend %.3f range %.1f voices %d", p, bend, range, vc);
        }
        char b[160];
        int at = snprintf(b, sizeof b, "synthplay cells");
        for (int i = 0; i < synthplay::kCells; i++)
            if (synthplay::cells()[i].state)
                at += snprintf(b + at, sizeof b - at, " %d:%d", synthplay::cells()[i].out, synthplay::cells()[i].state);
        out("%s", b);
        out("synthplay pedal %d", synthplay::pedalDown() ? 1 : 0);
    }
#elif defined(FEATURE_SYNTH_PATCHFX)
    else if (!strcmp(cmd, "synthplay"))
    {   // Tracker+ / Mini (P12 round 7): no voice budget or bend range per part on the stock parts, the voices the page gave each part
        out("%s", splayLine().c_str());
    }
#endif
    else if (!strcmp(cmd, "perffx"))
    {   // perffx SLOT: the slot gets the next FX of the Performance list (a fresh project has empty slots, a pad does nothing on an empty one)
        if (got >= 2 && atoi(a) >= 0 && atoi(a) < 12)
            performance.changeSlotFx((uint8_t)atoi(a), 1);
    }
    else if (!strcmp(cmd, "perfpages"))
    {
        char t[40], r[40];
        for (int i = 0; i < kPerformanceTracks; i++)
            t[i] = char('0' + (performance.getTrackState(uint8_t(i)) & 7));
        t[kPerformanceTracks] = 0;
        for (int i = 0; i < 12; i++)
            r[i] = char('0' + (performance.getSelectedRow(uint8_t(i)) & 7));
        r[12] = 0;
        out("perfpages screen %d perfmode %d page %d open %d keep %d tracks %s rows %s", mtInterface.isPerformanceModeScreenActive() ? 1 : (UISHOT_SPL_OPEN() ? 2 : 0),
            (int)sequencer.isPerformanceMode(), (int)performanceMode.tracksPage, (int)UISHOT_SPL_OPEN(), (int)performanceMode.dontClearPerformanceMode, t, r);
    }
#ifdef FEATURE_SYNTH_OPEN
    else if (!strcmp(cmd, "paircap"))
    {   // paircap STORED [N]: N >= 0 overrides the row, N -1 clears the override, no N only reads
        const int stored = atoi(a);
        out("paircap stored %d cap %d", stored, synthPlayPairCapSim(stored, got >= 3 ? (n < 0 ? -1 : int(n)) : -2));
    }
#endif
    else if (!strcmp(cmd, "ptype"))
    {
        const int p = atoi(a);
        if (got >= 3 && p >= 0 && p < kSynthInstrumentsCount)
            mtProject.values.synthPart[p].synthType = (int32_t)n;
    }
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    else if (!strcmp(cmd, "programin"))
        out("programin %d", (int)mtConfig.midi.programIn);   // Config > MIDI > Program in (midi_pc_sim.sh)
    else if (!strcmp(cmd, "splstate"))
    {   // the page state of the round trip rows (synth_play_sim.sh P15): the part, B, transpose, the selected column and the item each column shows, the sends of the parts
        char sub[kSynthPlayColumns + 1] = {};
        for (int c = 0; c < kSynthPlayColumns; c++)
            sub[c] = char('0' + synthPlay.subItem(c));
        out("splstate part %d partb %d transpose %d voices %d sel %d sub %s", (int)synthplay::value[synthplay::kPart], (int)synthplay::value[synthplay::kPartB],
            (int)synthplay::value[synthplay::kTranspose], (int)synthplay::value[synthplay::kVoices], synthPlay.column(), sub);
        out("splstate bank A %s B %s", synthPlay.columnText(1), synthPlay.columnText(3));   // the Bank A and Bank B columns (PC1 in synth_play_sim.sh)
        for (int p = 0; p < kSynthInstrumentsCount; p++)
            out("splstate send %d delay %d reverb %d", p, (int)mtProject.values.synthPart[p].delaySend, (int)mtProject.values.synthPart[p].reverbSend);
    }
#endif
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    else if (!strcmp(cmd, "abstate"))
    {
        out("abstate partb %d morph %d banka %d bankb %d", (int)synthplay::value[synthplay::kPartB], (int)synthplay::value[synthplay::kMorph],
            (int)synthplay::value[synthplay::kBankA], (int)synthplay::value[synthplay::kBankB]);
#ifdef FEATURE_SYNTH_OPEN
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            float g = 0, vg = 0;
            if (polysynth::open::hostGain(p, &g, &vg) == 0)
                out("abstate part %d gain %.4f vgain %.4f", p, g, vg);
        }
#endif
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            char b[96];
            int at = snprintf(b, sizeof b, "abstate slots %d cur %d bank", p, instrumentEditor.getSynth()->getPatchSlot(p));
#ifdef FEATURE_SYNTH_OPEN
            for (int sl = 0; sl < polysynth::open::kPatchBankSlots; sl++)
                if (polysynth::open::patchBankHas(p, sl))
                    at += snprintf(b + at, sizeof b - at, " %d", sl);
#else   // the IMX bank (Synth::patchBankFilled, bit n: slot n has a file of the part's engine)
            for (int sl = 0; sl < Synth::kPatchSlots; sl++)
                if (instrumentEditor.getSynth()->patchBankFilled(p) >> sl & 1u)
                    at += snprintf(b + at, sizeof b - at, " %d", sl);
#endif
            out("%s", b);
        }
    }
#endif
#ifdef FEATURE_SYNTH_OPEN
    else if (!strcmp(cmd, "govcap"))
    {
        out("govcap budget %d cap %d sounding %d cuts %u", polysynth::open::voiceBudget(), polysynth::open::voiceCap(), polysynth::open::voicesSounding(),
            (unsigned)polysynth::open::governorCuts());
    }
#endif
    else if (!strcmp(cmd, "inserts"))
        out("%s", insertsLine().c_str());
    else if (!strcmp(cmd, "fxslots"))
        out("fxslots %d", fx34::g_on ? int(fx34::kSlots) : 2);   // the FX slots a step has now: 4 with the project switch on (Project Settings > 4 FX Slots), else 2
    else if (!strcmp(cmd, "duck"))
    {
#ifdef FEATURE_SEND_DUCK
        const DuckParams& e = engine.getSendDuck();
        const DuckParams& c = projstore::duck();
        out("duck engine=%u,%u,%u,%u sidecar=%u,%u,%u,%u gain=%.4f page=%s place=%u", e.source, e.depth, e.release, e.thresh, c.source, c.depth, c.release,
            c.thresh, engine.getSendDuckGain(), !extMasterParams.isLimiterScreen ? "none" : (extMasterParams.duckPage ? "duck" : "limiter"),
            (unsigned)extMasterParams.selectedPlaceLimiter);
#else
        out("duck: not in this build");
#endif
    }
#ifdef FEATURE_PLUG_HOST   // plugin host commands (project.mtff, inserts): not in the plus sim (stock Tracker+ CE)
    else if (!strcmp(cmd, "projsave"))
        out("projsave %s", ffHookWorkspaceSave(cWorkspacePath) ? "ok" : "FAILED");   // both sidecars, as the Workspace save
    else if (!strcmp(cmd, "projload"))
    {   // the call that ends a project load (WorkspaceProjectLoad.cpp): the sidecar of the Workspace is read again, both inserts are
        // unloaded and queued. The synth parts and the audio are not touched, so this isolates the insert side of a load
        ffHookProjectLoaded(cWorkspacePath, false);   // project.mtff and plugins.txt, as WorkspaceProjectLoad.cpp
        out("projload T=%lu", (unsigned long)hostp::g_nowMs);
    }
#endif
    else if (!strcmp(cmd, "tap"))
    {   // the calls the real audio chain made to the master insert twins (host_tap.cpp): tap reset, tap dump (plug_tempo_sim.sh)
        if (!strcmp(a, "reset"))
            uishotTapReset();
        else
        {
            std::string b(size_t(1) << 21, '\0');
            b.resize(size_t(uishotTapDump(&b[0], int(b.size()))));
            if (!b.empty() && b.back() == '\n') b.pop_back();
            out("tap lines follow\n%s", b.c_str());
        }
    }
#ifdef FEATURE_PLUG_HOST   // plugin host commands (project.mtff, inserts): not in the plus sim (stock Tracker+ CE)
    else if (!strcmp(cmd, "mtffver"))
        out("mtffver %d %lx", projstore::formatVersion(), (unsigned long)projstore::writerVersion());
    else if (!strcmp(cmd, "macromode"))
    {
        if (got >= 2) projstore::setMacroMode(atoi(a));
        out("macromode %d", projstore::macroMode());
    }
#endif
    else if (!strcmp(cmd, "patchmode"))
    {
        const int part = atoi(a);
        if (part >= 0 && part < kSynthInstrumentsCount)
            out("patchmode part %d %s", part, static_cast<polysynth::open::OpenPatch&>(instrumentEditor.getSynth()->getPatch(part)).macroScale() ? "scale" : "add");
        else
            out("patchmode: usage patchmode PART");
    }
    else if (!strcmp(cmd, "synthlib"))
    {
        char verb[16] = {}, name[64] = {};
        int part = -1, idx = 0;
        sscanf(line, "%*s %15s %d %63s", verb, &part, name);
        Synth* sy = instrumentEditor.getSynth();
        if (part < 0 || part >= kSynthInstrumentsCount)
            out("synthlib: usage synthlib save|load|init|engine PART [NAME|N]");
        else if (!strcmp(verb, "save"))
            out("synthlib save part %d %s rc=%d", part, name, int(sy->savePatch(part, name)));
        else if (!strcmp(verb, "load"))
            out("synthlib load part %d %s rc=%d", part, name, int(sy->loadPatch(part, name)));
        else if (!strcmp(verb, "init"))
        {
            sy->initPatch(part);
            out("synthlib init part %d", part);
        }
        else if (!strcmp(verb, "engine") && sscanf(name, "%d", &idx) == 1)
        {
#ifdef FEATURE_SYNTH_OPEN
            sy->setInstrumentType(part, Synth::synthTypeAt(idx));
#else
            sy->setInstrumentType(part, Synth::kSynthTypes[size_t(idx) % Synth::kSynthTypes.size()]);
#endif
            out("synthlib engine part %d %d", part, idx);
        }
        else
            out("synthlib: usage synthlib save|load|init|engine PART [NAME|N]");
    }
    else if (!strcmp(cmd, "mtff"))
    {   // the settings project.mtff carries (core/FileManager/projectSidecar.cpp), set and read straight on the core objects: no plugin host, no
        // Project Settings page, so it also runs in the plus sim (P3, plus_mtff_sim.sh). mtff seed N | groove T V | follow T MODE SRC | fx4 0|1 |
        // pairs MASK | strum N | dirty | (no op: only the answer)
        char op[16] = {};
        int a1 = 0, a2 = 0, a3 = 0;
        sscanf(line, "%*s %15s %d %d %d", op, &a1, &a2, &a3);
        if (!strcmp(op, "seed")) sequencer.randomSeed = uint8_t(a1);
        else if (!strcmp(op, "groove") && a1 >= 0 && a1 < 16) sequencer.setTrackGroove(uint8_t(a1), a2);
        else if (!strcmp(op, "follow") && a1 >= 0 && a1 < followchord::kTracks)
        {
            followchord::setting.mode[a1] = uint8_t(a2);
            followchord::setting.source[a1] = uint8_t(a3);
            followchord::recalc();
        }
        else if (!strcmp(op, "fx4")) fx34io::applyProjectSwitch(a1 != 0);
        else if (!strcmp(op, "pairs")) engine.setStereoPairs(uint8_t(a1));
        else if (!strcmp(op, "strum")) projstore::setStrumRampDepth(a1);
        if (op[0] && strcmp(op, "get")) newFileManager.setProjectStructChanged();   // the Workspace save writes the file
        std::string g, f;
        for (int t = 0; t < 16; t++)
        {
            g += (t ? "," : "") + std::to_string(sequencer.getTrackGroove(uint8_t(t)));
            f += (t ? "," : "") + std::to_string(followchord::setting.mode[t]) + ":" + std::to_string(followchord::setting.source[t]);
        }
        out("mtff seed %d fx4 %d pairs %d strum %d groove %s follow %s", int(sequencer.randomSeed), fx34::enabled() ? 1 : 0, int(stereopairs::g_mask),
            projstore::strumRampDepth(), g.c_str(), f.c_str());
    }
#ifdef FEATURE_PLUG_HOST   // plugin host commands (project.mtff, inserts): not in the plus sim (stock Tracker+ CE)
    else if (!strcmp(cmd, "pairs"))
    {   // the linked stereo pairs (stereoPairs.h): the engine's mask and what the project load decided for the stereo .pti files
        out("pairs %d stereo %d notice %d fx %d", int(stereopairs::g_mask), newFileManager.getSampleMode() == FileManagerOperation::kLeftAndRight ? 1 : 0, int(stereopairs::g_linkNotice), stereopairs::g_linkNoticeFx ? 1 : 0);
    }
    else if (!strcmp(cmd, "strumdepth"))
    {
        if (got >= 2) projstore::setStrumRampDepth(atoi(a));
        out("strumdepth %d", projstore::strumRampDepth());
    }
#endif
    else if (!strcmp(cmd, "fmsave") || !strcmp(cmd, "fmopen"))
    {
        char path[300] = {}, name[64] = {};
        sscanf(line, "%*s %299s %63s", path, name);
        if (!strcmp(path, "-")) path[0] = 0;
        newFileManager.clearStatus();   // a save that ended (fmSaveEnd) leaves the status for the screen that showed it to take, no screen does in a poke run
        const bool ok = !strcmp(cmd, "fmsave") ? newFileManager.saveProject(path, name) : newFileManager.openProject(path, name);
        out("%s '%s' '%s' started=%d", cmd, path, name, int(ok));
    }
#ifdef FEATURE_PLUG_HOST   // plugin host commands (project.mtff, inserts): not in the plus sim (stock Tracker+ CE)
    else if (!strcmp(cmd, "sidecar"))
    {   // the project hooks (ff_hooks.h: project.mtff and plug_host.cpp plugins.txt) with the folder paths the file manager hands them (long, nested ones included)
        char op[16] = {}, d1[512] = {}, d2[512] = {};
        sscanf(line, "%*s %15s %511s %511s", op, d1, d2);
        bool rc = false;
        if (!strcmp(op, "open"))
            rc = ffHookProjectOpen(d1, d2);
        else if (!strcmp(op, "save"))
            rc = ffHookProjectSaveAs(d1, d2);
        else if (!strcmp(op, "wssave"))
            rc = ffHookWorkspaceSave(d1);
        else
        {
            out("sidecar: usage sidecar open|save|wssave DIR [DIR]");
            return;
        }
        out("sidecar %s rc=%d", op, int(rc));
    }
#endif
    else if (!strcmp(cmd, "paramid") || !strcmp(cmd, "macroid"))
    {
        int* ids = !strcmp(cmd, "paramid") ? instrumentEditor.sectionParamsIds : instrumentEditor.sectionIds;
        const int pos = atoi(a);
        if (got < 3 || ids == nullptr || pos < 0 || pos >= 16)
            out("%s: no list or bad position", cmd);
        else
        {
            ids[pos] = (int)n;
            out("%s[%d] = %ld", cmd, pos, n);
        }
    }
    else
        out("unknown command '%s'", cmd);
}
} // namespace

extern "C" void hostPokeRun(const char* line)
{
    run(line);
}

extern "C" void hostPokePoll()
{
    watchSample();
    {
        const int t = mtProject.values.synthPart[0].synthType;
        if (g_loadsPrev >= 0 && t != g_loadsPrev)
        {
            g_loadsSeen++;
            g_loadsLastMs = hostp::g_nowMs;
        }
        g_loadsPrev = t;
    }
    // UISHOT_TRACE_INSERTS=1: every change of a slot's state (or of what it holds) is written to $UISHOT_POKE.out with the virtual clock,
    // from the first millisecond, so the load order of a power on can be read (insert_defer_sim.sh)
    static const bool trace = getenv("UISHOT_TRACE_INSERTS") != nullptr;
    if (trace)
    {
        static std::string prev;
        std::string l = insertsLine();
        const std::string key = l.substr(l.find(" 1="));
        if (key != prev)
        {
            prev = key;
            out("%s", l.c_str());
        }
    }
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    // UISHOT_TRACE_MORPH=1 (Tracker+ / Mini, P12): every change of the Morph target of the synth part the UI has open or of the patch panel
    // heading, "morph part P target T heading H" (plus_flows.sh *_morphtarget.flow). The pair: the target changes inside the key handler
    // (the names file write runs the clock) before the heading is drawn
    static const bool traceMorph = getenv("UISHOT_TRACE_MORPH") != nullptr;
    if (traceMorph && mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX
        && mtProject.values.lastUsedInstrument < SYNTH_INSTRUMENTS_START_IDX + kSynthInstrumentsCount)
    {
        static std::string prevMorph;
        const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        const int to = instrumentEditor.getSynth()->getMorphTarget(part);
        std::string h;
        hostPokeLabelText(instrumentEditor.noteLabel, &h);
        char l[160];
        snprintf(l, sizeof(l), "morph part %d target %d heading %s", part, to, h.c_str());
        if (prevMorph != l)
        {
            prevMorph = l;
            out("%s", l);
        }
    }
#endif
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    // UISHOT_TRACE_SPLAY=1 (Tracker+ / Mini, P12 round 7): every change of the Synth play page state (splayLine), plus_flows.sh *_splay.flow
    static const bool traceSplay = getenv("UISHOT_TRACE_SPLAY") != nullptr;
    if (traceSplay)
    {
        static std::string prevSplay;
        const std::string l = splayLine();
        if (l != prevSplay)
        {
            prevSplay = l;
            out("%s", l.c_str());
        }
    }
#endif
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    // UISHOT_TRACE_VARY=1 (Tracker+ / Mini, P12): every change of the Vary amount or of the values of the synth part the UI has open,
    // "vary part P percent X values H" with H a hash of the value bits (plus_flows.sh *_vary.flow: H goes away and comes back)
    static const bool traceVary = getenv("UISHOT_TRACE_VARY") != nullptr;
    if (traceVary && mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX
        && mtProject.values.lastUsedInstrument < SYNTH_INSTRUMENTS_START_IDX + kSynthInstrumentsCount)
    {
        static std::string prevVary;
        const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        Synth* s = instrumentEditor.getSynth();
        const polysynth::Patch& p = s->getPatch(part);
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < p.getMetadata().params.size(); i++)
        {
            const float v = p.getParamValue(int(i)).value_or(-1.0f);
            uint32_t b;
            memcpy(&b, &v, sizeof(b));
            h = (h ^ b) * 16777619u;
        }
        char l[96];
        snprintf(l, sizeof(l), "vary part %d percent %d values %08x", part, s->getVaryPercent(), unsigned(h));
        if (prevVary != l)
        {
            prevVary = l;
            out("%s", l);
        }
    }
#endif
    // UISHOT_TRACE_ORDER=1 (P12 round 6): every change of the Model list as drawn (orderLine), plus_flows.sh *_order.flow
    static const bool traceOrder = getenv("UISHOT_TRACE_ORDER") != nullptr;
    if (traceOrder)
    {
        static std::string prevOrder;
        const std::string l = orderLine();
        if (l != prevOrder)
        {
            prevOrder = l;
            out("%s", l.c_str());
        }
    }
#if !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
    // UISHOT_TRACE_LOGOS=1: every change of the card logo slots of the stock IMX list (plus_flows.sh *_logos.flow)
    static const bool traceLogos = getenv("UISHOT_TRACE_LOGOS") != nullptr;
    if (traceLogos)
    {
        static std::string prev;
        std::string l = logosLine();
        if (l != prev)
        {
            prev = l;
            out("%s T=%lu", l.c_str(), (unsigned long)hostp::g_nowMs);
        }
    }
#endif
    // UISHOT_TRACE_PLAY=1: every change of sequencer.isPlay(), "playing 0|1 T=<virtual ms>" (plus_flows.sh *_playlock.flow)
    static const bool tracePlay = getenv("UISHOT_TRACE_PLAY") != nullptr;
    if (tracePlay)
    {
        static int prev = 0;
        const int p = int(sequencer.isPlay());
        if (p != prev)
        {
            prev = p;
            out("playing %d T=%lu", p, (unsigned long)hostp::g_nowMs);
        }
    }
    // UISHOT_TRACE_SYNTHVAL=1: every change of the synth patch editor's selected parameter or its value (the synthval line) while the
    // editor is open on a synth part (plus_flows.sh *_patch.flow)
    static const bool traceVal = getenv("UISHOT_TRACE_SYNTHVAL") != nullptr;
    if (traceVal && instrumentEditor.mode == mtInstEditModeSynthPatchEdit && instrumentEditor.sectionParamsIds != nullptr
        && mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX && mtInterface.isInstrumentParamsScreenActive())
    {
        static std::string prev;
        std::string l = synthvalLine();
        if (l != prev)
        {
            prev = l;
            out("%s", l.c_str());
        }
    }
    // UISHOT_TRACE_SYNTHTEXT=1: the same for the synthtext line (the value text of the selected parameter, plus_flows.sh *_units.flow)
    static const bool traceText = getenv("UISHOT_TRACE_SYNTHTEXT") != nullptr;
    if (traceText && instrumentEditor.mode == mtInstEditModeSynthPatchEdit && instrumentEditor.sectionParamsIds != nullptr
        && mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX && mtInterface.isInstrumentParamsScreenActive())
    {
        static std::string prev;
        std::string l = synthtextLine();
        if (l != prev)
        {
            prev = l;
            out("%s", l.c_str());
        }
    }
    // UISHOT_TRACE_SYNTHGRID=1: every change of the patch editor's section tiles (the synthgrid line, plus_flows.sh *_grid.flow)
    static const bool traceGrid = getenv("UISHOT_TRACE_SYNTHGRID") != nullptr;
    if (traceGrid && instrumentEditor.mode == mtInstEditModeSynthPatchEdit && instrumentEditor.sectionNamesList != nullptr
        && mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX && mtInterface.isInstrumentParamsScreenActive())
    {
        static std::string prev;
        std::string l = synthgridLine();
        if (l != prev)
        {
            prev = l;
            out("%s", l.c_str());
        }
    }
    // UISHOT_TRACE_FOLLOW=1: every change of a track's Follow chord mode or chord track (plus_flows.sh *_follow.flow)
    static const bool traceFollow = getenv("UISHOT_TRACE_FOLLOW") != nullptr;
    if (traceFollow)
    {
        static followchord::Setting prev = {};
        for (int t = 0; t < followchord::kTracks; t++)
            if (followchord::setting.mode[t] != prev.mode[t] || followchord::setting.source[t] != prev.source[t])
            {
                prev.mode[t] = followchord::setting.mode[t];
                prev.source[t] = followchord::setting.source[t];
                out("follow track %d mode %d src %d T=%lu", t, int(prev.mode[t]), int(prev.source[t]), (unsigned long)hostp::g_nowMs);
            }
    }
#ifdef FEATURE_PARTS
    // UISHOT_TRACE_PARTS=1: every change of the Parts list size, the selected Part, the view, Build, the list mode or the playing Part
    // (plus_flows.sh *_parts.flow)
    static const bool traceParts = getenv("UISHOT_TRACE_PARTS") != nullptr;
    if (traceParts)
    {
        static std::string prev;
        char l[96];
        snprintf(l, sizeof l, "parts count %d sel %d view %d build %d mode %d play %d", int(parts::list().count), partsSelected(),
                 int(songEditor.partsView), int(songEditor.editMode), int(parts::list().mode), parts::currentPart());
        if (prev != l)
        {
            prev = l;
            out("%s T=%lu", l, (unsigned long)hostp::g_nowMs);
        }
    }
#endif
#ifdef FEATURE_TABLES
    // UISHOT_TRACE_TABLES=1: every change of the table runners (the mask, and table/row/instrument of tracks 0 and 1), plus_flows.sh
    // *_tables.flow: "tablerun mask M 0=TABLE/ROW/INSTR 1=TABLE/ROW/INSTR T=<ms>"
    static const bool traceTables = getenv("UISHOT_TRACE_TABLES") != nullptr;
    if (traceTables)
    {
        static std::string prev;
        char l[96];
        snprintf(l, sizeof l, "tablerun mask %04x 0=%d/%d/%d 1=%d/%d/%d", unsigned(tables::g_runMask), int(tables::run(0).table),
                 int(tables::run(0).row), int(tables::run(0).instr), int(tables::run(1).table), int(tables::run(1).row), int(tables::run(1).instr));
        if (prev != l)
        {
            prev = l;
            out("%s T=%lu", l, (unsigned long)hostp::g_nowMs);
        }
    }
#endif
#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
    // UISHOT_TRACE_PARTAUTO=1: every change of synth part 0's destinations or of the values its Pan, Gain and sends have now (plus_flows.sh
    // *_partauto.flow)
    static const bool tracePartAuto = getenv("UISHOT_TRACE_PARTAUTO") != nullptr;
    if (tracePartAuto)
    {
        static std::string prev;
        static unsigned n = 0;
        static const bool every = getenv("UISHOT_TRACE_PARTAUTO")[0] == '2';   // =2: also every 100th pass (timing of the audio side)
        const std::string l = partautoLine(0);
        if (prev != l || (every && ++n % 100 == 0))
        {
            prev = l;
            out("%s T=%lu", l.c_str(), (unsigned long)hostp::g_nowMs);
        }
    }
#endif
    // UISHOT_TRACE_FFSTATE=1 (IMX round 4 flows): "ffstate pl P seed S tlen A,B,C notes N:C,.. fx0 T:V,T:V duck SRC,DEPTH pm P rs R T=ms" at every change: the
    // random seed, the last step of tracks 1 to 3, the note ons the synth parts saw per note, the FX of step 1 of track 1, the send ducking, the play mode
    // and Repitch Steps of instrument 1
    static const bool traceFf = getenv("UISHOT_TRACE_FFSTATE") != nullptr;
    if (traceFf && (g_mwCb || mtInterface.isPatternScreenActive()))   // the synth is not up during the init (getSynth() is null then)
    {
        static std::string prev;
        midiWatchOn();
        auto* pt = sequencer.getPatternToUI();
        char l[320];
        int n = snprintf(l, sizeof l, "ffstate pl %d seed %d tlen %d,%d,%d notes", int(sequencer.isPlay()), int(sequencer.randomSeed), int(Sequencer::getTrackLastStep(pt, 0)),
                         int(Sequencer::getTrackLastStep(pt, 1)), int(Sequencer::getTrackLastStep(pt, 2)));
        bool any = false;
        for (int k = 0; k < 128 && n < 200; k++)
            if (g_mwCnt[k])
            {
                n += snprintf(l + n, sizeof l - size_t(n), "%s%d:%d", any ? "," : " ", k, g_mwCnt[k]);
                any = true;
            }
        if (!any)
            n += snprintf(l + n, sizeof l - size_t(n), " -");
        const auto& st = pt->track[0].step[0];
        n += snprintf(l + n, sizeof l - size_t(n), " fx0 %d:%d,%d:%d", int(st.fx[0].type), int(st.fx[0].value), int(st.fx[1].type), int(st.fx[1].value));
#ifdef FEATURE_SEND_DUCK
        const DuckParams& dk = engine.getSendDuck();
        static int dgMin = 1000;   // lowest send duck gain seen so far in permille (1000 = never ducked)
        const int dgNow = int(engine.getSendDuckGain() * 1000.0f + 0.5f);
        dgMin = dgNow < dgMin ? dgNow : dgMin;
        n += snprintf(l + n, sizeof l - size_t(n), " duck %d,%d dgmin %d", int(dk.source), int(dk.depth), dgMin);
#endif
        const auto& in0 = mtProject.instrument[0];
        static int starts[2] = {}, was[2] = {};
        for (int t = 0; t < 2; t++)
        {
            const int pl = hostTrackPlaying(t);
            starts[t] += pl && !was[t];
            was[t] = pl;
        }
        n += snprintf(l + n, sizeof l - size_t(n), " pm %d rs %d starts %d,%d play %d,%d", int(in0.playMode), int(in0.repitchSteps()), starts[0], starts[1],
                      was[0], was[1]);
        if (prev != l)
        {
            prev = l;
            out("%s T=%lu", l, (unsigned long)hostp::g_nowMs);
        }
    }
    // UISHOT_TRACE_FEATURES=1: every change of Config > Features > Play stop lock (plus_flows.sh *_features.flow)
    static const bool traceFeat = getenv("UISHOT_TRACE_FEATURES") != nullptr;
    if (traceFeat)
    {
        static int prev = 0;
        const int v = int(interfaceGlobals.playStopLock);
        if (v != prev)
        {
            prev = v;
            out("playstoplock %d T=%lu", v, (unsigned long)hostp::g_nowMs);
        }
    }
    if (!s_pokePat.empty() && mtInterface.isPatternScreenActive())
    {
        for (auto& e : s_pokePat)
            s_pokeLater.push_back({hostp::g_nowMs + e.first, e.second});
        s_pokePat.clear();
    }
    for (size_t k = 0; k < s_pokeLater.size();)
    {
        if (hostp::g_nowMs >= s_pokeLater[k].first)
        {
            const std::string l = s_pokeLater[k].second;
            s_pokeLater.erase(s_pokeLater.begin() + long(k));
            run(l.c_str());
        }
        else
            k++;
    }
    static const char* path = getenv("UISHOT_POKE");
    static unsigned calls = 0;
    if (!path || (++calls & 255) != 0)
        return;
    struct stat st;
    if (stat(path, &st) != 0)
        return;
    FILE* f = fopen(path, "r");
    if (!f)
        return;
    std::vector<std::string> lines;
    char buf[256];
    while (fgets(buf, sizeof buf, f))
        lines.push_back(buf);
    fclose(f);
    remove(path);
    for (auto& l : lines)
    {
        unsigned long at = 0;
        int used = 0;
        if (sscanf(l.c_str(), "at %lu %n", &at, &used) == 1 && used > 0)
            s_pokeLater.push_back({at, l.substr(size_t(used))});   // "at MS LINE": LINE once the virtual clock reaches MS
        else if (sscanf(l.c_str(), "pat %lu %n", &at, &used) == 1 && used > 0)
            s_pokePat.push_back({at, l.substr(size_t(used))});     // "pat MS LINE": MS after the pattern screen first showed (the boot project load is over)
        else
            run(l.c_str());
    }
}

extern "C" void uishotHashBlock(int part, const float* l, const float* r)
{
    if (part < 0 || part > 2)
        return;
    uint64_t h = g_rhHash[part];
    bool loud = false;
    for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < 128; i++)
        {
            uint32_t b;
            memcpy(&b, (ch ? r : l) + i, 4);
            loud |= (b << 1) != 0;
            for (int k = 0; k < 4; k++)
            {
                h ^= (b >> (8 * k)) & 0xff;
                h *= 0x100000001b3ull;
            }
        }
    g_rhHash[part] = h;
    g_rhBlocks[part]++;
    g_rhLoud[part] += loud;
}
