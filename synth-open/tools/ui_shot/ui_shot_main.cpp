// ui_shot: runs the real Tracker UI (core/ and synth-open/ of this tree, the PLUG_HOST configuration) on the Mac against the
// FT812 model and writes the framebuffer as PNG. NOT firmware.
//
//   ui_shot SD_DIR "script"
//   ui_shot --serve SD_DIR ...
//                      the development console of a CONSOLE=1 build (make CONSOLE=1, needs a separate checkout, see
//                      devtools.mk at the top of the tree). Without it --serve only says so.
//
// The script is a ';' separated list of commands, run after the UI has started (pattern screen up):
//   wait MS            run the main loop for MS virtual milliseconds
//   tap NAME [N]       press and release a front panel button N times (names below)
//   hold NAME / unhold NAME   keep a button down (what Shift needs), press event on hold
//   enc N              turn the encoder N detents (negative: left)
//   pad N              press pad N (0 based), release
//   padon N [MS] / padoff N [MS]   press pad N and keep it down / release it (a chord on the pads), the loop runs MS after it (default 40)
//   midiclk N          N USB MIDI clock pulses with no main loop pass between them (synth_play_sim.sh)
//   midion NOTE [VEL] / midioff NOTE   a note from the USB MIDI input (notes in: USB and jack, omni), as mtMidi.cpp hands it on
//   setstep T S NOTE INSTR FX0T FX0V FX1T FX1V   write a step of the pattern on screen (NOTE -1 = empty, FX type ids of mtSequencer.h)
//   step T S           print a step: "step T S note N instr I fx0 T:V fx1 T:V"
//   seed [N|reroll]   print the project's random seed (seeded random takes), set it (0 Off) or press Reroll seed
//   saveproj NAME / openproj NAME / newproj   the file manager's project save and open (folder "Projects") and a new project, run to the end
//   now                print the rendered audio time in ms (the clock of the midilog lines, "audio 1")
//   stepv T S          print the volume and micro-move of a step: "stepv T S vol V move M" (-1 = not set)
//   cursor             print the pattern editor cursor: "cursor track T step S param P edit E"
//   scale ROOT SCALE   set the project Notes scale and Root note (root 24 = C, scale 0 chromatic, 1 minor, 2 major)
//   snap [T M]         print the scale snap mode of every track, or set track T (-1: all) to mode M (0 Off 1 Nearest 2 Down 3 Up)
//   shot FILE.png      write the screen
//   dump               print the model's statistics
//   power press|release        the power button (the callback the firmware registered, Tracker.cpp onPowerButtonChange)
//   power hold MS              press, run MS, print whether the shutdown reached setPowerMode(false), then keep holding
//   fm                         print the file manager status and operation
//   eeprom N                   set the MK66 EEPROM start state byte (1 = what a console image or a power-on leaves)
//   reset                      an MCU reset now (any cause): prints the start decision of the MK66 model
//   stall MS                   the main loop blocked in one call for MS (time and SysTick go on, no main loop pass)
//   resets                     print the MCU reset counters of the MK66 model
//   restartmode                print the restart mode state (RestartMode.h) and whether its file is on the card. With the
//                              features (EXTHOOKS=1): " cfg N", the on line of /Features/restart.cfg (-1: no file)
//   features                   print every feature: id, on, listed, row value (EXTHOOKS=1)
//   audiomark                  print "audio frames N": the frames the audio callback rendered so far. UISHOT_AUDIO_DUMP=FILE appends every
//                              rendered block to FILE (16 bit L R interleaved), the reference of the Skip-back sim (skipback_sim.sh)
//   skipback                   print the Skip-back state (EXTHOOKS=1): on, saving, channels, filled and saved frames, the last file
//   usbaudio                   print the USB audio model: the session's function and the EEPROM boot flag
//   eepromret N / usbflag N    set the EEPROM Power return mirror (byte 8) or the USB audio boot flag (byte 9)
//   fmset N                    set the file manager status to N (fileManager.h fileManagerStatus), an injection
//   touch PATH / rm PATH       create or remove a file (absolute path), e.g. the UISHOT_FAIL_ARM file of host_fault.cpp
//   put REL TEXT               write TEXT (backslash n and backslash r escapes) to REL in the SD folder, replacing the file (the order file of the model list)
//   cat REL                    print a file of the SD folder (nothing when it is not there)
//   chmod MODE PATH            chmod a path of the SD folder (octal MODE), e.g. 555 Workspace makes every Workspace save fail
//   padon padoff voices view waitview cardstats audiostats card   the synth engine scenario commands (host_synthcmd.cpp)
// Buttons: F1..F8 Params Performance File Config Master SampleLoad SamplePlay SampleEdit SampleRec Song Note Instr Vol Fx Pattern
//          Play Copy Insert Up Delete Rec Shift Left Down Right
#include "host_platform.h"
#include "tapeFx.h"
#include "mtAudioExport.h"
#include "mtFollowChord.h"

#include "Tracker.h"
#include "core/interface.h"
#include "display.h"
#include "mtStructs.h"
#include "fileManager.h"
#ifdef FEATURE_MODIT_MODULE
#include "ext_hooks.h"
#endif
#include "mtSleep.h"
#include "mtMidi.h"
#include "mtSequencer.h"
#include "fx34.h"
#include "patternEditor/patternEditor.h"
#if __has_include("RestartMode.h")
#include "RestartMode.h"
#endif
#if defined(FEATURE_EXT_HOOKS) && __has_include("features.h")
#include "features.h"
#define UISHOT_HAVE_FEATURES 1
#endif
#if __has_include("skipback.h")
#include "skipback.h"
#ifdef FEATURE_SKIPBACK
#define UISHOT_HAVE_SKIPBACK 1
#endif
#endif
#include "synthPlayRec.h"
#include <fstream>

extern Sequencer::strPattern copyPastePattern;   // mtSequencer_patternEdit.cpp: the stock Copy/Paste buffer

// The sample playback screen reads editorInstrument->playMode in initDisplayControls() before start() has set the pointer. On the
// Cortex-M a null pointer reads the flash at address 0, on the Mac it is a fault. The host seeds the pointer first, which needs the
// private member (access only, same layout; the standard headers are in already).
#define private public
#include "samplePlayback/samplePlayback.h"
#undef private

#include "mtCapture.h"
#include "mtChord.h"
#ifdef FEATURE_PARTS
#include "parts.h"
#include "songEditor/songEditor.h"
#endif

#include <cmath>
#include <vector>
#include <execinfo.h>
#include <poll.h>
#include <mach-o/dyld.h>
#include <map>
#include <signal.h>
#include <unistd.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>

std::string g_sdRootForStat;
bool hostSynthCommand(const std::string& c, std::istringstream& is);
#ifdef UISHOT_BRIDGE
bool hostPlugRenderCommand(const std::string& c, std::istringstream& is);   // host_plugrender.cpp (BRIDGE=1)
#endif

extern "C" void hostPokeRun(const char* line); // host_poke.cpp
bool hostFx34StoreCommand(const std::string& line, bool* ok); // host_poke_fx34_store.cpp
extern "C" void hostTapAttach(int track);        // host_poke_fx.cpp: "audiostat tap N"
extern "C" void hostTapClear();
extern "C" size_t hostTapCount();
extern "C" void hostTapGet(size_t i, float* rms, int* zc);
// "audio 1": the audio callback (the firmware's own Tracker::updateAudioBuffers) runs between the main loop passes, the blocks that fit
// into each 5 ms step (128 frames at 44,117.6 Hz = 2,901.3 us), so the sequencer ticks and plays. g_audioUs: rendered audio time,
// the time stamp of the "midilog" lines (host_platform.cpp).
double g_audioUs = 0;
bool g_midiLog = false;
namespace
{
bool g_audio = false;
double g_audioAcc = 0;
// "audiostat 1" .. "audiostat dump": the level and the zero crossings of every audio block the callback renders (left + right of the
// final mix, 128 frames per block). What the Tape FX proof reads: the level over time and the pitch as crossings per block.
bool g_stat = false;
struct BlockStat { float rms; int zc; };
std::vector<BlockStat> g_blocks;
void statBlock(const int16_t* l, const int16_t* r)
{
    double sum = 0.0;
    int zc = 0, prev = 0;
    for (int i = 0; i < 128; i++)
    {
        const int v = int(l[i]) + int(r[i]);
        sum += double(v) * double(v);
        if (i > 0 && ((prev < 0 && v >= 0) || (prev >= 0 && v < 0))) zc++;
        prev = v;
    }
    g_blocks.push_back({float(std::sqrt(sum / 128.0)), zc});
}
uint64_t g_audioFrames;   // rendered frames (audiomark)
FILE* audioDump()
{   // UISHOT_AUDIO_DUMP: every rendered block, the reference the Skip-back sim compares the saved WAV with
    static FILE* f = getenv("UISHOT_AUDIO_DUMP") ? fopen(getenv("UISHOT_AUDIO_DUMP"), "wb") : nullptr;
    return f;
}
// "wav PATH" appends every block the audio callback renders to PATH as 16 bit stereo WAV (header rate 44100, the MK66 runs 44117.647 Hz),
// "wav off" closes it. Mac listening and level checks only, nothing of this is in the firmware.
FILE* g_wav = nullptr;
uint32_t g_wavFrames = 0;
void wavHeader(FILE* f, uint32_t frames)
{
    const uint32_t rate = 44100, bytes = frames * 4, riff = 36 + bytes, brate = rate * 4, fmt = 16;
    const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fseek(f, 0, SEEK_END);
}
void wavClose()
{
    if (!g_wav) return;
    wavHeader(g_wav, g_wavFrames);
    fclose(g_wav);
    g_wav = nullptr;
}
void run(uint32_t ms)
{
    Tracker* t = Tracker::getInstance();
    static int16_t l[256], r[256];
    for (uint32_t done = 0; done < ms; done += 5)
    {
        hostp::advance(5);
        const uint32_t t0 = hostp::g_nowMs;
        t->update();
        // virtual time one main loop pass took (only card waits of SIM_CARD_FAULTS slow= move the clock inside it): the UI stall
        if (hostp::g_nowMs - t0 > hostp::g_audio.maxMainMs) hostp::g_audio.maxMainMs = hostp::g_nowMs - t0;
        if (!g_audio) continue;
        for (g_audioAcc += 5000.0; g_audioAcc >= 2901.33; g_audioAcc -= 2901.33)
        {
            t->updateAudioBuffers(l, r, 128);
            if (g_stat) statBlock(l, r);
            if (g_wav)
            {
                int16_t lr[256];
                for (int i = 0; i < 128; i++) { lr[2 * i] = l[i]; lr[2 * i + 1] = r[i]; }
                fwrite(lr, sizeof(int16_t), 256, g_wav);
                g_wavFrames += 128;
            }
            g_audioUs += 1e6 * 128 / 44117.6470588;
            g_audioFrames += 128;
            if (FILE* d = audioDump())
            {
                int16_t lr[256];
                for (int i = 0; i < 128; i++) { lr[2 * i] = l[i]; lr[2 * i + 1] = r[i]; }
                fwrite(lr, sizeof lr, 1, d);
                fflush(d);
            }
        }
    }
}

int buttonId(const std::string& n)
{
    static const std::map<std::string, int> m = {
        {"F1", 0}, {"F2", 1}, {"F3", 2}, {"F4", 3}, {"F5", 4}, {"F6", 5}, {"F7", 6}, {"F8", 7},
        {"Params", 8}, {"Performance", 9}, {"File", 10}, {"Config", 11}, {"Master", 12}, {"SampleLoad", 13}, {"SamplePlay", 14},
        {"SampleEdit", 15}, {"SampleRec", 16}, {"Song", 17}, {"Note", 18}, {"Instr", 19}, {"Vol", 20}, {"Fx", 21}, {"Pattern", 22},
        {"Play", 23}, {"Copy", 24}, {"Insert", 25}, {"Up", 26}, {"Delete", 27}, {"Rec", 28}, {"Shift", 29}, {"Left", 30},
        {"Down", 31}, {"Right", 32},
    };
    auto it = m.find(n);
    return it == m.end() ? -1 : it->second;
}

bool command(const std::string& line)
{
    std::istringstream is(line);
    std::string c, a;
    is >> c;
    if (c.empty()) return true;
    if (c == "wait") { int ms = 0; is >> ms; run(uint32_t(ms)); return true; }
    if (c == "repeat")
    {   // the auto-repeat of a held key: a buttonHold event (Keys::update every holdTime), Interface::buttonChange turns it into a press on the arrows
        is >> a;
        int n = 1;
        is >> n;
        const int id = buttonId(a);
        if (id < 0) { fprintf(stderr, "ui_shot: unknown button '%s'\n", a.c_str()); return false; }
        for (int i = 0; i < n; i++) { mtInterface.buttonChange(uint8_t(id), buttonHold); run(40); }
        return true;
    }
    if (c == "tap" || c == "hold" || c == "unhold")
    {
        is >> a;
        int n = 1;
        is >> n;
        const int id = buttonId(a);
        if (id < 0) { fprintf(stderr, "ui_shot: unknown button '%s'\n", a.c_str()); return false; }
        if (c == "hold") { hostp::setHeld(id, true); mtInterface.buttonChange(uint8_t(id), buttonPress); run(40); return true; }
        if (c == "unhold") { mtInterface.buttonChange(uint8_t(id), buttonRelease); hostp::setHeld(id, false); run(40); return true; }
        for (int i = 0; i < n; i++)
        {
            hostp::setHeld(id, true);
            mtInterface.buttonChange(uint8_t(id), buttonPress);
            run(40);
            mtInterface.buttonChange(uint8_t(id), buttonRelease);
            hostp::setHeld(id, false);
            run(40);
        }
        return true;
    }
    if (c == "enc") { int d = 0; is >> d; mtInterface.potChange(0, int16_t(d)); run(60); return true; }
    if (c == "pad") { int p = 0; is >> p; mtInterface.padPressed(uint8_t(p), 0, 0, 100); run(40); mtInterface.padReleased(uint8_t(p)); run(40); return true; }
    if (c == "padon") { int p = 0, w = 40; is >> p >> w; mtInterface.padPressed(uint8_t(p), 0, 0, 100); run(uint32_t(w < 0 ? 0 : w)); return true; }
    if (c == "padoff") { int p = 0, w = 40; is >> p >> w; mtInterface.padReleased(uint8_t(p)); run(uint32_t(w < 0 ? 0 : w)); return true; }
    if (c == "midion" || c == "midioff")
    {
        int n = 60, v = 100, w = 40;   // w: the ms the loop runs after the note (a chord played by hand is a few ms apart)
        is >> n >> v >> w;
        mtConfig.midi.notesInMode = notesIn_usb_and_jack;
        mtConfig.midi.notesInChannel = 0;
        if (c == "midion") handleUsbNoteOn(1, uint8_t(n), uint8_t(v));
        else handleUsbNoteOff(1, uint8_t(n), 0);
        run(uint32_t(w < 0 ? 0 : w));
        return true;
    }
    if (c == "midich" || c == "midichoff")
    {   // midich CH NOTE [VEL] / midichoff CH NOTE: a USB note on or off on that channel (the MIDI remote, ui_remote_sim.sh)
        int ch = 1, n = 60, v = 100;
        is >> ch >> n >> v;
        if (c == "midich") handleUsbNoteOn(uint8_t(ch), uint8_t(n), uint8_t(v));
        else handleUsbNoteOff(uint8_t(ch), uint8_t(n), 0);
        run(60);
        return true;
    }
    if (c == "midicc")
    {   // midicc CH NUMBER VALUE: a USB control change
        int ch = 1, n = 0, v = 0;
        is >> ch >> n >> v;
        handleUSBControlChange(uint8_t(ch), uint8_t(n), uint8_t(v));
        run(60);
        return true;
    }
    // The Synth play page's inputs (synth_play_sim.sh), as the handlers of mtMidi.cpp get them from the MIDI libraries. Unlike midion they leave
    // the Config > MIDI settings alone: the page has its own Input and Channel
    if (c == "midibend" || c == "dinbend")
    {   // midibend CH VALUE / dinbend CH VALUE: pitch bend -8192..8191 on USB / on DIN
        int ch = 1, v = 0;
        is >> ch >> v;
        if (c == "midibend") handleUsbPitchBend(uint8_t(ch), v);
        else handleJackPitchBend(uint8_t(ch), v);
        run(20);
        return true;
    }
    if (c == "dinon" || c == "dinoff" || c == "dincc")
    {   // dinon CH NOTE [VEL] / dinoff CH NOTE / dincc CH NUMBER VALUE: the same on the DIN jack
        int ch = 1, n = 60, v = 100;
        is >> ch >> n >> v;
        if (c == "dinon") handleJackNoteOn(uint8_t(ch), uint8_t(n), uint8_t(v));
        else if (c == "dinoff") handleJackNoteOff(uint8_t(ch), uint8_t(n), 0);
        else handleJackControlChange(uint8_t(ch), uint8_t(n), uint8_t(v));
        run(20);
        return true;
    }
    if (c == "midiono" || c == "midionoff")
    {   // midiono CH NOTE VEL / midionoff CH NOTE: a USB note on or off with no wait and without touching the Config (a fast chord: all in one block)
        int ch = 1, n = 60, v = 100;
        is >> ch >> n >> v;
        if (c == "midiono") handleUsbNoteOn(uint8_t(ch), uint8_t(n), uint8_t(v));
        else handleUsbNoteOff(uint8_t(ch), uint8_t(n), 0);
        return true;
    }
    if (c == "midiclk")
    {   // midiclk N: N USB MIDI clock pulses in a row with no main loop pass between (so "poke clockpulses" sees all of them)
        int n = 1;
        is >> n;
        mtConfig.midi.clkIn = clockIn_Usb;
        for (int i = 0; i < n; i++) receiveUsbRealtime(Midi::MessageType::kClock);
        return true;
    }
    if (c == "midirt")
    {   // midirt start|continue|stop|clock|songpos [BEATS]: the USB real time messages with Config > MIDI transport in and clock in set to USB
        std::string what;
        int beats = 0;
        is >> what >> beats;
        mtConfig.midi.transportIn = clockIn_Usb;
        mtConfig.midi.clkIn = clockIn_Usb;
        if (what == "start") receiveUsbRealtime(Midi::MessageType::kStart);
        else if (what == "continue") receiveUsbRealtime(Midi::MessageType::kContinue);
        else if (what == "stop") receiveUsbRealtime(Midi::MessageType::kStop);
        else if (what == "clock") receiveUsbRealtime(Midi::MessageType::kClock);
        else if (what == "songpos") handleUsbSongPosition(uint16_t(beats));
        run(20);
        return true;
    }
    if (c == "setstep")
    {
        int t = 0, st = 0, note = -1, ins = 0, f0t = 0, f0v = 0, f1t = 0, f1v = 0;
        is >> t >> st >> note >> ins >> f0t >> f0v >> f1t >> f1v;
        auto& step = sequencer.getPatternToUI()->track[t].step[st];
        step.note = int8_t(note);
        step.instrument = uint8_t(ins);
        step.fx[0].type = uint8_t(f0t);
        step.fx[0].value = uint8_t(f0v);
        step.fx[1].type = uint8_t(f1t);
        step.fx[1].value = uint8_t(f1v);
        patternEditor.refreshPattern();
        run(40);
        return true;
    }
    if (c == "patlen")
    {   // the pattern length in steps (all tracks take the length of track 0, as the pattern edit does), for passes that take seconds, not minutes
        int n = 16;
        is >> n;
        auto* pat = sequencer.getPatternToUI();
        pat->track[0].length = uint8_t(n - 1);
        sequencer.updateTrackLengths(pat);
        patternEditor.refreshPattern();
        run(40);
        return true;
    }
    if (c == "fx34on")
    {   // fx34on 0|1: the "4 FX" project switch of the host (the Project screen row and the project load set it the same way)
        int n = 0;
        is >> n;
        fx34::setEnabled(n != 0);
        patternEditor.refreshPattern();
        run(40);
        return true;
    }
    if (c == "vmcols")
    {   // vmcols 0|1: Config > General > Pattern V/m columns (3 tracks with V and m columns instead of the marks)
        int n = 0;
        is >> n;
        mtConfig.common.patternVmColumns = uint8_t(n != 0);
        patternEditor.showTitleBarPattern();
        patternEditor.focusOnActual();
        patternEditor.refreshPattern();
        run(40);
        return true;
    }
    if (c == "setfx" || c == "fx34")
    {   // setfx T S RANK TYPE VALUE: FX3 / FX4 (rank 2 = FX4, rank 3 = FX3) of a step of the pattern on screen. fx34 T S prints them
        int t = 0, st = 0, rank = 2, ty = 0, v = 0;
        is >> t >> st;
        auto* pat = sequencer.getPatternToUI();
        if (c == "setfx")
        {
            is >> rank >> ty >> v;
            const bool full = fx34::put(fx34::setOf(pat), uint8_t(t), uint8_t(st), uint8_t(rank), uint8_t(ty), uint8_t(v)) == fx34::kFull;
            printf("setfx %s\n", full ? "full" : "ok");
            patternEditor.refreshPattern();
            run(40);
        }
        else
        {
            fx34::Fx f[2];
            fx34::getStep(fx34::setOf(pat), uint8_t(t), uint8_t(st), f);
            printf("fx34 %d %d r2 %d:%d r3 %d:%d n %d\n", t, st, f[0].type, f[0].value, f[1].type, f[1].value, int(fx34::count(fx34::setOf(pat))));
        }
        fflush(stdout);
        return true;
    }
    if (c == "setmm" || c == "mm")
    {   // the micromove field of a step (-1 none, 0..100): setmm T STEP V sets it, mm T STEP prints "mm T STEP V"
        int t = 0, st = 0, v = -1;
        is >> t >> st;
        auto& step = sequencer.getPatternToUI()->track[t].step[st];
        if (c == "setmm") { is >> v; step.micromove = int8_t(v); patternEditor.refreshPattern(); run(40); return true; }
        printf("mm %d %d %d\n", t, st, int(step.micromove));
        fflush(stdout);
        return true;
    }
    if (c == "groove")
    {   // groove T V sets the per track groove, bare "groove" prints "groove v0 .. v15" (what the sequencer plays)
        int t = -1, v = 0;
        if (is >> t >> v) { sequencer.setTrackGroove(uint8_t(t), v); return true; }
        printf("groove");
        for (int tr = 0; tr < kAllTracks; tr++) printf(" %d", int(sequencer.getTrackGroove(uint8_t(tr))));
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "recmode") { int n = 0; is >> n; mtConfig.common.recReplace = uint8_t(n); return true; }   // Config > General > Recording mode
    if (c == "step")
    {
        int t = 0, st = 0;
        is >> t >> st;
        const auto& step = sequencer.getPatternToUI()->track[t].step[st];
        printf("step %d %d note %d instr %d fx0 %d:%d fx1 %d:%d\n", t, st, step.note, step.instrument, step.fx[0].type, step.fx[0].value,
               step.fx[1].type, step.fx[1].value);
        fflush(stdout);
        return true;
    }
    if (c == "stepv")
    {   // the volume and the micro-move of a step ("stepv T S vol V move M", -1 = not set)
        int t = 0, st = 0;
        is >> t >> st;
        const auto& step = sequencer.getPatternToUI()->track[t].step[st];
        printf("stepv %d %d vol %d move %d\n", t, st, step.volume, step.micromove);
        fflush(stdout);
        return true;
    }
    if (c == "lens")
    {   // polymeter: the mark, the pattern length and the length every track plays (mtSequencer.h getTrackLastStep), 1 based
        const auto* p = sequencer.getPatternToUI();
        printf("lens mark %d pattern %d tracks", int(Sequencer::isPolymeter(p)), p->track[0].length + 1);
        for (int t = 0; t < kAllTracks; t++) printf(" %d", Sequencer::getTrackLastStep(p, t) + 1);
        printf(" label %s\n", patternEditor.cLength);
        fflush(stdout);
        return true;
    }
    if (c == "rawlen")
    {   // the stored bytes: rezerwa1 rezerwa2 and track[t].length, to seed stale stock lengths ("rawlen T N" sets track[T].length = N)
        auto* p = sequencer.getPatternToUI();
        int t = -1, n = 0;
        if (is >> t >> n) { p->track[t].length = uint8_t(n); patternEditor.refreshPattern(); run(40); return true; }
        printf("rawlen r1 %d r2 %d r3 %d", p->rezerwa1, p->rezerwa2, p->rezerwa3);
        for (int k = 0; k < kAllTracks; k++) printf(" %d", p->track[k].length);
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "patop")
    {   // the pattern actions of the second action button page: dup (duplicate), exp (expand), shr (shrink)
        is >> a;
        if (a == "dup") sequencer.duplicatePattern();
        else if (a == "exp") sequencer.expandPattern();
        else if (a == "shr") sequencer.shrinkPattern();
        patternEditor.refreshPattern();
        run(40);
        return true;
    }
    if (c == "ltitle")
    {   // the left side of the pattern title bar ("Pattern 1  V51 m30")
        printf("ltitle %s\n", patternEditor.cTitlePattern);
        fflush(stdout);
        return true;
    }
    if (c == "capbuf")
    {   // a step of the copy buffer (the stock Copy/Paste one, where Capture puts its notes): the step fields and the volume and micro move
        int t = 0, st = 0;
        is >> t >> st;
        const auto& step = copyPastePattern.track[t].step[st];
        printf("capbuf %d %d note %d instr %d fx0 %d:%d fx1 %d:%d vol %d micro %d\n", t, st, step.note, step.instrument, step.fx[0].type, step.fx[0].value,
               step.fx[1].type, step.fx[1].value, step.volume, step.micromove);
        fflush(stdout);
        return true;
    }
    if (c == "capsel")
    {   // the selection the buffer holds (what Paste takes): first step, first track, last step, last track
        printf("capsel %d %d %d %d\n", sequencer.copySelection.firstStep, sequencer.copySelection.firstTrack, sequencer.copySelection.lastStep,
               sequencer.copySelection.lastTrack);
        fflush(stdout);
        return true;
    }
    if (c == "title")
    {   // the right side of the pattern title bar (the FX name of the cell under the cursor)
        printf("title %s\n", patternEditor.rightTitleBarLabel);
        fflush(stdout);
        return true;
    }
    if (c == "follow")
    {   // Follow chord: the F4 / F5 texts of the third action page (cursor track) and every follower of the setting
        printf("follow page %d labels %s|%s", int(patternEditor.actualActionButtonsPage), patternEditor.followModeText,
               patternEditor.followSourceText);
        for (int t = 0; t < kAllTracks; t++)
            if (followchord::setting.mode[t]) printf(" t%d<-t%d:%s", t + 1, followchord::setting.source[t] + 1, followchord::modeName(followchord::setting.mode[t]));
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "cursor")
    {
        printf("cursor track %d step %d param %d edit %d\n", cPatternEditor::trackerPattern.actualTrack, cPatternEditor::trackerPattern.actualStep,
               patternEditor.editParam, patternEditor.editMode);
        fflush(stdout);
        return true;
    }
    if (c == "shot")
    {
        is >> a;
        run(300);   // let the lists and bars settle (they animate)
        if (!hostp::eve().savePng(a.c_str())) { fprintf(stderr, "ui_shot: cannot write %s\n", a.c_str()); return false; }
        return true;
    }
    if (c == "dirty") { newFileManager.setActualPatternStructChanged(); newFileManager.setProjectStructChanged(); run(40); return true; }   // the Workspace autosave picks the pattern up
    if (c == "recopt") { int n = 0; is >> n; mtConfig.general.recOptions = uint8_t(n); return true; }   // Config > General > Recording options
    if (c == "seed")
    {   // seeded random takes: "seed" prints the project's random seed, "seed N" sets it (0 Off), "seed reroll" is the Reroll seed row
        std::string v;
        if (is >> v) { if (v == "reroll") sequencer.rerollRandomSeed(); else sequencer.randomSeed = uint8_t(atoi(v.c_str())); }
        printf("seed %d\n", int(sequencer.randomSeed));
        fflush(stdout);
        return true;
    }
    if (c == "saveproj" || c == "openproj" || c == "newproj")
    {   // the file manager's project operations as the Project screen starts them, run until the file manager is idle.
        // saveproj NAME / openproj NAME use the "Projects" folder, saveproj DIR NAME / openproj DIR NAME the card folder DIR/NAME
        // (features-140: one handler for the idea-seed and idea-groove forms)
        char path[PATCH_SIZE] = "Projects", name[PROJECT_NAME_SIZE] = {};
        std::string a, b;
        is >> a;
        if (is >> b) { strncpy(path, a.c_str(), sizeof(path) - 1); strncpy(name, b.c_str(), sizeof(name) - 1); }
        else strncpy(name, a.c_str(), sizeof(name) - 1);
        bool ok = true;
        if (c == "saveproj") ok = newFileManager.saveProject(path, name);
        else if (c == "openproj") ok = newFileManager.openProject(path, name);
        else { newFileManager.createNewProjectInWorkspace(); ok = newFileManager.openProjectFromWorkspace(); }
        for (int i = 0; ok && i < 6000 && (newFileManager.getStatus() != fmIdle || newFileManager.getCurrentOperation() != fmNoOperation); i++) run(5);
        run(200);
        printf("%s %s status %d\n", c.c_str(), ok ? "ok" : "refused", int(newFileManager.getStatus()));
        fflush(stdout);
        return true;
    }
    if (c == "modimport" || c == "modexport")
    {   // MOD/IT import and IT export as the Project screen starts them (modit_golden_sim.sh), run until the file manager is idle.
        // modimport DIR NAME: a new Workspace project, then the import of the card file DIR/NAME. modexport: the open project to Mod/<project>.it
        char path[PATCH_SIZE] = {}, name[PROJECT_NAME_SIZE + 32] = {};
        bool ok = true;
#ifdef FEATURE_MODIT_MODULE
        ok = ext_modit_check(nullptr, nullptr) == 0;   // the click's first step (fProjEdit.cpp moditReady): modit.plg from the card, or the refusal
        if (!ok) { std::string skip; std::getline(is, skip); }
#endif
        if (!ok) {}
        else if (c == "modimport")
        {
            std::string a, b;
            is >> a >> b;
            strncpy(path, a.c_str(), sizeof(path) - 1);
            strncpy(name, b.c_str(), sizeof(name) - 1);
            newFileManager.createNewProjectInWorkspace();
            ok = newFileManager.openProjectFromWorkspace() && newFileManager.importModAfterLoadNewProject(path, name);
        }
        else ok = newFileManager.exportItFile();
        int last = -1, waited = 0;
        for (; ok && waited < 60000; waited += 5)
        {
            run(5);
            const int st = int(newFileManager.getStatus());
            if (st == fmLoadEnd || st == fmExportModEnd || st >= fmError) { last = st; newFileManager.setStatus(fmIdle); }
            if (newFileManager.getStatus() == fmIdle && newFileManager.getCurrentOperation() == fmNoOperation) break;
        }
        run(200);
        printf("%s %s %s end status %d (error %d) after %d ms\n", c.c_str(), name, ok ? "started" : "refused", last, int(last >= fmError), waited);
        fflush(stdout);
        return true;
    }
    if (c == "export")
    {   // export pattern|song|patternstems|songstems: the real mtAudioExport render (what Project > Export does), run to its end.
        // The WAV lands on the card under Export/<project>/ (export_bpm_check.py reads it)
        std::string m;
        is >> m;
        const uint8_t mode = m == "song" ? exportSong : m == "patternstems" ? exportPatternStems : m == "songstems" ? exportSongStems : exportPattern;
        bool ok;
        if (m == "selection")
        {   // export selection FIRST_STEP LAST_STEP: Render Selection of the steps of tracks 1 to 8, the WAV lands in Export/<project>/Selection/sel.wav
            int s0 = 0, s1 = 15;
            is >> s0 >> s1;
            cPatternEditor::trackerPattern.selectState = 2;
            cPatternEditor::trackerPattern.selectColumn = 0;
            cPatternEditor::trackerPattern.selectStartStep = int16_t(s0);
            cPatternEditor::trackerPattern.selectEndStep = int16_t(s1);
            cPatternEditor::trackerPattern.selectStartTrack = 0;
            cPatternEditor::trackerPattern.selectEndTrack = 7;
            char name[] = "sel";
            ok = newFileManager.exportSoundRenderSelection(name);
        }
        else
            ok = newFileManager.exportSound(mode);
        int waited = 0;
        for (; ok && waited < 3600000; waited += 5)
        {
            run(5);
            if (getenv("EXPORT_DEBUG") && waited % 5 == 0 && waited < 60) fprintf(stderr, "export dbg: play %d timer %u status %d\n", int(sequencer.isPlay()), unsigned(sequencer.getSeqTimer()), int(exporter.getStatus()));
            if (newFileManager.getStatus() == fmExportSoundEnd) newFileManager.setStatus(fmIdle);
            if (exporter.getStatus() == 0 && waited > 50) break;   // the render ended (the UI's own update turns the file manager state idle)
        }
        run(200);
        printf("export %s %s after %d ms\n", m.c_str(), ok ? "done" : "refused", waited);
        fflush(stdout);
        return true;
    }
    if (c == "now") { printf("now %.1f\n", g_audioUs / 1000.0); fflush(stdout); return true; }   // the rendered audio time in ms (the midilog clock)
    if (c == "audio") { int n = 0; is >> n; g_audio = n != 0; return true; }
    if (c == "sampletone")
    {   // instrument N (0-based) becomes a 4 s sine of 441 Hz in the sample memory, single shot, no file: what the Tape FX proof plays
        int n = 0;
        is >> n;
        static int16_t tone[44100 * 4];
        for (size_t i = 0; i < sizeof(tone) / sizeof(tone[0]); i++) tone[i] = int16_t(12000.0 * std::sin(6.283185307179586 * 441.0 * double(i) / 44100.0));
        auto& in = mtProject.instrument[n];
        in.isActive = 1;
        in.sample.address = tone;
        in.sample.length = uint32_t(sizeof(tone) / sizeof(tone[0]));
        in.playMode = playModeSingleShot;
        in.startPoint = 0;
        in.endPoint = MAX_16BIT;
        in.volume = 100;
        mtProject.sampleMemory[n].address[0] = tone;
        mtProject.sampleMemory[n].address[1] = tone;
        mtProject.sampleMemory[n].isStreamed = false;
        return true;
    }
    if (c == "audiostat")
    {
        is >> a;
        if (a == "tap") { int t = 0; is >> t; hostTapAttach(t); }
        else if (a == "1") { g_blocks.clear(); hostTapClear(); g_stat = true; }
        else if (a == "0") g_stat = false;
        else if (a == "dump")
        {
            printf("audiostat blocks %zu\n", g_blocks.size());
            for (size_t i = 0; i < g_blocks.size(); i++) printf("blk %zu rms %.1f zc %d\n", i, g_blocks[i].rms, g_blocks[i].zc);
        }
        else if (a == "tapdump")
        {
            printf("audiostat blocks %zu\n", hostTapCount());
            for (size_t i = 0; i < hostTapCount(); i++) { float r; int z; hostTapGet(i, &r, &z); printf("blk %zu rms %.1f zc %d\n", i, r, z); }
            fflush(stdout);
        }
        return true;
    }
    if (c == "wav")
    {
        std::string a;
        is >> a;
        wavClose();
        if (a != "off" && !a.empty())
        {
            g_wav = fopen(a.c_str(), "w+b");
            g_wavFrames = 0;
            if (g_wav) wavHeader(g_wav, 0);
            printf("wav %s\n", g_wav ? "on" : "err");
        }
        return true;
    }
    if (c == "midilog") { int n = 0; is >> n; g_midiLog = n != 0; mtConfig.midi.notesOutMode = notesOut_mode_usb; return true; }
    if (c == "arm")
    {   // only track T armed for live recording (what Shift + Rec and the track buttons do), -1: every track armed (the default)
        int t = -1;
        is >> t;
        for (int tr = 0; tr < kAllTracks; tr++)
            if (bool(sequencer.isRecArmedOnTrack(uint8_t(tr))) != (t < 0 || tr == t)) sequencer.toogleRecArmedOnTrack(uint8_t(tr));
        return true;
    }
    if (c == "armed")
    {
        printf("armed");
        for (int tr = 0; tr < kAllTracks; tr++) printf(" %d", int(sequencer.isRecArmedOnTrack(uint8_t(tr))));
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "capture")
    {   // Config > Features > Capture on or off (the real feature switches the same flag), the button page of the pattern editor follows
        int n = 1;
        is >> n;
        captureFeatureOn = n != 0;
        patternEditor.refreshEditState();
        run(40);
        return true;
    }
    if (c == "capring")
    {   // the capture ring, oldest first: the count line, then one line per event (note, on|off, velocity, pos, uStep, instrument, pattern, flags, time in us)
        const uint16_t cnt = captureRingCount();
        printf("capring count %d\n", int(cnt));
        for (int back = int(cnt) - 1; back >= 0; back--)
        {
            CaptureEvent e;
            if (!captureRingGet(uint16_t(back), &e)) continue;
            printf("cap %d %s v%d pos %d ustep %d instr %d pat %d flags %d t %u\n", e.note, (e.vel & kCapOn) ? "on" : "off", e.vel & 0x7F, e.pos, e.uStep,
                   e.instrument, e.pattern, e.flags, unsigned(e.tUs));
        }
        fflush(stdout);
        return true;
    }
    if (c == "capclear") { captureRingClear(); return true; }
    if (c == "capind")
    {   // the Capture indicator of the title bar: the state it is drawn in (0 hidden, 1 empty, 2 stale, 3 ready), how often it was redrawn, whether it is shown, the ring count
        printf("capind state %d draws %d idle %d ring %d\n", int(titleBar.captureStateDrawn()), int(titleBar.captureDraws()), int(captureIdleSec), int(captureRingCount()));
        fflush(stdout);
        return true;
    }
    if (c == "capidle") { int n = 20; is >> n; captureIdleSec = uint8_t(n); return true; }   // Config > Features > Capture > Idle time, seconds
    if (c == "chordnames") { int n = 1; is >> n; chordShortNamesOn = n != 0; patternEditor.refreshPattern(); run(40); return true; }
    if (c == "grid")
    {   // the FX cells of a step as the pattern grid draws them: the type letter and the 3 character value (fPatternEdit.cpp)
        int t = 0, st = 0;
        is >> t >> st;
        const auto& step = sequencer.getPatternToUI()->track[t].step[st];
        printf("grid %d %d", t, st);
        for (int k = 0; k < 2; k++)
        {
            char v[5] = {};
            const uint8_t name = step.fx[k].type ? interfaceGlobals.fxIdToName(step.fx[k].type) : 0;
            if (name > 0 && name < FX_COUNT) sequencer.makeFxValLabelForSeqStep(v, 4, uint8_t(k), uint8_t(t), uint8_t(st));
            const bool listed = name > 0 && name < FX_COUNT;
            const char letter = !listed ? '-' : step.fx[k].type == sequencer.fx.FX_TYPE_TAPE ? tapefx::letter(step.fx[k].value) : interfaceGlobals.ptrFxNames[name][0];
            printf(" fx%d [%c%s]", k, letter, listed ? v : "---");
        }
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "scale")
    {   // the project's Notes scale and Root note (Config > Project Settings), as the two list items leave them
        int root = 24, sc = 0;
        is >> root >> sc;
        mtProject.values.padBoardRootNote = uint8_t(root);
        mtProject.values.padBoardScale = uint8_t(sc);
        return true;
    }
    if (c == "snap")
    {   // "snap": print the scale snap mode of every track, "snap T M": set track T (-1: all) to mode M (Project Settings > Scale snap)
        int t = -2, m = 0;
        is >> t >> m;
        if (t == -2)
        {
            printf("snap");
            for (int k = 0; k < kScaleSnapTracks; k++) printf(" %d", int(scaleSnapMode[k]));
            printf("\n");
            fflush(stdout);
        }
        else
            for (int k = 0; k < kScaleSnapTracks; k++)
                if (t < 0 || k == t) scaleSnapMode[k] = uint8_t(m);
        return true;
    }
    if (c == "snapfx")
    {   // "snapfx": the Snap FX latch of every track (255 none, else the mode 0 Off .. 3 Up). "stutter T V" sets the step repeater of track T
        // (a Performance mode setting, value 0 none or a Sequencer::enStutterPeriod: 11 is 1/4, 15 is 1/16) for the stutter check of snap_fx_sim.sh
        std::string w;
        is >> w;
        if (w == "stutter")
        {
            int t = 0, v = 0;
            is >> t >> v;
            const_cast<Sequencer::strPlayer*>(sequencer.ptrPlayer)->track[t].performanceStepRepeater = int8_t(v);
            return true;
        }
        printf("snapfx");
        for (int k = 0; k < kScaleSnapTracks; k++) printf(" %d", int(sequencer.ptrPlayer->track[k].snapFx));
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "instr")
    {   // what the Instr button and the encoder do in the pattern screen: the instrument the Params screen shows (64..66 are the synth parts)
        int n = 0;
        is >> n;
        mtProject.values.lastUsedInstrument = uint8_t(n);
        return true;
    }
    if (c == "poke")
    {   // a host_poke.cpp action at this point of the script, the line is its text (the result goes to $UISHOT_POKE.out)
        std::string rest;
        std::getline(is, rest);
        hostPokeRun(rest.c_str());
        return true;
    }
    if (c == "status")
    {
        printf("pattern %d song %d master %d params %d play %d perf %d\n", mtInterface.isPatternScreenActive(), mtInterface.isSongScreenActive(), mtInterface.isMasterScreenActive(),
               mtInterface.isInstrumentParamsScreenActive(), mtInterface.isSamplePlayerScreenActive(), mtInterface.isPerformanceModeScreenActive());
        return true;
    }
    if (c == "power")
    {
        is >> a;
        if (!hostp::g_powerButtonCallback) { fprintf(stderr, "ui_shot: no power button callback registered\n"); return false; }
        if (a == "press" || a == "hold") hostp::g_powerButtonCallback(1);
        else if (a == "release") hostp::g_powerButtonCallback(0);
        else { fprintf(stderr, "ui_shot: power press|release|hold MS\n"); return false; }
        const uint32_t t0 = hostp::g_nowMs;
        int ms = 40;
        if (a == "hold") is >> ms;
        run(uint32_t(ms));
        printf("power %s: off %d after %d ms, sleep state %d, fm status %d op %d, progress %d, guard armed %d kicks %d\n", a.c_str(),
               hostp::g_powerOffCalls, hostp::g_powerOffCalls ? int(hostp::g_powerOffAtMs - t0) : -1, int(lowPower.getLowPowerState()),
               int(newFileManager.getStatus()), int(newFileManager.getCurrentOperation()), int(lowPower.getShutdownProgress()),
               hostp::g_guardArmed, hostp::g_guardKicks);
        return true;
    }
    if (c == "eeprom") { int n = 0; is >> n; hostp::g_startState = n; return true; }
    if (c == "reset") { hostp::resetCommand(); return true; }
    if (c == "stall") { int ms = 0; is >> ms; hostp::stall(uint32_t(ms)); return true; }
    if (c == "restartmode")
    {
#if __has_include("RestartMode.h")
        struct stat st;
        const bool file = stat((g_sdRootForStat + restart_mode::kFilePath).c_str(), &st) == 0;
#ifdef UISHOT_HAVE_FEATURES
        int cfg = -1;   // the on line of /Features/restart.cfg, the store of the mode in a features tree
        std::ifstream f(g_sdRootForStat + "/Features/restart.cfg");
        for (std::string l; f && std::getline(f, l);)
            if (l.rfind("on ", 0) == 0) cfg = atoi(l.c_str() + 3);
        printf("restart mode %d leaving %d platform %d file %d cfg %d\n", int(lowPower.isRestartMode()),
               int(lowPower.isLeavingRestartMode()), int(hostp::g_restartOnPowerOff), int(file), cfg);
#else
        printf("restart mode %d leaving %d platform %d file %d\n", int(lowPower.isRestartMode()), int(lowPower.isLeavingRestartMode()),
               int(hostp::g_restartOnPowerOff), int(file));
#endif
#else
        printf("restart mode: not in this tree\n");
#endif
        return true;
    }
    if (c == "audiomark") { printf("audio frames %llu\n", (unsigned long long)g_audioFrames); fflush(stdout); return true; }
#ifdef UISHOT_HAVE_SKIPBACK
    if (c == "skipback")
    {
        printf("skipback on %d saving %d channels %d filled %u capacity %u saved %u last %s\n", int(skipback::on()), int(skipback::saving()),
               int(skipback::channels()), skipback::filledFrames(), skipback::capacityFrames(), skipback::savedFrames(), skipback::lastPath());
        fflush(stdout);
        return true;
    }
#endif
#ifdef UISHOT_HAVE_FEATURES
    if (c == "features")
    {
        for (int i = 0; i < features::count(); i++)
            printf("feature %s on %d listed %d value %s\n", features::desc(i)->id, int(features::on(i)), int(features::listed(i)),
                   features::rowValue(i));
        return true;
    }
#endif
    if (c == "usbaudio")
    {
        printf("usb audio running %d flag %d\n", hostp::g_usbAudioRunning, hostp::g_usbAudioFlag);
        return true;
    }
    if (c == "eepromret") { int n = 0; is >> n; hostp::g_returnMirror = n; return true; }
    if (c == "usbflag") { int n = 0; is >> n; hostp::g_usbAudioFlag = n; return true; }
    if (c == "usbstream") { int n = 0; is >> n; hostp::g_usbAudioStreaming = n; return true; } // 1: the computer records now
    if (c == "usbmode") { printf("usb audio mode running %d eeprom %d\n", hostp::g_usbAudioRunningMode, hostp::g_usbAudioMode); return true; }
    if (c == "usbmc") { std::string rest; std::getline(is, rest); hostUsbMcCommand(rest); return true; }
    if (c == "sampletonehz")
    {   // sampletonehz N HZ AMP (not "sampletone": that one is the Tape FX proof's, 441 Hz, instrument 0 only): instrument N (0-based) becomes a 4 s sine in the sample memory, single shot, no file (the 8+3 tap proof)
        int n = 0, hz = 441, amp = 6000;
        is >> n >> hz >> amp;
        static int16_t tones[8][44100 * 4];
        int16_t* tone = tones[n & 7];
        for (size_t i = 0; i < 44100 * 4; i++) tone[i] = int16_t(double(amp) * std::sin(6.283185307179586 * double(hz) * double(i) / 44100.0));
        auto& in = mtProject.instrument[n];
        in = defaultInstrumentParams;   // a loaded instrument's values (bit depth 16, volume, centre pan), no file read
        in.isActive = 1;
        in.sample.address = tone;
        in.sample.length = 44100 * 4;
        in.playMode = playModeSingleShot;
        in.startPoint = 0;
        in.endPoint = MAX_16BIT;
        in.volume = 100;
        mtProject.sampleMemory[n].address[0] = tone;
        mtProject.sampleMemory[n].address[1] = tone;
        mtProject.sampleMemory[n].isStreamed = false;
        return true;
    }
    if (c == "samplekick")
    {   // samplekick N: instrument N (0-based) becomes a 1 s decaying 60 Hz kick (full level on its first frame, 40 ms decay), single shot, no file (export_bpm_sim.sh)
        int n = 0;
        is >> n;
        static int16_t kick[44100];
        for (size_t i = 0; i < 44100; i++) kick[i] = int16_t(20000.0 * std::exp(-double(i) / 44100.0 / 0.04) * std::cos(6.283185307179586 * 60.0 * double(i) / 44100.0));
        auto& in = mtProject.instrument[n];
        in = defaultInstrumentParams;
        in.isActive = 1;
        in.sample.address = kick;
        in.sample.length = 44100;
        in.playMode = playModeSingleShot;
        in.startPoint = 0;
        in.endPoint = MAX_16BIT;
        in.volume = 100;
        mtProject.sampleMemory[n].address[0] = kick;
        mtProject.sampleMemory[n].address[1] = kick;
        mtProject.sampleMemory[n].isStreamed = false;
        return true;
    }
    if (c == "poolslot")
    {   // poolslot N FRAMES: instrument N (0-based) becomes an active mono instrument of FRAMES frames at the packed place of the sample memory (after the
        // used slots below it), filled with a pattern that is never silent (synth_rec_sim.sh). The memory counts follow, as after a load
        int n = 0;
        unsigned frames = 0;
        is >> n >> frames;
        const uint32_t before = newFileManager.getSampleMemorySize(0, n - 1);
        int16_t* at = externalRamSampleMemory + before / 2;
        for (unsigned i = 0; i < frames; i++) at[i] = int16_t(((i * 37u + unsigned(n) * 1013u) & 0x3FFF) + 100);
        auto& in = mtProject.instrument[n];
        in = defaultInstrumentParams;
        in.isActive = 1;
        in.sample.address = at;
        in.sample.length = frames;
        in.playMode = playModeSingleShot;
        in.endPoint = MAX_16BIT;
        snprintf(in.sample.file_name, SAMPLE_NAME_SIZE, "Pool %d", n + 1);
        mtProject.sampleMemory[n] = strSampleMemory();
        mtProject.sampleMemory[n].address[0] = at;
        mtProject.sampleMemory[n].address[1] = at;
        mtProject.sampleMemory[n].ramFrames = frames;
        mtProject.sampleMemory[n].preCacheFrames = frames;
        mtProject.used_memory = newFileManager.getSampleMemorySize(0, kSampleInstrumentsCount - 1);
        return true;
    }
    if (c == "poolmax")
    {   // poolmax BYTES: the project's sample memory limit (a small one makes a take hit the pool end in a second)
        unsigned b = 0;
        is >> b;
        mtProject.max_memory = b;
        return true;
    }
    if (c == "pool")
    {   // pool [N]: the used bytes, the limit and a hash of the first N bytes of the sample memory (default the used bytes, FNV 1a), "pool used U max M hash H"
        static unsigned snap = 0;
        unsigned bytes = mtProject.used_memory;
        std::string w;
        if (is >> w)   // "pool N": the hash of the first N bytes, "pool snap": of the used bytes, and the count is kept, "pool old": of the kept count
        {
            if (w == "snap") snap = bytes;
            bytes = w == "old" || w == "snap" ? snap : unsigned(atoi(w.c_str()));
        }
        uint64_t h = 1469598103934665603ull;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(externalRamSampleMemory);
        for (uint32_t i = 0; i < bytes; i++) h = (h ^ b[i]) * 1099511628211ull;
        printf("pool used %u max %u hash %016llx\n", mtProject.used_memory, mtProject.max_memory, (unsigned long long)h);
        fflush(stdout);
        return true;
    }
    if (c == "instrinfo")
    {   // instrinfo N: "instrinfo N active A name NAME length L offset BYTES ram R stream S"
        int n = 0;
        is >> n;
        const auto& in = mtProject.instrument[n];
        printf("instrinfo %d active %d name %s length %u offset %lld ram %u stream %d\n", n, int(in.isActive), in.sample.file_name, in.sample.length,
               in.isActive ? (long long)((const uint8_t*)in.sample.address - (const uint8_t*)externalRamSampleMemory) : -1LL,
               mtProject.sampleMemory[n].ramFrames, int(mtProject.sampleMemory[n].isStreamed));
        fflush(stdout);
        return true;
    }
    if (c == "recstate")
    {   // recstate: the take of the Synth play page REC: "recstate S frames F cap C" (0 idle, 1 recording, 2 full)
        printf("recstate %d frames %u cap %u\n", int(synthplayrec::state()), synthplayrec::frames(), synthplayrec::capacity());
        fflush(stdout);
        return true;
    }
#ifdef FEATURE_USB_MSC
    // USB drive (USB_MSC=1): "drive on|off" enters / leaves Drive mode as Config > USB > Storage and its Exit do (the Exit key is
    // "tap F8"), "driveflag N" the EEPROM boot flag (takes effect at the next MCU start, "reset"), "drivestat" the gate counters,
    // "hostrm PATH" the computer deletes a file of the card (PATH relative to the SD folder, `put` writes one)
    if (c == "drive")
    {
        is >> a;
        const bool r = mtInterface.setFileTransferMode(a == "on");
        run(40);
        printf("drive %s: mode %d owned %d\n", a.c_str(), int(r), int(hostp::g_driveOwned));
        return true;
    }
    if (c == "driveflag") { int n = 0; is >> n; hostp::g_driveFlag = n; return true; }
    if (c == "drivestat")
    {
        printf("drivestat owned %d running %d backend %u refused %u stale %u by", int(hostp::g_driveOwned), hostp::g_driveRunning,
               hostp::g_driveBackend, hostp::g_driveRefused, hostp::g_driveStale);
        for (auto& kv : hostp::g_driveRefusedBy) printf(" %s:%u", kv.first.c_str(), kv.second);
        printf("\n");
        return true;
    }
    if (c == "hostrm") { is >> a; const std::string path = g_sdRootForStat + "/" + a; unlink(path.c_str()); return true; }
    if (c == "hostcp")
    {   // the computer copies FILE (a host path) to PATH of the card
        std::string dst;
        is >> a >> dst;
        const std::string cmd = "cp '" + a + "' '" + g_sdRootForStat + "/" + dst + "'";
        if (system(cmd.c_str()) != 0) { fprintf(stderr, "ui_shot: %s failed\n", cmd.c_str()); return false; }
        return true;
    }
#endif
    // the project tempo: "tempo N" sets it as the Song screen does and marks the project for the Workspace autosave, "proj" prints it
    if (c == "tempo") { float t = 0; is >> t; mtProject.values.globalTempo = t; newFileManager.setProjectStructChanged(); run(40); return true; }
    if (c == "proj") { printf("proj tempo %.1f\n", double(mtProject.values.globalTempo)); return true; }
    if (c == "resets") { printf("resets %d while off %d start state %d\n", hostp::g_mcuResets, hostp::g_resetsWhileOff, hostp::g_startState); return true; }
    if (c == "pattern")
    {   // the pattern on screen becomes N (what the song editor does for a slot: the edits of the old one are saved first)
        int n = 1;
        is >> n;
        newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
        mtProject.values.actualPattern = uint8_t(n);
        newFileManager.loadWorkspacePatternNow(uint8_t(n));
        sequencer.switchRamPatternsNow();
        patternEditor.refreshPattern();
        run(40);
        return true;
    }
    if (c == "plen")
    {   // the length in steps of the pattern on screen (all tracks), marked changed
        int n = 16;
        is >> n;
        sequencer.getPatternToUI()->track[0].length = uint8_t(n - 1);
        sequencer.updateTrackLengths(sequencer.getPatternToUI());
        newFileManager.setActualPatternStructChanged();
        run(40);
        return true;
    }
    if (c == "playlist")
    {   // playlist P1 P2 ...: the song's slots (the rest empty)
        int n, k = 0;
        memset(mtProject.song.playlist, 0, SONG_MAX);
        while (is >> n && k < SONG_MAX - 1) mtProject.song.playlist[k++] = uint8_t(n);
        newFileManager.setProjectStructChanged();
        return true;
    }
    if (c == "projsave" || c == "projopen")
    {   // the file manager calls of the Project screen's Save As and Open, run until the file manager is idle again
        // (features-140: the one handler of idea-followchord and parts, follow_chord_sim reads this line too)
        std::string dir, name;
        is >> dir >> name;
        const bool ok = c == "projsave" ? newFileManager.saveProject((char*)dir.c_str(), (char*)name.c_str())
                                        : newFileManager.openProject((char*)dir.c_str(), (char*)name.c_str());
        int waited = 0;
        do
        {
            run(50);
            waited += 50;
            // the end states the Project screen acknowledges (fProjEdit.cpp): the same here
            if (newFileManager.getStatus() == fmSaveEnd || newFileManager.getStatus() == fmLoadEnd) newFileManager.setStatus(fmIdle);
        } while (newFileManager.getStatus() != fmIdle && waited < 30000);
        run(500);
        printf("%s %s/%s started %d idle %d after %d ms (status %d op %d)\n", c.c_str(), dir.c_str(), name.c_str(), int(ok),
               int(newFileManager.getStatus() == fmIdle), waited, int(newFileManager.getStatus()), int(newFileManager.getCurrentOperation()));
        fflush(stdout);
        return true;
    }
#ifdef FEATURE_PARTS
    if (c == "padleds")
    {   // the pad LEDs that are on: pad:level (pad 0 top left, the level is the config brightness, 0 on the host)
        printf("padleds");
        for (int i = 0; i < 48; i++)
            if (hostp::g_padLed[i]) printf(" %d:%d", i, hostp::g_padLed[i] & 0xFF);
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "partsui")
    {   // what the Parts view says in text: the title bar (the mode), Build or Arrange, the cursor track (the half of the F-row)
        printf("partsui title [%s] build %d cursor %d\n", partsUiTitle(), int(songEditor.editMode), partsUiCursor());
        fflush(stdout);
        return true;
    }
    if (c == "sel")
    {   // the selected Part of the Parts view
        printf("partsel %d\n", partsSelected());
        fflush(stdout);
        return true;
    }
    if (c == "mute")
    {   // mute TRACK(0-15) 0/1: the track mute of the project values (as the Performance pads and the Mixer set it)
        int t = 0, v = 1;
        is >> t >> v;
        if (t < kSampleTracks) mtProject.values.trackMute[t] = uint8_t(v);
        else if (t < kAllTracks) mtProject.values.midiTrackMute[t - kSampleTracks] = uint8_t(v);
        return true;
    }
    if (c == "dlstat")
    {   // display list budget: words of the last frame and the largest so far, bytes per control slot (last update / largest)
        const EveModel::Stats& st = hostp::eve().stats;
        printf("dlstat frame %u max %u overflow %u songplayer %d partstable %d table %u\n", st.frameWords, st.frameWordsMax, st.dlOverflow, int(songEditor.songPlayerControl), int(songEditor.partsTableControl), songEditor.partsTableControl >= 0 ? st.slotBytes[songEditor.partsTableControl] / 4 : 0u);
        for (int i = 0; i < 80; i++)
            if (st.slotBytesMax[i]) printf("dlslot %d bytes %u max %u\n", i, st.slotBytes[i], st.slotBytesMax[i]);
        fflush(stdout);
        return true;
    }
    if (c == "parts")
    {   // the Parts state: list, mode, view, playback and the pattern editor's edit mode
        const parts::List& l = parts::list();
        printf("parts count %d mode %d view %d playing %d cur %d left %d queued %d edit %d actual %d seqsong %d play %d pos %d screen %s late %u stored %u saves %u\n", l.count,
               l.mode, int(songEditor.partsView), int(parts::playing()), parts::currentPart(), parts::passesLeft(), parts::queuedPart(),
               int(patternEditor.editMode), int(mtProject.values.actualPattern), int(sequencer.isSongMode()), int(sequencer.isPlay()),
               int(sequencer.getActualPos()), mtInterface.isSongScreenActive() ? "song" : (mtInterface.isPatternScreenActive() ? "pattern" : "other"), unsigned(parts::diag(0)),
               unsigned(parts::diag(1)), unsigned(parts::diag(2)));
        for (int i = 0; i < l.count; i++)
        {
            printf("part %d len %d rep %d flags %d baked %d src", i, l.part[i].length, l.part[i].repeats, l.part[i].flags, l.part[i].baked);
            for (int t = 0; t < parts::kTracks; t++) printf(" %d", l.part[i].src[t]);
            printf("\n");
        }
        printf("playlist");
        for (int k = 0; k < SONG_MAX && mtProject.song.playlist[k]; k++) printf(" %d", mtProject.song.playlist[k]);
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "partstart")
    {   // the start step of every track of every Part (one line a Part), as the Parts view keeps them
        const parts::List& l = parts::list();
        for (int i = 0; i < l.count; i++)
        {
            printf("partstart %d", i);
            for (int t = 0; t < parts::kTracks; t++) printf(" %d", l.start[i][t]);
            printf("\n");
        }
        fflush(stdout);
        return true;
    }
    if (c == "partset")
    {   // partset PART field value: src<T> / len / rep / flags (a direct edit, as the Parts view's own edits mark it)
        int i = 0, v = 0;
        std::string f;
        is >> i >> f >> v;
        parts::List& l = parts::list();
        if (i < 0 || i >= l.count) { fprintf(stderr, "ui_shot: no part %d\n", i); return false; }
        if (f == "len") l.part[i].length = uint8_t(v);
        else if (f == "rep") l.part[i].repeats = uint8_t(v);
        else if (f == "flags") l.part[i].flags = uint8_t(v);
        else if (f.rfind("src", 0) == 0) l.part[i].src[atoi(f.c_str() + 3)] = uint8_t(v);
        else if (f.rfind("start", 0) == 0) l.start[i][atoi(f.c_str() + 5)] = uint8_t(v);
        else { fprintf(stderr, "ui_shot: partset field %s\n", f.c_str()); return false; }
        parts::changed();
        if (songEditor.partsView) songEditor.partsReadList();
        run(40);
        return true;
    }
#endif
    if (c == "fm") { printf("fm status %d op %d\n", int(newFileManager.getStatus()), int(newFileManager.getCurrentOperation())); return true; }
    if (c == "fmset") { int n = 0; is >> n; newFileManager.setStatus(uint8_t(n)); return true; }
    if (c == "chmod")
    {
        std::string mode;
        is >> mode >> a;
        const std::string path = g_sdRootForStat + "/" + a;
        if (chmod(path.c_str(), mode_t(strtoul(mode.c_str(), nullptr, 8))) != 0) { fprintf(stderr, "ui_shot: chmod %s failed\n", path.c_str()); return false; }
        return true;
    }
    if (c == "put" || c == "cat")
    {
        is >> a;
        const std::string path = g_sdRootForStat + "/" + a;
        if (c == "cat")
        {
            if (FILE* f = fopen(path.c_str(), "rb"))
            {
                char buf[1024];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) fwrite(buf, 1, n, stdout);
                fclose(f);
            }
            return true;
        }
        std::string rest, text;
        std::getline(is, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        for (size_t i = 0; i < rest.size(); i++)
        {
            if (rest[i] == '\\' && i + 1 < rest.size() && (rest[i + 1] == 'n' || rest[i + 1] == 'r')) text += rest[++i] == 'n' ? '\n' : '\r';
            else text += rest[i];
        }
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { fprintf(stderr, "ui_shot: cannot write %s\n", path.c_str()); return false; }
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
        return true;
    }
    if (c == "touch") { is >> a; if (FILE* f = fopen(a.c_str(), "a")) { fclose(f); return true; } fprintf(stderr, "ui_shot: cannot create %s\n", a.c_str()); return false; }
    if (c == "rm") { is >> a; unlink(a.c_str()); return true; }
    if (c == "dump")
    {
        const auto& s = hostp::eve().stats;
        printf("swaps %d errors %d dlOverflow %u romText %u\n", hostp::eve().swaps(), hostp::eve().errors(), s.dlOverflow, s.romTextCalls);
        printf("coprocessor commands:");
        for (int i = 0; i < 256; i++) if (s.cmd[i]) printf(" %02X:%u", i, s.cmd[i]);
        printf("\ndisplay list opcodes:");
        for (int i = 0; i < 64; i++) if (s.dl[i]) printf(" %d:%u", i, s.dl[i]);
        printf("\nbitmap formats:");
        for (int i = 0; i < 32; i++) if (s.fmt[i]) printf(" %d:%u", i, s.fmt[i]);
        printf("\n%s", hostp::eve().log().c_str());
        return true;
    }
    static const char* const kSynthCmds[] = {"padon", "padoff", "voices", "view", "waitview", "encto", "maxreset", "poly", "pickmodel", "cardstats", "audiostats", "card"};
    for (const char* k : kSynthCmds)
        if (c == k)
        {
            if (hostSynthCommand(c, is)) return true;
            fprintf(stderr, "ui_shot: command '%s' failed\n", line.c_str());
            return false;
        }
#ifdef UISHOT_BRIDGE
    if (c == "plugrender")   // a render transcript on the installed card plugin under the bridge (host_plugrender.cpp)
    {
        if (hostPlugRenderCommand(c, is)) return true;
        fprintf(stderr, "ui_shot: command '%s' failed\n", line.c_str());
        return false;
    }
#endif
    {   // the FX3 / FX4 storage commands (host_poke_fx34_store.cpp)
        bool ok = false;
        if (hostFx34StoreCommand(line, &ok)) return ok;
    }
    fprintf(stderr, "ui_shot: unknown command '%s'\n", c.c_str());
    return false;
}
}   // namespace

void hostRun(uint32_t ms) { run(ms); }

// ---- --serve: the development console (console checkout host/ui_shot/host_serve.cpp, linked in a CONSOLE=1 build)
__attribute__((weak)) int hostServe(int, char**)
{
    fprintf(stderr, "ui_shot: --serve needs the development console: make CONSOLE=1 with the console checkout (DEVTOOLS_DIR)\n");
    return 2;
}

static void onCrash(int sig)
{
    void* bt[40];
    const int n = backtrace(bt, 40);
    dprintf(2, "ui_shot: signal %d, load address 0x%lx (atos -o ui_shot -arch arm64 -l ADDR frames), backtrace:\n", sig, (unsigned long)(0x100000000ul + (unsigned long)_dyld_get_image_vmaddr_slide(0)));
    backtrace_symbols_fd(bt, n, 2);
    _exit(139);
}

int main(int argc, char** argv)
{
    signal(SIGSEGV, onCrash);
    signal(SIGBUS, onCrash);
    signal(SIGABRT, onCrash);
    if (argc >= 3 && std::string(argv[1]) == "--serve") return hostServe(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: ui_shot SD_DIR \"script\"   or   ui_shot --serve SD_DIR\n"); return 2; }
    const std::string sd = argv[1];
    mkdir(sd.c_str(), 0755);
    g_sdRootForStat = sd;
    hostp::setSdRoot(sd);
    Platform* p = hostp::platform();
    Tracker* t = Tracker::getInstance();
    hostp::bootModel();
    // UISHOT_SHIFT_BOOT=1: Shift held while the unit powers up (the plugin host's safe mode), released once the pattern screen is up
    const bool shiftBoot = getenv("UISHOT_SHIFT_BOOT") != nullptr;
    if (shiftBoot) hostp::setHeld(29, true);
    // UISHOT_REC_BOOT=1: Record held while the unit powers up (the restart mode, RestartMode.h), released once the pattern screen is up
    const bool recBoot = getenv("UISHOT_REC_BOOT") != nullptr;
    if (recBoot) hostp::setHeld(28, true);
    t->getStartDisplayFunc()(p);
    t->init(p);
    // the start sequence: config, project open, start screen, then the pattern screen
    for (int i = 0; i < 20000 && !mtInterface.isPatternScreenActive(); i++)
    {
        hostp::advance(5);
        t->update();
    }
    if (!mtInterface.isPatternScreenActive()) { fprintf(stderr, "ui_shot: the pattern screen never came up\n"); return 1; }
    samplePlayback.editorInstrument = &mtProject.instrument[0];
    if (shiftBoot) hostp::setHeld(29, false);
    if (recBoot) hostp::setHeld(28, false);
    // UISHOT_NO_SETTLE=1: the script starts right after the pattern screen came up, inside the key window of a develop boot (the console
    // module's slot is still "about to load": MOD/IT says busy, modit_popup_sim.sh)
    if (!getenv("UISHOT_NO_SETTLE")) run(6000);   // past the plugin host's boot key window (Shift in the first seconds means safe mode)
    std::istringstream is(argv[2]);
    std::string line;
    int rc = 0;
    while (std::getline(is, line, ';'))
        if (!command(line)) rc = 1;
    if (hostp::audioViolated()) { fprintf(stderr, "ui_shot: %u card calls from the audio interrupt\n", hostp::g_card.audioViolations); rc = 3; }
    if (hostp::eve().errors()) { fprintf(stderr, "ui_shot: %d model errors\n%s", hostp::eve().errors(), hostp::eve().log().c_str()); rc = 1; }
    return rc;
}
