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
//   midirows           print the rows Config > MIDI lists (linked pairs hide the follower's Track N in channel row)
//   projrows           print the name and the value text of every row of the Project Settings page, as the screen reads them (the value pointers are all
//                      taken before any is printed, so a row that shares a buffer with another shows its text), and the stereo pair mask
//   snapzero 0|1       the project snap to zero crossing setting (stereo_fx_sweep.sh renders with it off)
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
#include "stereoPairs.h"
#include "VoiceStreamManager.h"
#include "mtAudioEngine.h"
#include "mtAudioExport.h"
#include "mtFollowChord.h"
#include "mtNoteName.h"
#include "mtTuneScale.h"
#include "play_memory.h"

#include "Tracker.h"
#include "core/interface.h"
#include "display.h"
#include "mtStructs.h"
#include "fileManager.h"
#ifdef FEATURE_MODIT_MODULE
#include "ext_hooks.h"
#endif
#include "mtSleep.h"
#include "ThemeManager.h"
#include "Themes.h"
#include "mtTestGlue.h"
#include "mtTest.h"
#include "mtMidi.h"
#include "host_imxmidi.h"
#include "mtSequencer.h"
#include "fx34.h"
#if defined(FEATURE_PLUG_HOST)   // ff_config.h FEATURE_TABLES (Table lite)
#include "tables.h"
#endif
#include "patternEditor/patternEditor.h"
#include "ff_host_seam.h"
#include "configEditor/configMenu.h"
#include "configEditor/configMenuActions.h"
extern cMenuGroup menuMidi;   // configMenu.cpp, for midirows
#ifdef FEATURE_FF_CORE
extern cMenuGroup menuProjectPage;   // configMenu.cpp, for projrows
#endif
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
#include <deque>
#include <fstream>
#include <vector>

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
#include <iomanip>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
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
extern "C" void hostTapAttachAmp(int track);     // host_poke_fx.cpp: "audiostat tapamp N"
extern "C" void hostTapClear();
extern "C" size_t hostTapCount();
extern "C" void hostTapGet(size_t i, float* rms, int* zc);
// "audio 1": the audio callback (the firmware's own Tracker::updateAudioBuffers) runs between the main loop passes, the blocks that fit
// into each 5 ms step (128 frames at 44,117.6 Hz = 2,901.3 us), so the sequencer ticks and plays. g_audioUs: rendered audio time,
// the time stamp of the "midilog" lines (host_platform.cpp).
double g_audioUs = 0;
bool g_midiLog = false;
bool g_usbBusLost = false;   // host_platform.cpp's HUsbMidi::takeBusLost reads it
extern std::deque<int> g_usbQueue;   // host_platform.cpp: the fake USB MIDI queue of "usbq"
extern std::vector<int> g_usbReadLog;
extern uint32_t g_usbReadCostUs, g_usbExtraUs;
extern uint32_t g_pulseCostUs, g_pulseBaseTicks;
extern uint32_t hostSeqTimerTicks();   // host_poke.cpp (timerTicks is private to the sequencer)
extern volatile uint32_t midiUsbDeferredBlocks;   // core/source/mtMidi.cpp
extern Sequencer::strPattern songTrackCopy[2]; // fileManagerSong.cpp: the scratch "patfile" reads into

namespace
{
bool g_audio = false;
double g_audioAcc = 0;
// "audiostat 1" .. "audiostat dump": the level and the zero crossings of every audio block the callback renders (left + right of the
// final mix, 128 frames per block). What the Tape FX proof reads: the level over time and the pitch as crossings per block.
bool g_stat = false;
struct BlockStat { float rms; int zc; };
std::vector<BlockStat> g_blocks;
// "audiostat lrdump": the same per side (the stereo pair proofs): rms and zero crossings of the left and the right bus
struct BlockLr { float rmsL, rmsR; int zcL, zcR; };
std::vector<BlockLr> g_blocksLr;
void statBlock(const int16_t* l, const int16_t* r)
{
    {
        double sl = 0.0, sr = 0.0;
        int zl = 0, zr = 0;
        for (int i = 0; i < 128; i++)
        {
            sl += double(l[i]) * double(l[i]);
            sr += double(r[i]) * double(r[i]);
            if (i > 0 && ((l[i - 1] < 0) != (l[i] < 0))) zl++;
            if (i > 0 && ((r[i - 1] < 0) != (r[i] < 0))) zr++;
        }
        g_blocksLr.push_back({float(std::sqrt(sl / 128.0)), float(std::sqrt(sr / 128.0)), zl, zr});
    }
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
#ifdef UISHOT_PLUS
            uishotImxMidiPump();   // the USB host reads what the real driver queued for MIDI out ("midilog" lines)
#endif
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
        // Tracker Mini only (PLATFORM=IMX PRODUCT=MINI, interfaceDefs.h interfaceButton33..42): the centre key, Menu, Fn1..Fn4, master volume ++ + - --
        {"Mid", 33}, {"Menu", 34}, {"Fn1", 35}, {"Fn2", 36}, {"Fn3", 37}, {"Fn4", 38}, {"Plus10", 39}, {"Plus1", 40}, {"Minus1", 41}, {"Minus10", 42},
    };
    auto it = m.find(n);
    return it == m.end() ? -1 : it->second;
}

// ---- the interrupt race (midi_isr_race_sim.sh). The preempt points of core/include/ff_host_seam.h: at a pattern view redraw of the main loop
// the "audio interrupt" fires, which runs what midiUpdate runs there (a DIN note on and off through the handlers of mtMidi.cpp). A redraw
// that starts while the interrupt runs is a redraw the interrupt cut into the main loop's: counted, the row wants none.
static int g_raceArm;         // interrupts still to fire at the next preempt point
static bool g_raceInIsr;      // the interrupt runs
static int g_raceFired, g_raceReentered;
extern "C" void uishotPreempt(int point)
{
    if (point != FF_PREEMPT_PATTERN_REFRESH) return;
    if (g_raceInIsr)
    {
        g_raceReentered++;
        return;
    }
    if (!g_raceArm) return;
    g_raceArm--;
    g_raceFired++;
    g_raceInIsr = true;
    handleJackNoteOn(1, 60, 100);
    handleJackNoteOff(1, 60, 0);
    g_raceInIsr = false;
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
    if (c == "trackch")
    {   // trackch TRACK CH: Config > MIDI, the track listens to its own channel (0: off), notes in from USB and jack (capture_sim.sh section 13)
        int t = 0, ch = 0;
        is >> t >> ch;
        mtConfig.midi.notesInMode = notesIn_usb_and_jack;
        if (t >= 0 && t < kSampleTracks) mtConfig.midi.trackNotesInChannel[t] = uint8_t(ch);
        return true;
    }
    if (c == "ccin")
    {   // ccin MODE CH: Config > MIDI > CC in (0 off, 1 USB, 2 DIN, 3 both) on channel CH, the stock enum
        int m = 0, ch = 1;
        is >> m >> ch;
        mtConfig.midi.ccInMode = decltype(mtConfig.midi.ccInMode)(m);
        mtConfig.midi.ccInChannel = uint8_t(ch);
        return true;
    }
    if (c == "notesin")
    {   // notesin MODE: Config > MIDI > Notes in (0 off, the stock enum)
        int m = 0;
        is >> m;
        mtConfig.midi.notesInMode = decltype(mtConfig.midi.notesInMode)(m);
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
    if (c == "midipc" || c == "dinpc")
    {   // midipc CH PROGRAM / dinpc CH PROGRAM: a MIDI Program Change on USB / on DIN (Config > MIDI > Program in stays as "pcin" set it)
        int ch = 1, p = 0;
        is >> ch >> p;
        if (c == "midipc") handleUsbProgramChange(uint8_t(ch), uint8_t(p));
        else handleJackProgramChange(uint8_t(ch), uint8_t(p));
        run(20);
        return true;
    }
    if (c == "pcin")
    {   // pcin 0|1: Config > MIDI > Program in (the value the menu row writes)
        int v = 0;
        is >> v;
        mtConfig.midi.programIn = uint8_t(v);
        return true;
    }
    if (c == "mpeout")
    {   // mpeout N: Project Settings > MPE out (0 Off, 1..15 member channels), through the row's own action: the zone and its configuration messages
        int v = 0;
        is >> v;
        projMpeOut = uint8_t(v);
        setProjMpeOut();
        return true;
    }
    // The Synth play page's inputs (synth_play_sim.sh), as the handlers of mtMidi.cpp get them from the MIDI libraries. Unlike midion they leave
    // the Config > MIDI settings alone: the page has its own Input and Channel
    if (c == "usbmidi" || c == "dinmidi")
    {   // usbmidi S D1 [D2] / dinmidi S D1 [D2]: ONE RAW MIDI MESSAGE (status byte and data bytes, decimal or 0x..) as the USB host / a DIN cable
        // sends it. The same script runs on the original Tracker, the Tracker+ and the Mini. PLATFORM=IMX: the bytes go BELOW the real driver
        // (host_imxmidi.cpp: a USB MIDI event packet into usb_midi.c, a byte into the MidiUart ring), so UsbMidiImpl::read / the Arduino MIDI
        // library parse and dispatch them (pitch bend, channel pressure, 1 based channel, running status). The original has no such driver in
        // the sim: a plain decoder of the message hands it to the mtMidi.cpp handlers the MK66 drivers call. Not a note on the Config: like midiono
        uint8_t m[3] = {0, 0, 0};
        int n = 0, v = 0;
        while (n < 3 && (is >> std::setbase(0) >> v)) m[n++] = uint8_t(v);
        if (n == 0 || m[0] < 0x80) return false;
#ifdef UISHOT_PLUS
        if (c == "usbmidi") uishotImxUsbIn(m, n);
        else uishotImxDinIn(m, n);
#else
        const bool usb = c == "usbmidi";
        const uint8_t ch = uint8_t((m[0] & 15) + 1);
        switch (m[0] & 0xF0)
        {
            case 0x80: if (usb) handleUsbNoteOff(ch, m[1], 0); else handleJackNoteOff(ch, m[1], 0); break;
            case 0x90:
                if (m[2] == 0) { if (usb) handleUsbNoteOff(ch, m[1], 0); else handleJackNoteOff(ch, m[1], 0); }
                else if (usb) handleUsbNoteOn(ch, m[1], m[2]);
                else handleJackNoteOn(ch, m[1], m[2]);
                break;
            case 0xB0: if (usb) handleUSBControlChange(ch, m[1], m[2]); else handleJackControlChange(ch, m[1], m[2]); break;
            case 0xC0: if (usb) handleUsbProgramChange(ch, m[1]); else handleJackProgramChange(ch, m[1]); break;
            case 0xD0: if (usb) handleUsbChannelPressure(ch, m[1]); else handleJackChannelPressure(ch, m[1]); break;
            case 0xE0:
            {
                const int bend = int((m[1] & 0x7f) | ((m[2] & 0x7f) << 7)) - 8192;
                if (usb) handleUsbPitchBend(ch, bend); else handleJackPitchBend(ch, bend);
                break;
            }
            default: break;
        }
#endif
        run(20);
        return true;
    }
    if (c == "midibend" || c == "dinbend")
    {   // midibend CH VALUE / dinbend CH VALUE: pitch bend -8192..8191 on USB / on DIN
        int ch = 1, v = 0;
        is >> ch >> v;
        if (c == "midibend") handleUsbPitchBend(uint8_t(ch), v);
        else handleJackPitchBend(uint8_t(ch), v);
        run(20);
        return true;
    }
    if (c == "midipress" || c == "dinpress")
    {   // midipress CH VALUE / dinpress CH VALUE: channel pressure (0xD0) 0..127 on USB / on DIN (the MPE pressure, synth_mpe_sim.sh)
        int ch = 1, v = 0;
        is >> ch >> v;
        if (c == "midipress") handleUsbChannelPressure(uint8_t(ch), uint8_t(v));
        else handleJackChannelPressure(uint8_t(ch), uint8_t(v));
        run(20);
        return true;
    }
    if (c == "mpeburst")
    {   // mpeburst SECONDS RATE FIRST LAST: RATE MPE expression events per second for SECONDS (bend, pressure and slide in turn, round the member channels
        // FIRST..LAST), each handled by the same USB handlers the driver calls, then 5 ms of the main loop and audio per step. Prints the events sent and
        // the host time the handlers took (informational: the audio block of the unit is another machine)
        int sec = 2, rate = 1500, first = 2, last = 6;
        is >> sec >> rate >> first >> last;
        const int span = last - first + 1;
        uint32_t seed = 12345u;
        long sent = 0;
        int lastBend[17] = {}, lastPress[17] = {};
        double handlerNs = 0, worstNs = 0;
        for (int step = 0; step < sec * 200; step++)   // run() moves the clock in steps of 5 ms (1 or 2 audio blocks): the events of a step arrive together
        {
            const int n = (step + 1) * rate / 200 - step * rate / 200;
            for (int k = 0; k < n; k++)
            {
                seed = seed * 1664525u + 1013904223u;
                const uint8_t ch = uint8_t(first + int(seed >> 24) % span);
                const int kind = int(sent % 3);
                const int r = int((seed >> 8) & 0x3FFF);
                const auto t0 = std::chrono::steady_clock::now();
                if (kind == 0) { handleUsbPitchBend(ch, r - 8192); lastBend[ch] = r - 8192; }
                else if (kind == 1) { handleUsbChannelPressure(ch, uint8_t(r & 0x7F)); lastPress[ch] = r & 0x7F; }
                else handleUSBControlChange(ch, 74, uint8_t(r & 0x7F));
                const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
                handlerNs += ns;
                worstNs = ns > worstNs ? ns : worstNs;
                sent++;
            }
            run(5);
        }
        printf("mpeburst sent %ld events in %d s, handler mean %.0f ns worst %.0f ns\n", sent, sec, handlerNs / double(sent ? sent : 1), worstNs);
        for (int ch = first; ch <= last; ch++)   // the last value sent on each channel, what the voices must hold when no event was lost
            printf("mpeburst last ch %d bend %d press %d\n", ch, lastBend[ch], lastPress[ch]);
        return true;
    }
    if (c == "midirace")
    {   // midirace N: N interrupts, each at a pattern view redraw of the main loop (the MIDI input takes notes on both inputs, omni)
        int n = 1;
        is >> n;
        mtConfig.midi.notesInMode = notesIn_usb_and_jack;
        mtConfig.midi.notesInChannel = 0;
        g_raceArm = n;
        g_raceFired = g_raceReentered = 0;
        for (int i = 0; i < 4 * n && g_raceArm; i++)
        {
            patternEditor.refreshPatternFromOutsideModule();   // a main loop redraw (as a key or the sequencer asks for one)
            run(20);
        }
        run(40);
        printf("midirace fired=%d reentered=%d\n", g_raceFired, g_raceReentered);
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
    if (c == "usbunplug")
    {   // usbunplug: the USB cable is pulled, no note off comes. The bus lost flag is set and the real midiUpdate of the audio interrupt takes it
        g_usbBusLost = true;
        midiUpdate();
        return true;
    }
    if (c == "usbq")
    {   // usbq N COST_US: N fake USB MIDI events (numbered on from the last) join the receive queue, each read costs COST_US on the fake micros clock
        int n = 1, cost = 0;
        is >> n >> cost;
        g_usbReadCostUs = uint32_t(cost);
        static int nextId = 1;
        for (int i = 0; i < n; i++) g_usbQueue.push_back(nextId++);
        return true;
    }
    if (c == "usbdrain")
    {   // usbdrain K: K audio blocks worth of midiUpdate on the real code, one line per block, then the order of everything read
        int k = 1;
        is >> k;
        for (int i = 0; i < k; i++)
        {
            g_usbExtraUs = 0;
            const size_t before = g_usbReadLog.size();
            midiUpdate();
            printf("usbdrain block %d handled %zu left %zu spent %u\n", i, g_usbReadLog.size() - before, g_usbQueue.size(), g_usbExtraUs);
        }
        bool inOrder = true;
        for (size_t i = 0; i < g_usbReadLog.size(); i++) inOrder = inOrder && g_usbReadLog[i] == int(i) + 1;
        printf("usbdrain total %zu in_order %d deferred %u\n", g_usbReadLog.size(), int(inOrder), (unsigned)midiUsbDeferredBlocks);
        return true;
    }
    if (c == "clockcost")
    {   // clockcost US: every external clock pulse the sequencer plays (96 nano ticks) costs US on the fake micros clock, 0 turns it off (midi_isr_cap_sim.sh)
        int us = 0;
        is >> us;
        g_pulseBaseTicks = hostSeqTimerTicks();
        g_pulseCostUs = uint32_t(us);
        return true;
    }
    if (c == "audioblocks")
    {   // audioblocks K: K audio blocks of 128 frames with no main loop pass (the audio interrupt only), then "poke clockstate" reads the sequencer
        int k = 1;
        is >> k;
        static int16_t l[256], r[256];
        for (int i = 0; i < k; i++) Tracker::getInstance()->updateAudioBuffers(l, r, 128);
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
    if (c == "view")
    {   // the pattern editor columns (linked stereo pairs hide the follower): first visible track, the hidden mask, the label of tracks 0-3, the selection
        const auto& tp = cPatternEditor::trackerPattern;
        printf("view mask %d lead0 %d first %d hidden 0x%x names %s %s %s %s sel %d %d..%d\n", int(stereopairs::g_mask), int(stereopairs::isLeader(0)), tp.firstVisibleTrack, unsigned(tp.hidden), tp.tracksNames[0], tp.tracksNames[1],
               tp.tracksNames[2], tp.tracksNames[3], tp.selectState, tp.selectStartTrack, tp.selectEndTrack);
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
    if (c == "theme")
    {   // "theme N": Config > Theme row N (0 to 2 built in, 3 on the card's /Themes/*.json) through ThemeManager::applyTheme. Prints the result, the
        // theme in use, the background colour and a checksum of themeCustom (what the JSON parser wrote), and whether extras.plg's theme hook is set
        int n = 0;
        is >> n;
        const bool ok = ThemeManager::applyTheme(uint8_t(n));
        uint32_t sum = 0;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&themeCustom);
        for (size_t i = 0; i < sizeof(themeCustom); i++) sum = sum * 31u + b[i];
        printf("theme %d ok %d current %d count %d bg %06lx custom %08lx hook %s\n", n, int(ok), int(ThemeManager::getCurrentThemeIndex()),
               int(ThemeManager::getThemeCount()), static_cast<unsigned long>(themeCustom.backgroundColor & 0xFFFFFF), static_cast<unsigned long>(sum),
#ifdef FEATURE_THEME_MODULE
               ext_hooks.theme ? "set" : "null"
#else
               "resident"
#endif
        );
        fflush(stdout);
        return true;
    }
    if (c == "selftest")
    {   // the factory test's start code on the Config screen (fConfigEdit.cpp functPads): the pads through the image's glue, then the event's start
        bool started = false;
        for (uint8_t pad : startCode)
            if (selftestCombo(pad, false) && selftestReady())
            {
                selftestStart(mtInterface.ip);
                started = true;
                break;
            }
        run(300);
        printf("selftest started %d status %d phase %d hook %s\n", int(started), mtTest.getMainStatus(), mtTest.getTestPhase(),
#ifdef FEATURE_SELFTEST_MODULE
               ext_hooks.selftest ? "set" : "null"
#else
               "resident"
#endif
        );
        fflush(stdout);
        return true;
    }
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
    if (c == "wsave")
    {   // the Workspace autosave of the Project screen (WorkspaceProjectSave.cpp): project.mt of the open project written now. The device does it after
        // an import (the unit's project.mt then holds the imported play order), the host import scripts end before it (modit_seeded_sim.sh)
        newFileManager.saveProjectFileToWorkspace();
        run(200);
        printf("wsave done\n");
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
        else if (a == "tapamp") { int t = 0; is >> t; hostTapAttachAmp(t); }
        else if (a == "1") { g_blocks.clear(); g_blocksLr.clear(); hostTapClear(); g_stat = true; }
        else if (a == "lrdump")
        {
            printf("audiostat blocks %zu\n", g_blocksLr.size());
            for (size_t i = 0; i < g_blocksLr.size(); i++) printf("lr %zu rmsl %.1f rmsr %.1f zcl %d zcr %d\n", i, g_blocksLr[i].rmsL, g_blocksLr[i].rmsR, g_blocksLr[i].zcL, g_blocksLr[i].zcR);
            fflush(stdout);
        }
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
            printf("cap %d %s v%d pos %d ustep %d instr %d pat %d flags %d src %d dest %d t %u\n", e.note, (e.vel & kCapOn) ? "on" : "off", e.vel & 0x7F, e.pos,
                   e.uStep, e.instrument, e.pattern, e.flags, e.src & 7, (e.src >> 3) & 15, unsigned(e.tUs));
        }
        fflush(stdout);
        return true;
    }
    if (c == "capclear") { captureRingClear(); return true; }
    if (c == "capgen")
    {   // "capgen COUNT PER_TICK": COUNT events straight into the ring through the real tap (USB source), PER_TICK of them, then 60 ms of run time (one
        // indicator tick). Event i: note 24 + (i / 2 * 5) % 60, on when i is even, the off of the same note when odd. The take file tests (capture_sim.sh section 16) need
        // more events than a script of midion lines can hold
        int count = 0, per = 100;
        is >> count >> per;
        if (per < 1) per = 1;
        for (int i = 0, k = 0; i < count; i++)
        {
            sequencer.captureTap(uint8_t(24 + (i / 2 * 5) % 60), 100, (i & 1) == 0, kCapSrcUsb);
            if (++k == per) { k = 0; run(60); }
        }
        run(60);
        return true;
    }
    if (c == "capfile")
    {   // the Capture take file writer's counters (capfile::Stat): state (0 idle, 1 creating, 2 ready, 3 failed), flushes, sectors, skips, events written, lost,
        // pending, file take id, headers, create steps, errors
        printf("capfile");
        const char* const names[] = {"state", "flushes", "sectors", "skips", "written", "lost", "pending", "take", "headers", "created", "errors"};
        for (uint32_t k = 0; k < capfile::kStatCount; k++) printf(" %s %u", names[k], unsigned(Sequencer::captureOp(CAPTURE_OP_FILE + k)));
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "capflush")
    {   // the flush before the card is lent (what handleFileTransferMode calls): 1 when nothing is left
        printf("capflush %u\n", unsigned(Sequencer::captureOp(CAPTURE_OP_FLUSHNOW)));
        fflush(stdout);
        return true;
    }
    if (c == "capread")
    {   // the take file as the reader sees it: "capread" the header and the extent line, "capread all" also one line per event (note, on|off, velocity, seq, take, pos, time)
        std::string all;
        is >> all;
        capfile::TakeInfo ti;
        if (!capfile::readOpen(&ti)) { printf("capread none\n"); fflush(stdout); return true; }
        printf("capread take %d lost %u/%d bpm100 %u hash %08x start %u span %u valid %u newest %u oldest %u\n", int(ti.take), unsigned(ti.lost), int(ti.lostFlag), unsigned(ti.bpm100),
               unsigned(ti.nameHash), unsigned(ti.startUs), unsigned(ti.span), unsigned(ti.valid), unsigned(ti.newest), unsigned(ti.oldest));
        if (all == "all")
        {
            static CaptureEvent buf[64];
            for (uint32_t from = 0; from < ti.span;)
            {
                const uint32_t n = capfile::readEvents(ti, from, buf, 64);
                if (!n) break;
                for (uint32_t i = 0; i < n; i++)
                    printf("capev %u %d %s v%d seq %u take %d pos %d t %u\n", unsigned(from + i), int(buf[i].note), (buf[i].vel & kCapOn) ? "on" : "off", buf[i].vel & 0x7F, unsigned(buf[i].seq),
                           int(buf[i].take), int(buf[i].pos), unsigned(buf[i].tUs));
                from += n;
            }
        }
        fflush(stdout);
        return true;
    }
    if (c == "fspool")
    {   // the device's SdFile pool as the host models it: slots in use now, the most in use at once, opens refused for want of a slot
        printf("fspool used %d peak %d refused %d held%s\n", hostp::g_filePoolUsed, hostp::g_filePoolPeak, hostp::g_filePoolRefused, hostp::filePoolHeldPaths().c_str());
        fflush(stdout);
        return true;
    }
    if (c == "fsbusy") { int n = 0; is >> n; hostp::g_asyncBusy = n; return true; }   // isAsyncReadBusy() true: a sample stream reads the card
    if (c == "fmstatus") { int n = 0; is >> n; newFileManager.setStatus(uint8_t(n)); return true; }   // the file manager's status (fileManagerStatus), 0 = idle
    if (c == "ftmode")
    {   // the USB storage mode on or off without the UI (Interface::setFileTransferMode): the Capture take file is flushed before the card is lent
        std::string onoff;
        is >> onoff;
        const bool r = mtInterface.setFileTransferMode(onoff == "on");
        run(40);
        printf("ftmode %s: %d\n", onoff.c_str(), int(r));
        fflush(stdout);
        return true;
    }
    if (c == "spillpace")
    {   // the spill's pacing of file writes (mtSequencer_capture.cpp): "spillpace 1" counts every pass as a slow one, plain "spillpace" prints the count
        int n = -1;
        is >> n;
        if (n >= 0) { capspill::paceAlways = n != 0; capspill::pacedPasses = 0; }
        else { printf("spillpace %u\n", unsigned(capspill::pacedPasses)); fflush(stdout); }
        return true;
    }
    if (c == "capind")
    {   // the Capture indicator of the title bar: the state it is drawn in (0 hidden, 1 empty, 2 stale, 3 ready, 4 past the gap), how often it was
        // redrawn, the take id (0: none yet), the events of the take in the ring, the events ever written
        printf("capind state %d draws %d take %d ring %d seq %u\n", int(titleBar.captureStateDrawn()), int(titleBar.captureDraws()),
               int(Sequencer::captureOp(CAPTURE_OP_TAKE)), int(captureRingCount()), unsigned(captureRingSeq()));
        fflush(stdout);
        return true;
    }
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
    if (c == "tune")
    {   // the project tuning (Project Settings > Tuning, core/source/mtTuning.h): "tune" prints it, "tune N [PERIOD_MC [ROOT [HZ_E4]]]" sets it as the rows
        // do (period in 1e-3 cent, Hz in 1e-4 Hz, the missing ones stay) and fills the sample tables and the pads again, "tune reset" is the default,
        // "tune name K" prints the screen name of key K, "tune off K" the synth offset of key K in semitones, "tune rate K" the sample rate ratio of key K
        std::string w;
        is >> w;
        if (w == "name" || w == "off" || w == "rate")
        {
            int k = 0;
            is >> k;
            if (w == "name") printf("tune name %d %s\n", k, noteName(k));
            else if (w == "off") printf("tune off %d %.6f\n", k, double(tuning::synthOffset(k)));
            else printf("tune rate %d %.9g\n", k, double(AudioPlayMemory::notes[k]));
            fflush(stdout);
            return true;
        }
        if (w == "names")
        {   // "tune names": the Note names row (0 degrees, 1 ups and downs), whether N has ups and downs names, whether they show now
            printf("tune names %d ok %d active %d\n", int(tuning::cfg.names), int(tuning::cfg.udOk), int(tuning::udActive()));
            fflush(stdout);
            return true;
        }
        if (w == "scale")
        {   // "tune scale msg": the reason of the last refused file, "tune scale": the Scala state, "tune scale file F NAME N PERIOD_MC D1 D2 ..." (F 1 = a Scala tuning, the degrees in 1e-3 cent)
            std::string sub;
            is >> sub;
            if (sub == "msg") { printf("tune scale msg %s\n", tunescale::message()); fflush(stdout); return true; }
            printf("tune scale file %d name %s n %d period %u deg", int(tuning::cfg.file), tuning::cfg.file ? tuning::fileName : "-", int(tuning::cfg.n), unsigned(tuning::cfg.periodMc));
            if (tuning::cfg.file)
                for (int i = 1; i < tuning::cfg.n; i++) printf(" %u", unsigned(tuning::degMc[i]));
            printf("\n");
            fflush(stdout);
            return true;
        }
        if (w == "reset") { tuning::reset(); tuningChanged(); }
        else if (!w.empty())
        {
            unsigned long p = tuning::cfg.periodMc, h = tuning::cfg.hzE4;
            int root = tuning::cfg.root;
            is >> p >> root >> h;
            tuning::set(atoi(w.c_str()), uint32_t(p), root, uint32_t(h));
            tuningChanged();
        }
        printf("tune n %d period %u root %d hz %u\n", int(tuning::cfg.n), unsigned(tuning::cfg.periodMc), int(tuning::cfg.root), unsigned(tuning::cfg.hzE4));
        fflush(stdout);
        return true;
    }
    if (c == "projvals")
    {   // the nine per-project rows of the Project Settings page and the global Performance presets, one line (projset_sim.sh)
        const auto& v = mtProject.values;
        printf("projvals div %d aa %d lim %d sends %d head %d s2z %d scale %d root %d lay %d perf %d\n", int(v.patternDiv), int(v.antialiasingEnable), int(v.limiterMode),
               int(v.sendsMode), int(v.isHeadroomEnabled), int(v.isSnapToZeroEnabled), int(v.padBoardScale), int(v.padBoardRootNote), int(v.padBoardNoteOffset),
               int(mtConfig.general.performanceSource));
        fflush(stdout);
        return true;
    }
    if (c == "scale")
    {   // the project's Notes scale and Root note (Project Settings page, File screen F7), as the two list items leave them
        int root = 24, sc = 0;
        is >> root >> sc;
        mtProject.values.padBoardRootNote = uint8_t(root);
        mtProject.values.padBoardScale = uint8_t(sc);
        return true;
    }
#ifdef FEATURE_FF_CORE   // Scale snap and Snap FX are FF core features (project.mtff)
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
#if defined(FEATURE_PLUG_HOST)   // ff_config.h FEATURE_TABLES: the table pokes
    if (c == "table")
    {   // tables (core/source/tables.h): "table N TIC LOOP LEN" the header of table N (1..16), "table row N R NOTE VOL FXTYPE FXVAL" one row
        // (NOTE -128 empty, VOL 255 empty), "table bind INSTR N" (instrument index from 0, N 0 none), "table run" prints every track's runner,
        // "table dump N" / "table dump bind I" print a table / a binding, "table fxok ID" whether a row takes that FX
        std::string w;
        is >> w;
        if (w == "row")
        {
            int n = 1, r = 0, note = -128, vol = 255, ft = 0, fv = 0;
            is >> n >> r >> note >> vol >> ft >> fv;
            tables::pool().table[n - 1].row[r] = tables::rowSanitize(tables::Row{int8_t(note), uint8_t(vol), uint8_t(ft), uint8_t(fv)});
        }
        else if (w == "bind")
        {
            int i = 0, n = 0;
            is >> i >> n;
            tables::pool().bind[i] = uint8_t(n);
        }
        else if (w == "dump")
        {   // "table dump N": tabledump N TIC LOOP LEN then the 16 rows as 8 hex digits (N V FXTYPE FXVAL), "table dump bind I": tablebind I N
            std::string x;
            is >> x;
            if (x == "bind")
            {
                int i = 0;
                is >> i;
                printf("tablebind %d %d\n", i, i >= 0 && i < tables::kInstruments ? int(tables::pool().bind[i]) : -1);
            }
            else
            {
                const int n = atoi(x.c_str());
                if (n < 1 || n > tables::kTables) return false;
                const tables::Table& t = tables::pool().table[n - 1];
                printf("tabledump %d %d %d %d", n, int(t.tic), int(t.loop), int(t.len));
                for (const tables::Row& r : t.row)
                    printf(" %02x%02x%02x%02x", unsigned(uint8_t(r.note)), unsigned(r.vol), unsigned(r.fxType), unsigned(r.fxVal));
                printf("\n");
            }
            fflush(stdout);
        }
        else if (w == "fxok")
        {   // "table fxok ID": tablefxok ID 0|1, whether a table row takes that FX type (tables::rowFxAllowed, None excluded)
            int id = 0;
            is >> id;
            printf("tablefxok %d %d\n", id, id > 0 && id < 256 && id != int(Sequencer::strFxConsts::FX_TYPE_OFF) && tables::rowFxAllowed(uint8_t(id)) ? 1 : 0);
            fflush(stdout);
        }
        else if (w == "run")
        {
            printf("tablerun mask %04x", unsigned(tables::g_runMask));
            for (int t = 0; t < 16; t++)
                printf(" %d:%d/%d", t, int(tables::run(t).table), int(tables::run(t).row));
            printf("\n");
            fflush(stdout);
        }
        else
        {
            const int n = atoi(w.c_str());
            int tic = tables::kTicDefault, loop = 1, len = tables::kRows;
            is >> tic >> loop >> len;
            tables::Table& tb = tables::pool().table[n - 1];
            tb.tic = tables::clampTic(uint32_t(tic));
            tb.loop = uint8_t(loop ? 1 : 0);
            tb.len = uint8_t(len < 1 ? 1 : len > tables::kRows ? tables::kRows : len);
        }
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
#endif   // FEATURE_TABLES
#endif
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
    if (c == "editflags")
    {   // editflags: "editflags stereoedit 0|1": the build has FEATURE_STEREO_EDIT (stereo_pairs_sim.sh section 11, stereo_edit_sim.sh)
#if defined(FEATURE_STEREO_EDIT) && !defined(PLATFORM_IMX)
        printf("editflags stereoedit 1\n");
#else
        printf("editflags stereoedit 0\n");
#endif
        fflush(stdout);
        return true;
    }
    if (c == "pair")
    {   // pair MASK: link the stereo track pairs (bit k = tracks 2k+1 and 2k+2, core/source/stereoPairs.h), what project.mtff does at a load. 0 unlinks all
        int m = 0;
        std::string smode;
        is >> m >> smode;   // a 2nd word "mode": also the file manager's stereo sample mode, as a project load with a pair key sets it (stereo_edit_sim.sh)
        engine.setStereoPairs(uint8_t(m));
        if (smode == "mode") newFileManager.setStereoSampleMode(m != 0);
        printf("pair mask %d\n", int(stereopairs::g_mask));
        fflush(stdout);
        return true;
    }
    if (c == "midirows")
    {   // midirows: the rows Config > MIDI lists now (the "Track N in channel" row of a follower of a linked pair is not one of them)
        printf("midirows %d:", int(menuMidi.getCount()));
        for (int i = 0; i < int(menuMidi.getCount()); i++) printf(" [%s]", menuMidi.getNames()[i]);
        printf("\n");
        fflush(stdout);
        return true;
    }
#ifdef FEATURE_FF_CORE
    if (c == "projrows")
    {   // projrows: the rows of Project Settings as "[name=value]", values read the way the list draws them (cMenuGroup::getValues, every pointer first)
        char** values = menuProjectPage.getValues();
        char** names = menuProjectPage.getNames();
        printf("projrows %d pairs %d:", int(menuProjectPage.getCount()), int(stereopairs::g_mask));
        for (int i = 0; i < int(menuProjectPage.getCount()); i++) printf(" [%s=%s]", names[i], values[i]);
        printf("\n");
        fflush(stdout);
        return true;
    }
#endif
    if (c == "delayparams")
    {   // delayparams BYTE: audioEngine::setDelayParams (bit 7 ping pong, bit 6 sync, low 6 bits the rate) and FB: the delay feedback percent (stereo_sends_sim.sh)
        int v = 0, fb = -1;
        is >> v >> fb;
        mtProject.values.delayParams = uint8_t(v); // the project value is applied again by the UI, so it is the one to set
        engine.setDelayParams(uint8_t(v));
        if (fb >= 0) { mtProject.values.delayFeedback = uint8_t(fb); engine.setDelayFeedback(uint8_t(fb)); }
        return true;
    }
    if (c == "reverbpre")
    {   // reverbpre PERMILLE: the reverb predelay (0..1000), audioEngine::setReverbPredelay (stereo_sends_sim.sh: the stereo input needs predelay 0)
        int v = 0;
        is >> v;
        engine.setReverbPredelay(float(v) / 1000.0f);
        return true;
    }
    if (c == "vsmiss")
    {   // vsmiss [reset]: the stream miss counters of the voice stream manager (samples played as 0 for lack of ring data), left and right channel
        std::string a;
        is >> a;
        if (a == "reset") voiceStreamManager.resetMissCount();
        else { printf("vsmiss %u %u\n", voiceStreamManager.getMissCount(0), voiceStreamManager.getMissCount(1)); fflush(stdout); }
        return true;
    }
    if (c == "playmode")
    {   // playmode N MODE: instrument N gets the play mode (0 one shot, 1 loop forward, 2 backward, 3 pingpong, 4 slice, 5 beat slice, 6 wavetable, 7 granular), the loop points
        // inside the first second (loop modes) and 4 slices (slice modes): the play mode sweep of stereo_fx_sweep.sh, after sampletonehz / samplestereo
        int n = 0, m = 0;
        is >> n >> m;
        auto& in = mtProject.instrument[n];
        in.playMode = uint8_t(m);
        if (m >= playModeLoopForward && m <= playModePingpong)
        {
            in.loopPoint1 = 2000;
            in.loopPoint2 = 12000;
        }
        if (m == playModeSlice || m == playModeBeatSlice)
        {
            in.sliceNumber = 4;
            in.slices[0] = 0;
            in.slices[1] = 16000;
            in.slices[2] = 32000;
            in.slices[3] = 48000;
        }
        return true;
    }
    if (c == "amprandom")
    {   // amprandom N SPEED: instrument N gets the amp envelope driven by a random LFO (loop 1, shape random), the draws of both chains of a pair are compared by the proof
        int n = 0, speed = 40;
        is >> n >> speed;
        auto& in = mtProject.instrument[n];
        in.envelope[envAmp].enable = 1;
        in.envelope[envAmp].loop = 1;
        in.lfo[envAmp].shape = lfoShapeRandom;
        in.lfo[envAmp].speed = uint8_t(speed);
        return true;
    }
    if (c == "snapzero")
    {   // snapzero 0|1: the project's snap to zero crossing setting (Project Settings, on by default): the sweep renders with it off for an exact comparison of two different tones
        int on = 1;
        is >> on;
        mtProject.values.isSnapToZeroEnabled = uint8_t(on != 0);
        engine.setSnapToZeroEnable(on != 0);
        return true;
    }
    if (c == "samplestereo")
    {   // samplestereo N HZL HZR AMP: instrument N (0-based) becomes a 4 s STEREO sample (planar like a .pti: the left block, then the right block, address[1] = address[0] + length),
        // single shot, no file. sampletonehz is the mono one
        int n = 0, hzl = 441, hzr = 882, amp = 6000;
        std::string save, strm;
        int ampr = -1;   // "ampr=N" anywhere after AMP: the right block gets its own level (a stereo sample whose R differs from L in level only, the same zero crossings)
        is >> n >> hzl >> hzr >> amp;
        for (std::string w; is >> w;)
        {
            if (w.rfind("ampr=", 0) == 0) ampr = atoi(w.c_str() + 5);
            else if (save.empty()) save = w;
            else strm = w;
        }
        if (ampr < 0) ampr = amp;   // a 6th word "stream": the instrument is flagged to stream (the loader streams a file over 32,768 frames)   // a 5th word "save": the instrument is marked changed, the next project save writes its .pti (both channels) to the Workspace
        static int16_t st[2 * 44100 * 4];
        for (size_t i = 0; i < 44100 * 4; i++)
        {
            st[i] = int16_t(double(amp) * std::sin(6.283185307179586 * double(hzl) * double(i) / 44100.0));
            st[44100 * 4 + i] = int16_t(double(ampr) * std::sin(6.283185307179586 * double(hzr) * double(i) / 44100.0));
        }
        auto& in = mtProject.instrument[n];
        in = defaultInstrumentParams;
        in.isActive = 1;
        in.sample.address = st;
        in.sample.length = 44100 * 4;
        in.playMode = playModeSingleShot;
        in.startPoint = 0;
        in.endPoint = MAX_16BIT;
        in.volume = 100;
        mtProject.sampleMemory[n].address[0] = st;
        mtProject.sampleMemory[n].address[1] = st + 44100 * 4;
        mtProject.sampleMemory[n].isStreamed = false;
        mtProject.sampleMemory[n].isStereo = true;
        if (strm == "stream")
            in.setStreamPreferred(true);
        if (save == "save")
        {
            newFileManager.setInstrumentStructChanged(uint8_t(n));
            newFileManager.setInstrumentSampleChanged(uint8_t(n));
        }
        return true;
    }
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
    if (c == "samplecres")
    {   // samplecres N FRAMES HZ RISE [RHZ]: instrument N (0-based) becomes FRAMES frames of a 12000 peak sine at HZ (44100 base), RISE 1: its level rises linearly from 0 to the peak (a crescendo reads back as a decrescendo), single shot, no file. RHZ > 0: a stereo sample whose right channel is a sine at RHZ at 6000 peak (m8_modes_sim.sh)
        int n = 0, rise = 0;
        unsigned frames = 44100;
        double hz = 441.0, rhz = 0.0;
        is >> n >> frames >> hz >> rise >> rhz;
        static int16_t cres[44100 * 4];
        static int16_t cresR[44100 * 4];
        if (frames > 44100 * 4) frames = 44100 * 4;
        for (unsigned i = 0; i < frames; i++)
        {
            const double env = rise ? double(i) / double(frames) : 1.0;
            cres[i] = int16_t(12000.0 * env * std::sin(6.283185307179586 * hz * double(i) / 44100.0));
            cresR[i] = int16_t(6000.0 * env * std::sin(6.283185307179586 * rhz * double(i) / 44100.0));
        }
        auto& in = mtProject.instrument[n];
        in = defaultInstrumentParams;
        in.envelope[envAmp].enable = 0;   // no amp envelope: the sample plays at its own level to its end, a looped one until the gate closes
        in.isActive = 1;
        in.sample.address = cres;
        in.sample.length = frames;
        in.playMode = playModeSingleShot;
        in.startPoint = 0;
        in.endPoint = MAX_16BIT;
        in.volume = 100;
        mtProject.sampleMemory[n].address[0] = cres;
        mtProject.sampleMemory[n].address[1] = rhz > 0.0 ? cresR : cres;
        mtProject.sampleMemory[n].isStreamed = false;
        mtProject.sampleMemory[n].isStereo = rhz > 0.0;
        return true;
    }
    if (c == "instrmode")
    {   // instrmode N MODE [sp=V] [ep=V] [lp1=V] [lp2=V] [steps=V]: the play mode (the enum value) of instrument N and optionally its points (0..65535 of the sample) and the Repitch Steps (high byte of wavetableCurrentWindow)
        int n = 0, mode = 0;
        is >> n >> mode;
        auto& in = mtProject.instrument[n];
        in.playMode = uint8_t(mode);
        std::string kv;
        while (is >> kv)
        {
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq);
            const int v = atoi(kv.c_str() + eq + 1);
            if (k == "sp") in.startPoint = uint16_t(v);
            else if (k == "ep") in.endPoint = uint16_t(v);
            else if (k == "lp1") in.loopPoint1 = uint16_t(v);
            else if (k == "lp2") in.loopPoint2 = uint16_t(v);
            else if (k == "steps") in.wavetableCurrentWindow = (in.wavetableCurrentWindow & 0x00FFFFFFu) | (uint32_t(v) << 24);
        }
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
        printf("instrinfo %d stereo %d rightIsLeft %d\n", n, int(mtProject.sampleMemory[n].isStereo), int(mtProject.sampleMemory[n].address[1] == mtProject.sampleMemory[n].address[0]));
        fflush(stdout);
        return true;
    }
    if (c == "recstate")
    {   // recstate: the take of the Synth play page REC: "recstate S frames F cap C" (0 idle, 1 recording, 2 full)
#ifdef FEATURE_SYNTH_REC
        printf("recstate %d frames %u cap %u\n", int(synthplayrec::state()), synthplayrec::frames(), synthplayrec::capacity());
#else   // PLATFORM=IMX: the Tracker+ core has no Synth play REC (synthPlayRec.h)
        printf("recstate none\n");
#endif
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
    if (c == "songlist")
    {   // the song: the slot that plays and every slot up to the first empty one (the Capture spill)
        printf("songlist pos %d", int(mtProject.song.playlistPos));
        for (int k = 0; k < SONG_MAX && mtProject.song.playlist[k]; k++) printf(" %d", int(mtProject.song.playlist[k]));
        printf("\n");
        fflush(stdout);
        return true;
    }
    if (c == "patfile")
    {   // patfile N T: the Workspace file of pattern N, its pattern bits (sample:MIDI) and the note steps of track T ("none": no file)
        int n = 1, t = 0;
        is >> n >> t;
        Sequencer::strPattern* p = &songTrackCopy[0];
        const bool ok = newFileManager.readWorkspacePatternTo(uint8_t(n), p);
        printf("patfile %d %d bits %d:%d", n, t, int(mtProject.values.allPatternsBitmask[n - 1]), int(mtProject.values.allPatternsBitmaskMidi[n - 1]));
        if (!ok) printf(" none");
        else
        {
            printf(" len %d", int(p->track[t].length) + 1);
            for (int k = 0; k <= p->track[t].length; k++)
                if (p->track[t].step[k].note != Sequencer::STEP_NOTE_EMPTY) printf(" %d:%d", k, int(p->track[t].step[k].note));
        }
        printf("\n");
        fflush(stdout);
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
        printf("dlstat frame %u max %u overflow %u songplayer %d partstable %d table %u fifo %u list %u\n", st.frameWords, st.frameWordsMax, st.dlOverflow, int(songEditor.songPlayerControl), int(songEditor.partsTableControl), songEditor.partsTableControl >= 0 ? st.slotBytes[songEditor.partsTableControl] / 4 : 0u, st.cmdFifoOverflow, st.cmdListWordsMax);
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
#ifdef FEATURE_PARTS
    if (c == "songsel")
    {   // the Song screen's selection: position, first track, tracks, and the hidden mask of the grid (linked stereo pairs)
        const auto& sel = songEditor.songPlayerData.selection;
        printf("songsel pos %d track %d len %d active %d hidden 0x%x\n", int(sel.startSongPosition), int(sel.startTrack), int(sel.trackSelectionLength), int(sel.isActive), unsigned(songEditor.songPlayerData.hidden));
        fflush(stdout);
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
