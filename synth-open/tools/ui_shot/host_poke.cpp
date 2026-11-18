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
//   projload                             plug::projectLoaded(Workspace): what the end of a project load does to the inserts (insert_defer_sim.sh)
//   duck                                 send ducking: "duck engine=SRC,DEPTH,RELEASE,THRESH sidecar=SRC,DEPTH,RELEASE,THRESH gain=G page=P place=N" (the engine's settings and
//                                        gain, the plug host's copy that the sidecar is written from, the master screen's page: none, limiter or duck) for duck_sim.sh
//   projsave                             plug::projectSave(Workspace): the Workspace save writes (or removes) the sidecar, "projsave ok" or "projsave FAILED"
//   sidecar open SRCDIR DSTDIR          plug::sidecarOpen: the project folder's plugins.txt to the Workspace (project open), "sidecar open rc=0|1"
//   sidecar save WSDIR PROJDIR          plug::sidecarToProject: the Workspace file to the project folder (project save), "sidecar save rc=0|1"
//   sidecar wssave DIR                  plug::projectSave: the Workspace save of the sidecar in DIR, "sidecar wssave rc=0|1" (sidecar_path_sim.sh)
//   mtffver                             the header of the project.mtff the last project load read: "mtffver <format> <writer as hex (major<<16|minor<<8|patch), 0 = unknown>" (plug::mtffFormatVersion, plug::mtffWriterVersion, mtff_sim.sh)
//   strumdepth [N]                      the project's Strum ramp depth (plug::strumRampDepth), set to N (0..100) when given: "strumdepth N" (mtff_sim.sh)
//   macromode [N]                       the project's macro depth mode (plug::macroMode: 0 per patch, 1 Add, 2 Scale), set to N when given: "macromode N" (projset_sim.sh)
//   patchmode PART                      "patchmode part P scale|add": the macro depth mode of the patch of synth part P (OpenPatch::macroScale)
//   synthlib save|load PART NAME        the patch library (Patches folder) as the Synth screen uses it: Synth::savePatch / Synth::loadPatch of synth part PART
//   synthlib init PART                  Synth::initPatch (the Init action), "synthlib engine PART N": Synth::setInstrumentType to the Nth engine of the list
//   fmsave PATH NAME                    newFileManager.saveProject(PATH, NAME): Save As into Projects folder PATH ("-" for the empty path), the
//                                       whole save runs in the main loop, "wait" for it. fmopen PATH NAME: newFileManager.openProject, the
//                                       open the USB console and the project list call (mtff_sim.sh)
//   voices                               the voices of the three synth parts as the shim holds them, one line per part:
//                                        "voices P cap C n=N [slot:gate:note:id:fade ...]" (voices that are on, in record order, id 0 = untagged), for synth_nna_sim.sh
//   renderhash [reset]                   "renderhash H0 H1 H2 blocks B0 B1 B2 loud L0 L1 L2": FNV-1a 64 of the bit patterns of every block the shim rendered for each part
//                                        since the last reset, the blocks and how many of them were not silent (the bit exact reference of synth_nna_sim.sh)
//   voicecount P N                       Voices (Polyphony) of part P, the way the editor row sets it
//   midiwatch [on|reset]                 counts the events the virtual MIDI port hands to the synth parts (note on, note off, stop) since "midiwatch reset",
//                                        "midiwatch on N off N stop N all N" (what reaches the shim's onMidi, synth_nna_sim.sh F13)
//   mute T N                             track T muted (N 1) or not (N 0), the project's trackMute / midiTrackMute as the Mute key sets it
//   clockpulses                          "clockpulses N": the external MIDI clock pulses the sequencer counted and has not used yet (synth_play_sim.sh P7)
//   perfpages                            Performance mode and the Synth play page as the firmware holds them, for perform_pages_sim.sh: one line
//                                        "perfpages screen S perfmode P page G open O keep K tracks <16 states> rows <12 selected rows>", S 0 other, 1 Performance, 2 Synth play
//   perffx SLOT                          gives a Performance slot the next FX of the list (perform_pages_sim.sh)
//   synthset K V                         sets page value K (synthplay::Param) to V, for the ones with no column: Input 0, Fixed vel 4 (synth_play_sim.sh P9)
//   synthplay                            the Synth play page as the firmware holds it, for synth_play_sim.sh: one line "synthplay mode M instr I budget B open O confirm C",
//                                        then per part "synthplay part P bend B range R voices V" (bend in semitones as the shim applies it, range = semitones for a full wheel)
//                                        and "synthplay cells <note:state ...>" (the router's table, state 1 held 2 pedal) and "synthplay pedal 0|1"
//   ptype PART N                         the stored engine number of PART in the project becomes N, the engine stays (the page's pair cap reads the number, synth_play_sim.sh)
//   paircap STORED [N]                   the row of STORED in the page's same engine pair cap table (kPairCap) reads N from now on, N -1 clears the override. Prints "paircap
//                                        stored S cap C" (C = the cap the page reads for that engine). The shipped table is untouched (synth_play_sim.sh AB6)
//   abstate                              the A/B page: "abstate partb B morph M banka A bankb B" then per part "abstate part P gain G vgain V" (the morph factor, the gain
//                                        of the part's first voice that is on, -1 none) and "abstate slots P cur C bank <slots with a file of the engine>" (synth_play_sim.sh)
//   capsim PART [N]                      N: the engine of PART declares N voices (a plugin with max_voices 4), prints "capsim part P declared D cap C" (synth_play_sim.sh P14)
//   govcap                               the voice pool the shim counts: "govcap budget B cap C sounding S cuts N"
// The same lines run from a ui_shot script as "poke LINE" (ui_shot_main.cpp), at that point of the script.
#define private public // host only: "clockpulses" reads the external clock pulse counter of the sequencer
#include "mtSequencer.h"
#undef private
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
#include "mtAudioEngine.h"
#include "masterParams/masterParams.h"
#include "sampleEditor/sampleEditor.h"
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
#include "performanceMode/performanceEngine.h"
#undef private

namespace polysynth::open { int hostVoices(int part, char* buf, unsigned n); } // open_synth.cpp (ui_shot overlay, gen_overlay.py)
namespace polysynth::open { int hostPart(int part, float* bend, float* range, int* voiceCount); }
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
             Synth::storedName(sp.synthType), instrumentEditor.getSynth()->getMaxVoiceCount(part), sp.patchName,
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
    char v[2][160] = {};
    for (int k = 0; k < 2; k++)
        for (uint32_t i = 0, n = plug::paramCount(k), at = 0; i < n && at + 16 < sizeof(v[k]); i++)
            at += unsigned(snprintf(v[k] + at, sizeof(v[k]) - at, "%s%g", i ? "," : "", double(plug::paramValue(k, i))));
    char l[480];
    snprintf(l, sizeof(l), "inserts T=%lu 1=%s:%s[%s] 2=%s:%s[%s]", (unsigned long)hostp::g_nowMs, kState[int(plug::slotState(0))], plug::slotName(0), v[0],
             kState[int(plug::slotState(1))], plug::slotName(1), v[1]);
    return l;
}

// midiwatch: an observer on the virtual MIDI port (MidiSource::subscribeMidiEvents), it sees what every synth part sees
int g_mwNoteOn = 0, g_mwNoteOff = 0, g_mwStop = 0, g_mwAll = 0;
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
            e.data.note.velocity ? g_mwNoteOn++ : g_mwNoteOff++;
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
            auto& patch = instrumentEditor.getSynth()->getPatch(part);
            const auto& md = patch.getMetadata();
            bool found = false;
            for (size_t i = 0; i < md.params.size() && !found; i++)
            {
                if (md.params[i].displayName == nm)
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
    else if (!strcmp(cmd, "sizes"))
    {
        out("sizeof(strProjectFile)=%zu", sizeof(strProjectFile));
    }
    else if (!strcmp(cmd, "synthval"))
    {
        const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        auto& patch = instrumentEditor.getSynth()->getPatch(part);
        const int idx = instrumentEditor.sectionParamsIds[instrumentEditor.paramListPos];
        const bool list = instrumentEditor.paramValueMode == instrumentEditor.kList;
        out("synthval pos %d id %d mode %s list %d norm %.3f", instrumentEditor.paramListPos, idx, list ? "list" : "value", instrumentEditor.paramValueListPos,
            double(patch.getParamValueEditMapped(idx).value_or(-1.0f)));
    }
    else if (!strcmp(cmd, "playing"))
        out("playing %d", int(sequencer.isPlay()));
    else if (!strcmp(cmd, "saveproj"))
    {
        static char path[] = "Projects";
        char name[PROJECT_NAME_SIZE] = {};
        strncpy(name, a, sizeof(name) - 1);
        out("saveProject(Projects, %s) = %d", name, (int)newFileManager.saveProject(path, name));
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
            out("part %d stored %d engine %s", p, st, Synth::storedName(st));
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
    else if (!strcmp(cmd, "heading"))
    {   // "heading": the patch panel label of the Synth screen ("Slot 0:", "Slot 0  Morph 3:" and the patch name)
        std::string l;
        hostPokeLabelText(instrumentEditor.noteLabel, &l);
        out("heading %s", l.c_str());
    }
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
    else if (!strcmp(cmd, "imode"))
    {   // the page of the instrument editor (synth_order_sim.sh, scenario 12: the Macros and Automation pages of a Missing part stay closed)
        out("imode %s", instrumentEditor.mode == mtInstEditModeSynth          ? "synth"
                        : instrumentEditor.mode == mtInstEditModeSynthMacros  ? "macros"
                        : instrumentEditor.mode == mtInstEditModeSynthAutomation ? "automation"
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
    else if (!strcmp(cmd, "clockpulses"))
        out("clockpulses %d", sequencer.externalClockPulses);   // MIDI clock pulses counted and not yet taken by the tempo follower (synth_play_sim.sh P7)
    else if (!strcmp(cmd, "synthset"))
    {
        const int k = atoi(a);   // a page value that has no column (Input, Fixed vel), by its synthplay::Param index
        if (got >= 3 && k >= 0 && k < synthplay::kValues)
            synthplay::value[k] = (int8_t)n;
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
        out("perfpages screen %d perfmode %d page %d open %d keep %d tracks %s rows %s", mtInterface.isPerformanceModeScreenActive() ? 1 : (synthPlay.isOpen() ? 2 : 0),
            (int)sequencer.isPerformanceMode(), (int)performanceMode.tracksPage, (int)synthPlay.isOpen(), (int)performanceMode.dontClearPerformanceMode, t, r);
    }
    else if (!strcmp(cmd, "paircap"))
    {   // paircap STORED [N]: N >= 0 overrides the row, N -1 clears the override, no N only reads
        const int stored = atoi(a);
        out("paircap stored %d cap %d", stored, synthPlayPairCapSim(stored, got >= 3 ? (n < 0 ? -1 : int(n)) : -2));
    }
    else if (!strcmp(cmd, "ptype"))
    {
        const int p = atoi(a);
        if (got >= 3 && p >= 0 && p < kSynthInstrumentsCount)
            mtProject.values.synthPart[p].synthType = (int32_t)n;
    }
    else if (!strcmp(cmd, "splstate"))
    {   // the page state of the round trip rows (synth_play_sim.sh P15): the part, B, transpose, the selected column and the item each column shows, the sends of the parts
        char sub[kSynthPlayColumns + 1] = {};
        for (int c = 0; c < kSynthPlayColumns; c++)
            sub[c] = char('0' + synthPlay.subItem(c));
        out("splstate part %d partb %d transpose %d voices %d sel %d sub %s", (int)synthplay::value[synthplay::kPart], (int)synthplay::value[synthplay::kPartB],
            (int)synthplay::value[synthplay::kTranspose], (int)synthplay::value[synthplay::kVoices], synthPlay.column(), sub);
        for (int p = 0; p < kSynthInstrumentsCount; p++)
            out("splstate send %d delay %d reverb %d", p, (int)mtProject.values.synthPart[p].delaySend, (int)mtProject.values.synthPart[p].reverbSend);
    }
    else if (!strcmp(cmd, "abstate"))
    {
        out("abstate partb %d morph %d banka %d bankb %d", (int)synthplay::value[synthplay::kPartB], (int)synthplay::value[synthplay::kMorph],
            (int)synthplay::value[synthplay::kBankA], (int)synthplay::value[synthplay::kBankB]);
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            float g = 0, vg = 0;
            if (polysynth::open::hostGain(p, &g, &vg) == 0)
                out("abstate part %d gain %.4f vgain %.4f", p, g, vg);
        }
        for (int p = 0; p < kSynthInstrumentsCount; p++)
        {
            char b[96];
            int at = snprintf(b, sizeof b, "abstate slots %d cur %d bank", p, instrumentEditor.getSynth()->getPatchSlot(p));
            for (int sl = 0; sl < polysynth::open::kPatchBankSlots; sl++)
                if (polysynth::open::patchBankHas(p, sl))
                    at += snprintf(b + at, sizeof b - at, " %d", sl);
            out("%s", b);
        }
    }
    else if (!strcmp(cmd, "govcap"))
    {
        out("govcap budget %d cap %d sounding %d cuts %u", polysynth::open::voiceBudget(), polysynth::open::voiceCap(), polysynth::open::voicesSounding(),
            (unsigned)polysynth::open::governorCuts());
    }
    else if (!strcmp(cmd, "inserts"))
        out("%s", insertsLine().c_str());
    else if (!strcmp(cmd, "duck"))
    {
#ifdef FEATURE_SEND_DUCK
        const DuckParams& e = engine.getSendDuck();
        const DuckParams& c = plug::duck();
        out("duck engine=%u,%u,%u,%u sidecar=%u,%u,%u,%u gain=%.4f page=%s place=%u", e.source, e.depth, e.release, e.thresh, c.source, c.depth, c.release,
            c.thresh, engine.getSendDuckGain(), !extMasterParams.isLimiterScreen ? "none" : (extMasterParams.duckPage ? "duck" : "limiter"),
            (unsigned)extMasterParams.selectedPlaceLimiter);
#else
        out("duck: not in this build");
#endif
    }
    else if (!strcmp(cmd, "projsave"))
        out("projsave %s", plug::projectSave(cWorkspacePath) ? "ok" : "FAILED");
    else if (!strcmp(cmd, "projload"))
    {   // the call that ends a project load (WorkspaceProjectLoad.cpp): the sidecar of the Workspace is read again, both inserts are
        // unloaded and queued. The synth parts and the audio are not touched, so this isolates the insert side of a load
        plug::projectLoaded(cWorkspacePath);
        out("projload T=%lu", (unsigned long)hostp::g_nowMs);
    }
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
    else if (!strcmp(cmd, "mtffver"))
        out("mtffver %d %lx", plug::mtffFormatVersion(), (unsigned long)plug::mtffWriterVersion());
    else if (!strcmp(cmd, "macromode"))
    {
        if (got >= 2) plug::setMacroMode(atoi(a));
        out("macromode %d", plug::macroMode());
    }
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
            sy->setInstrumentType(part, Synth::synthTypeAt(idx));
            out("synthlib engine part %d %d", part, idx);
        }
        else
            out("synthlib: usage synthlib save|load|init|engine PART [NAME|N]");
    }
    else if (!strcmp(cmd, "strumdepth"))
    {
        if (got >= 2) plug::setStrumRampDepth(atoi(a));
        out("strumdepth %d", plug::strumRampDepth());
    }
    else if (!strcmp(cmd, "fmsave") || !strcmp(cmd, "fmopen"))
    {
        char path[300] = {}, name[64] = {};
        sscanf(line, "%*s %299s %63s", path, name);
        if (!strcmp(path, "-")) path[0] = 0;
        const bool ok = !strcmp(cmd, "fmsave") ? newFileManager.saveProject(path, name) : newFileManager.openProject(path, name);
        out("%s '%s' '%s' started=%d", cmd, path, name, int(ok));
    }
    else if (!strcmp(cmd, "sidecar"))
    {   // the project hooks of plug_host.cpp with the folder paths the file manager hands them (long, nested ones included)
        char op[16] = {}, d1[512] = {}, d2[512] = {};
        sscanf(line, "%*s %15s %511s %511s", op, d1, d2);
        bool rc = false;
        if (!strcmp(op, "open"))
            rc = plug::sidecarOpen(d1, d2);
        else if (!strcmp(op, "save"))
            rc = plug::sidecarToProject(d1, d2);
        else if (!strcmp(op, "wssave"))
            rc = plug::projectSave(d1);
        else
        {
            out("sidecar: usage sidecar open|save|wssave DIR [DIR]");
            return;
        }
        out("sidecar %s rc=%d", op, int(rc));
    }
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
        run(l.c_str());
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
