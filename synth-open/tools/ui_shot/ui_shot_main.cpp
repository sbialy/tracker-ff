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
//   midion NOTE [VEL] / midioff NOTE   a note from the USB MIDI input (notes in: USB and jack, omni), as mtMidi.cpp hands it on
//   setstep T S NOTE INSTR FX0T FX0V FX1T FX1V   write a step of the pattern on screen (NOTE -1 = empty, FX type ids of mtSequencer.h)
//   step T S           print a step: "step T S note N instr I fx0 T:V fx1 T:V"
//   cursor             print the pattern editor cursor: "cursor track T step S param P edit E"
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
//   usbaudio                   print the USB audio model: the session's function and the EEPROM boot flag
//   eepromret N / usbflag N    set the EEPROM Power return mirror (byte 8) or the USB audio boot flag (byte 9)
//   fmset N                    set the file manager status to N (fileManager.h fileManagerStatus), an injection
//   touch PATH / rm PATH       create or remove a file (absolute path), e.g. the UISHOT_FAIL_ARM file of host_fault.cpp
//   put REL TEXT               write TEXT (backslash n and backslash r escapes) to REL in the SD folder, replacing the file (the order file of the model list)
//   cat REL                    print a file of the SD folder (nothing when it is not there)
//   chmod MODE PATH            chmod a path of the SD folder (octal MODE), e.g. 555 Workspace makes every Workspace save fail
// Buttons: F1..F8 Params Performance File Config Master SampleLoad SamplePlay SampleEdit SampleRec Song Note Instr Vol Fx Pattern
//          Play Copy Insert Up Delete Rec Shift Left Down Right
#include "host_platform.h"

#include "Tracker.h"
#include "core/interface.h"
#include "display.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "mtSleep.h"
#include "mtMidi.h"
#include "mtSequencer.h"
#include "patternEditor/patternEditor.h"
#if __has_include("RestartMode.h")
#include "RestartMode.h"
#endif
#if defined(FEATURE_EXT_HOOKS) && __has_include("features.h")
#include "features.h"
#define UISHOT_HAVE_FEATURES 1
#endif
#include <fstream>

// The sample playback screen reads editorInstrument->playMode in initDisplayControls() before start() has set the pointer. On the
// Cortex-M a null pointer reads the flash at address 0, on the Mac it is a fault. The host seeds the pointer first, which needs the
// private member (access only, same layout; the standard headers are in already).
#define private public
#include "samplePlayback/samplePlayback.h"
#undef private

#include "mtChord.h"

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

extern "C" void hostPokeRun(const char* line); // host_poke.cpp
// "audio 1": the audio callback (the firmware's own Tracker::updateAudioBuffers) runs between the main loop passes, the blocks that fit
// into each 5 ms step (128 frames at 44,117.6 Hz = 2,901.3 us), so the sequencer ticks and plays. g_audioUs: rendered audio time,
// the time stamp of the "midilog" lines (host_platform.cpp).
double g_audioUs = 0;
bool g_midiLog = false;
namespace
{
bool g_audio = false;
double g_audioAcc = 0;
void run(uint32_t ms)
{
    Tracker* t = Tracker::getInstance();
    static int16_t l[256], r[256];
    for (uint32_t done = 0; done < ms; done += 5)
    {
        hostp::advance(5);
        t->update();
        if (!g_audio) continue;
        for (g_audioAcc += 5000.0; g_audioAcc >= 2901.33; g_audioAcc -= 2901.33)
        {
            t->updateAudioBuffers(l, r, 128);
            g_audioUs += 1e6 * 128 / 44117.6470588;
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
    if (c == "title")
    {   // the right side of the pattern title bar (the FX name of the cell under the cursor)
        printf("title %s\n", patternEditor.rightTitleBarLabel);
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
    if (c == "audio") { int n = 0; is >> n; g_audio = n != 0; return true; }
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
            printf(" fx%d [%c%s]", k, name > 0 && name < FX_COUNT ? interfaceGlobals.fxNames[name][0] : '-', name > 0 && name < FX_COUNT ? v : "---");
        }
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
    if (c == "resets") { printf("resets %d while off %d start state %d\n", hostp::g_mcuResets, hostp::g_resetsWhileOff, hostp::g_startState); return true; }
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
    fprintf(stderr, "ui_shot: unknown command '%s'\n", c.c_str());
    return false;
}
}   // namespace

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
    run(6000);   // past the plugin host's boot key window (Shift in the first seconds means safe mode)
    std::istringstream is(argv[2]);
    std::string line;
    int rc = 0;
    while (std::getline(is, line, ';'))
        if (!command(line)) rc = 1;
    if (hostp::eve().errors()) { fprintf(stderr, "ui_shot: %d model errors\n%s", hostp::eve().errors(), hostp::eve().log().c_str()); rc = 1; }
    return rc;
}
