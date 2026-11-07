// ui_shot: runs the real Tracker UI (core/ and synth-open/ of this tree, the PLUG_HOST configuration) on the Mac against the
// FT812 model and writes the framebuffer as PNG. NOT firmware.
//
//   ui_shot SD_DIR "script"
//   ui_shot --serve SD_DIR [--no-develop] [--tx-rate BYTES_PER_5MS]
//                      the USB test console of the firmware (core/source/UsbTest*.cpp) on stdin and stdout, the same line protocol as the
//                      device's serial port (docs/usb-test-console.md), driven by tools/usbtest/usbtest.py. Time is virtual: it runs
//                      while commands are being handled and by "_advance MS". Host only lines (start with _): _advance MS, _link 0|1,
//                      _rate BYTES. A reboot command ends the process with exit code 77.
//
// The script is a ';' separated list of commands, run after the UI has started (pattern screen up):
//   wait MS            run the main loop for MS virtual milliseconds
//   tap NAME [N]       press and release a front panel button N times (names below)
//   hold NAME / unhold NAME   keep a button down (what Shift needs), press event on hold
//   enc N              turn the encoder N detents (negative: left)
//   pad N              press pad N (0 based), release
//   shot FILE.png      write the screen
//   dump               print the model's statistics
// Buttons: F1..F8 Params Performance File Config Master SampleLoad SamplePlay SampleEdit SampleRec Song Note Instr Vol Fx Pattern
//          Play Copy Insert Up Delete Rec Shift Left Down Right
#include "host_platform.h"

#include "Tracker.h"
#include "UsbTest.h"
#include "core/interface.h"
#include "display.h"
#include "mtStructs.h"

// The sample playback screen reads editorInstrument->playMode in initDisplayControls() before start() has set the pointer. On the
// Cortex-M a null pointer reads the flash at address 0, on the Mac it is a fault. The host seeds the pointer first, which needs the
// private member (access only, same layout; the standard headers are in already).
#define private public
#include "samplePlayback/samplePlayback.h"
#undef private

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

namespace
{
void run(uint32_t ms)
{
    Tracker* t = Tracker::getInstance();
    for (uint32_t done = 0; done < ms; done += 5)
    {
        hostp::advance(5);
        t->update();
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
    if (c == "shot")
    {
        is >> a;
        run(300);   // let the lists and bars settle (they animate)
        if (!hostp::eve().savePng(a.c_str())) { fprintf(stderr, "ui_shot: cannot write %s\n", a.c_str()); return false; }
        return true;
    }
    if (c == "instr")
    {   // what the Instr button and the encoder do in the pattern screen: the instrument the Params screen shows (64..66 are the synth parts)
        int n = 0;
        is >> n;
        mtProject.values.lastUsedInstrument = uint8_t(n);
        return true;
    }
    if (c == "status")
    {
        printf("pattern %d song %d master %d params %d play %d perf %d\n", mtInterface.isPatternScreenActive(), mtInterface.isSongScreenActive(), mtInterface.isMasterScreenActive(),
               mtInterface.isInstrumentParamsScreenActive(), mtInterface.isSamplePlayerScreenActive(), mtInterface.isPerformanceModeScreenActive());
        return true;
    }
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

// ---- --serve: the USB test console
namespace
{
bool g_sawReady;

void flushTx()
{
    std::string& tx = hostp::usbtestTx();
    if (!tx.empty())
    {
        if (tx.find("@ready\n") != std::string::npos) g_sawReady = true;
        fwrite(tx.data(), 1, tx.size(), stdout);
        fflush(stdout);
        tx.clear();
    }
}

void serveStep()
{
    hostp::usbtestStep();
    flushTx();
}

// steps until the console has nothing left to do (a command that waits for the host, the put payload, counts as done)
void stepUntilQuiet()
{
    int quiet = 0;
    for (int i = 0; i < 24000 && quiet < 3; i++)
    {
        serveStep();
        quiet = (hostp::usbtestRxPending() == 0 && !UsbTest_Busy()) ? quiet + 1 : 0;
        if (hostp::g_resetRequested) return;
    }
}

int serve(const std::string& sd, bool develop, long txRate)
{
    mkdir(sd.c_str(), 0755);
    g_sdRootForStat = sd;
    hostp::setSdRoot(sd);
    Platform* p = hostp::platform();
    Tracker* t = Tracker::getInstance();
    if (develop)
    {
        FILE* f = fopen((sd + "/.develop").c_str(), "wb");
        if (f) fclose(f);
    }
    else
    {
        remove((sd + "/.develop").c_str());
    }
    t->getStartDisplayFunc()(p);
    t->init(p);
    for (int i = 0; i < 20000 && !mtInterface.isPatternScreenActive(); i++) hostp::usbtestStep();
    if (!mtInterface.isPatternScreenActive()) { fprintf(stderr, "ui_shot: the pattern screen never came up\n"); return 1; }
    samplePlayback.editorInstrument = &mtProject.instrument[0];
    for (int i = 0; i < 1200; i++) hostp::usbtestStep();   // past the plugin host's boot key window
    hostp::usbtestTx().clear();
    if (txRate > 0) hostp::usbtestSetTxRate(size_t(txRate));
    printf("ui_shot: serve ready\n");
    fflush(stdout);

    std::string pending;
    long rawRemaining = 0;   // bytes of a put payload still to come
    for (;;)
    {
        char buf[4096];
        const ssize_t n = read(0, buf, sizeof(buf));
        if (n <= 0) break;
        pending.append(buf, size_t(n));
        for (;;)
        {
            if (rawRemaining > 0)
            {
                const size_t take = std::min<size_t>(pending.size(), size_t(rawRemaining));
                if (take == 0) break;
                hostp::usbtestFeed(pending.data(), take);
                pending.erase(0, take);
                rawRemaining -= long(take);
                stepUntilQuiet();
                continue;
            }
            const size_t nl = pending.find('\n');
            if (nl == std::string::npos) break;
            const std::string line = pending.substr(0, nl + 1);
            pending.erase(0, nl + 1);
            if (line[0] == '_')
            {
                long v = 0;
                if (sscanf(line.c_str(), "_advance %ld", &v) == 1)
                {
                    for (long ms = 0; ms < v; ms += 5) serveStep();
                    printf("@advanced %ld\n", v);
                }
                else if (sscanf(line.c_str(), "_link %ld", &v) == 1)
                {
                    hostp::usbtestSetLink(v != 0);
                    for (int i = 0; i < 4; i++) serveStep();
                    printf("@linked %ld\n", v);
                }
                else if (sscanf(line.c_str(), "_rate %ld", &v) == 1)
                {
                    hostp::usbtestSetTxRate(size_t(v));
                    printf("@rated %ld\n", v);
                }
                fflush(stdout);
                continue;
            }
            hostp::usbtestFeed(line.data(), line.size());
            stepUntilQuiet();
            // a put the console accepted (@ready): its payload follows as raw bytes
            if (g_sawReady && line.compare(0, 4, "put ") == 0)
            {
                const size_t b = line.find_last_of(' ', line.size() - 3);   // before the crc
                const size_t a = line.find_last_of(' ', b - 1);              // before the size
                if (a != std::string::npos && b != std::string::npos) rawRemaining = atol(line.substr(a + 1, b - a - 1).c_str());
            }
            g_sawReady = false;
            if (hostp::g_resetRequested)
            {
                fflush(stdout);
                return 77;
            }
        }
    }
    return 0;
}
}   // namespace

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
    if (argc >= 3 && std::string(argv[1]) == "--serve")
    {
        bool develop = true;
        long rate = 0;
        for (int i = 3; i < argc; i++)
        {
            if (std::string(argv[i]) == "--no-develop") develop = false;
            else if (std::string(argv[i]) == "--tx-rate" && i + 1 < argc) rate = atol(argv[++i]);
        }
        return serve(argv[2], develop, rate);
    }
    if (argc < 3) { fprintf(stderr, "usage: ui_shot SD_DIR \"script\"   or   ui_shot --serve SD_DIR\n"); return 2; }
    const std::string sd = argv[1];
    mkdir(sd.c_str(), 0755);
    g_sdRootForStat = sd;
    hostp::setSdRoot(sd);
    Platform* p = hostp::platform();
    Tracker* t = Tracker::getInstance();
    // UISHOT_SHIFT_BOOT=1: Shift held while the unit powers up (the plugin host's safe mode), released once the pattern screen is up
    const bool shiftBoot = getenv("UISHOT_SHIFT_BOOT") != nullptr;
    if (shiftBoot) hostp::setHeld(29, true);
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
    run(6000);   // past the plugin host's boot key window (Shift in the first seconds means safe mode)
    std::istringstream is(argv[2]);
    std::string line;
    int rc = 0;
    while (std::getline(is, line, ';'))
        if (!command(line)) rc = 1;
    if (hostp::eve().errors()) { fprintf(stderr, "ui_shot: %d model errors\n%s", hostp::eve().errors(), hostp::eve().log().c_str()); rc = 1; }
    return rc;
}
