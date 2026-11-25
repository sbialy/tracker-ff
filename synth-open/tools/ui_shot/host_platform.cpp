// Host Platform for ui_shot. NOT firmware. See host_platform.h.
#include "eve_model.h"
#include "host_platform.h"
#include "host_pti.h"
#include "util_string.h"
#ifdef UISHOT_PLUS
#include "../../../platforms/imx/source/UartMidiImpl.h"
#include "../../../platforms/imx/source/UsbMidiImpl.h"
#endif

#include "ConfigManager.h"
#if __has_include("PowerOffGuard.h")
#include "PowerOffGuard.h"
#endif
#if __has_include("RestartMode.h")
#define UISHOT_HAVE_RESTART_MODE 1
#endif

#include <algorithm>
#include <deque>
#include <map>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern double g_audioUs;   // ui_shot_main.cpp: "audio 1" time, "midilog 1" prints every note sent to USB MIDI out with it
extern bool g_midiLog;
extern bool g_usbBusLost;
// the fake USB MIDI receive queue of the "usbq" script command (midi_isr_cap_sim.sh): event numbers, each read() takes one, logs it and
// advances the fake micros clock by g_usbReadCostUs (the cost of an expensive event, a note on that starts a voice)
std::deque<int> g_usbQueue;
std::vector<int> g_usbReadLog;
uint32_t g_usbReadCostUs = 0;
uint32_t g_pulseCostUs = 0;     // "clockcost US": each external clock pulse (96 nano ticks) played costs US on the fake micros clock
uint32_t g_pulseBaseTicks = 0;
extern uint32_t hostSeqTimerTicks();   // ui_shot_main.cpp: sequencer.timerTicks
uint32_t g_usbExtraUs = 0;   // ui_shot_main.cpp, the "usbunplug" script command: the USB bus went idle (usb_dev.c flag), taken once by midiUpdate like the device does
extern "C" volatile uint32_t systick_millis_count;   // host_stubs.cpp: the firmware's systick, kept in step with the virtual clock

namespace hostp
{
uint32_t g_nowMs = 1000;
bool g_resetRequested;
Platform::PowerButtonCallback g_powerButtonCallback;
int g_powerOffCalls;
uint32_t g_powerOffAtMs;
int g_guardArmed, g_guardKicks;
static std::string s_root = ".";
static bool g_held[64];

// ---- MK66 power model (PowerControl.cpp checkPowerState, PlatformImpl.cpp, PowerOffGuard.h) ----
// The EEPROM start state byte, the off marker in RAM that survives a reset, the power-off guard checked every millisecond (SysTick).
// The decisions are the firmware's own (PowerOffGuard.h). A reset does not restart the host process: it is recorded and the start
// decision the unit would take is printed, the script goes on.
// UISHOT_K66_WDOG=1 models 5878f0e instead: setPowerOffGuard(true) arms the K66 WDOG, which on the unit ignores the refresh and the
// disable (ref/shot-hang-1.2.0.md R8.2), so it bites kTimeoutMs after the arm whatever the firmware does next.
int g_startState = 1;       // EEPROM byte 0 (UISHOT_START_STATE, default 1 = the unit was switched on)
int g_returnMirror;         // EEPROM byte 8 (UISHOT_RETURN_MIRROR), PlatformImpl::readPowerReturnMirror
#ifdef FEATURE_USB_AUDIO_IN
// FEATURE_USB_AUDIO_IN (USB_IN=1): the recorder's USB source asks the platform for the stream, the sim only counts the asks
int g_usbInWant;
int g_usbInWantCalls;
extern "C" void ffUsbAudioInWant(int on)
{
    g_usbInWant = on;
    g_usbInWantCalls++;
    fprintf(stderr, "usbin want %d (call %d)\n", on, g_usbInWantCalls);
}
#endif
int g_filePoolUsed, g_filePoolPeak, g_filePoolRefused;   // see host_platform.h
std::set<const void*> g_filePoolHeld;   // the HFile of every slot in use (fspool names the paths)
int g_asyncBusy;           // see host_platform.h
int g_usbAudioFlag;         // EEPROM byte 9 (UISHOT_USB_FLAG), ffUsbAudioBootFlag
int g_usbAudioRunning;      // usb_init at the last start (ff_usb_audio_select)
int g_usbAudioMode;         // EEPROM byte 7 (UISHOT_USB_MODE): 1 = 8+3 mono (0xA5), ffUsbAudioBootFlag
int g_usbAudioRunningMode;  // the mode usb_init picked at the last start (8+3 needs the flag on too)
int g_usbAudioStreaming;    // the computer records now (`usbstream 1`), Platform::usbAudioStreaming
#ifdef FEATURE_USB_MSC
int g_driveFlag;             // EEPROM byte 7 (UISHOT_DRIVE_FLAG), ffUsbMscBootFlag
int g_driveRunning;          // usb_init at the last start (ff_usb_msc_select)
bool g_driveOwned;
unsigned g_driveBackend, g_driveRefused, g_driveStale;
std::map<std::string, unsigned> g_driveRefusedBy;
#endif
bool g_offMarker;           // RAM marker of a guard reset
bool g_restartMarker;       // RAM marker of a guard reset in the restart mode (RestartMode.h)
bool g_restartOnPowerOff;   // Platform::setRestartOnPowerOff
bool g_unitOff;             // setPowerMode(false) ran (the unit is off), until a reset that starts it
int g_mcuResets, g_resetsWhileOff;
#if __has_include("PowerOffGuard.h")
#define UISHOT_HAVE_GUARD 1
static power_off_guard::Guard g_guard;
static bool g_wdogModel;          // UISHOT_K66_WDOG
static uint32_t g_wdogBiteAt;     // 0 = not armed
static uint32_t g_tickMs;         // last millisecond the SysTick model ran

// any MCU reset: what checkPowerState decides at the next boot
static void mcuReset(const char* why, bool watchdog)
{
    g_mcuResets++;
    if (g_unitOff) g_resetsWhileOff++;
    power_off_guard::StartInputs in;
    in.startStateSave = true;   // release build
    in.consoleBuild = false;
    in.offMarker = g_offMarker;
    in.watchdogReset = watchdog;
    in.configValid = true;
    in.startState = uint8_t(g_startState);
#ifdef UISHOT_HAVE_RESTART_MODE
    in.restartMarker = g_restartMarker;
#endif
    g_offMarker = false;        // checkPowerState takes it
    g_restartMarker = false;
    const bool run = power_off_guard::decideStart(in) == power_off_guard::Start::kRun;
    // the off branch writes 0 (setPowerMode(false)), the start branch the Power return mirror (PowerControl.cpp checkPowerState)
    g_startState = run && g_returnMirror ? 1 : 0;
    g_usbAudioRunning = g_usbAudioFlag;   // usb_init runs before checkPowerState at every MCU start
    g_usbAudioRunningMode = g_usbAudioFlag && g_usbAudioMode ? 1 : 0;
#ifdef FEATURE_USB_MSC
    g_driveRunning = g_driveFlag;
#endif
    g_guard.disarm();
    g_wdogBiteAt = 0;
    printf("mcu reset (%s)%s: start %s\n", why, in.watchdogReset ? " wdog" : "", run ? "RUN" : "off");
    g_unitOff = !run;
}

// SysTick: every millisecond, also while the main loop is blocked
static void sysTick(uint32_t now)
{
    if (g_guard.expired(now))
    {
        (g_restartOnPowerOff ? g_restartMarker : g_offMarker) = true;   // resetAfterPowerOff
        mcuReset(g_restartOnPowerOff ? "guard restart" : "guard", false);
    }
    if (g_wdogBiteAt != 0 && int32_t(now - g_wdogBiteAt) >= 0)
    {
        mcuReset("k66 wdog", true);
    }
}

void advance(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        g_nowMs++;
        sysTick(g_nowMs);
    }
    systick_millis_count = g_nowMs;   // the firmware's systick follows the virtual clock (rate limits such as the 50 ms of the synth service)
    audioTick();                      // SIM_AUDIO=1 only (host_simcard.cpp)
}

void bootModel()
{
    g_wdogModel = getenv("UISHOT_K66_WDOG") != nullptr;
    if (const char* e = getenv("UISHOT_START_STATE")) g_startState = atoi(e);
    if (const char* e = getenv("UISHOT_RETURN_MIRROR")) g_returnMirror = atoi(e);
    if (const char* e = getenv("UISHOT_USB_FLAG")) g_usbAudioFlag = atoi(e);
    if (const char* e = getenv("UISHOT_USB_MODE")) g_usbAudioMode = atoi(e);
    g_usbAudioRunning = g_usbAudioFlag;
    g_usbAudioRunningMode = g_usbAudioFlag && g_usbAudioMode ? 1 : 0;
#ifdef FEATURE_USB_MSC
    if (const char* e = getenv("UISHOT_DRIVE_FLAG")) g_driveFlag = atoi(e);
    g_driveRunning = g_driveFlag;
#endif
    power_off_guard::StartInputs in{true, false, false, false, true, uint8_t(g_startState)};
    const bool run = power_off_guard::decideStart(in) == power_off_guard::Start::kRun;
    printf("boot: start state %d: start %s\n", g_startState, run ? "RUN" : "off");
    g_startState = run && g_returnMirror ? 1 : 0;
}

void resetCommand() { mcuReset("reset", false); }
void stall(uint32_t ms) { advance(ms); }   // the main loop blocked in a call: time and SysTick go on, no main loop pass
#else
void advance(uint32_t ms)
{
    g_nowMs += ms;
    systick_millis_count = g_nowMs;
    audioTick();   // SIM_AUDIO=1 only (host_simcard.cpp)
}
void bootModel() {}
void resetCommand() { printf("mcu reset: no model in this tree\n"); }
void stall(uint32_t ms) { advance(ms); }
#endif
void syncSystick() { systick_millis_count = g_nowMs; }
void setHeld(int idx, bool on) { if (idx >= 0 && idx < 64) g_held[idx] = on; }
void setSdRoot(const std::string& d) { s_root = d; }
const std::string& sdRoot() { return s_root; }
// replaced by the console host_usbtest.cpp in a CONSOLE=1 build (host_platform.h)
__attribute__((weak)) bool keyPressed(bool, int) { return false; }
__attribute__((weak)) const char* simBuildId() { return "uishot"; }
__attribute__((weak)) void simBootloaderHandoff(const char*) {}
// the default platform has no extensions: a console build (poly-devtools host_hw.cpp) replaces this and swaps devices in the slots
__attribute__((weak)) void extendPlatform(HwSlots&) {}

extern "C" void hostPokePoll(); // host_poke.cpp: UISHOT_POKE actions, run from the main loop

int g_radioStereo = -1; // the last Radio::setStereo of the firmware (-1 never called), read by host_poke recstat
namespace
{
// ------------------------------------------------------------------------------------------------ simple devices
class HTimer : public Timer
{
  public:
    uint32_t millis() override { hostPokePoll(); return g_nowMs; }
    uint32_t micros() override
    {
        const uint32_t pulseUs = ::g_pulseCostUs ? (hostSeqTimerTicks() - ::g_pulseBaseTicks) * ::g_pulseCostUs / 96u : 0u;
        return g_nowMs * 1000u + ::g_usbExtraUs + pulseUs;
    }
    void delay(int ms) override { advance(uint32_t(ms)); }
    uint32_t clockCycles() override { return g_nowMs * 180000u; }
    uint32_t clockCyclesPerUs() override { return 180; }
};

class HEncoder : public Encoder
{
  public:
    void setAcceleration(int) override {}
    void setResolution(int) override {}
};

class HBattery : public Battery
{
  public:
    void enableMonitoring() override {}
    int getLevel() override { return 100; }
    float getVoltage() override { return 4.1f; }
    bool getCharging() override { return false; }
    int getTimeTo() override { return 0; }
    bool getState() override { return true; }
    bool isEmpty() override { return false; }
};

// The pad LEDs (cMtPadsBacklight, the keyboard manager): the default does nothing. The console build (poly-devtools host/ui_shot/host_hw.cpp) replaces
// it through hostp::extendPlatform and records the state of each pad for `_leds` and `_frame`.
class HLeds : public Leds
{
  public:
    void setLed(int n, bool on, int level) override
    {   // the last state of each pad LED (`padleds` of ui_shot_main.cpp)
        if (n >= 0 && n < 64) g_padLed[n] = on ? 0x100 | (level & 0xFF) : 0;   // on with level 0 is on (the host config is all zeros)
    }
};

class HSwitch : public SwitchGroup
{
  public:
    explicit HSwitch(bool pad = false) : pad_(pad) {}
    void setHoldTime(int) override {}
    bool isButtonPressed(int i) override
    {
        return (!pad_ && i >= 0 && i < 64 && g_held[i]) || (i >= 0 && i < 64 && (remote_ >> i & 1)) || keyPressed(pad_, i);
    }
    bool pad_;
    // the MIDI remote's second source, true when the held state changed (as Keys::setRemote of the unit does)
    bool setRemoteHeld(int i, bool on) override
    {
        if (i < 0 || i >= 64 || bool(remote_ >> i & 1) == on) return false;
        remote_ = on ? remote_ | uint64_t(1) << i : remote_ & ~(uint64_t(1) << i);
        return true;
    }
    void setActionCallback(ActionType, void (*)(uint8_t)) override {}

  private:
    uint64_t remote_ = 0;
};

class HRadio : public Radio
{
  public:
    void enableLineInput(bool) override {}
    void enable(bool) override {}
    bool setFrequency(float f) override { freq_ = f; return true; }
    void setSeekCallback(void (*)(void)) override {}
    void seek(SeekDirection) override {}
    void updateSeek() override {}
    bool updateRDS() override { return false; }
    float getFrequency() override { return freq_; }
    float getBaseFrequency() override { return 87.5f; }
    float getSpacing() override { return 0.1f; }
    bool isinitialized() override { return false; }
    void clearRDS() override {}
    void setRegion(Region) override {}
    bool isRegionChanged() override { return false; }
    char* getStationName() override { static char n[1] = {0}; return n; }
#ifdef FEATURE_RADIO_STEREO
    void setStereo(bool s) override { g_radioStereo = s ? 1 : 0; }
#endif

  private:
    float freq_ = 100.0f;
};

class HCodec : public Sgtl5000
{
  public:
    void init() override {}
    void setInputType(InputType) override {}
    void setHeadphoneSource(HeadphoneSource) override {}
    void setLineInLevel(int) override {}
    void setMicGain(int) override {}
    void setHeadphoneMuted(bool) override {}
    void setMasterVolume(float) override {}
    void setAutoVolumeEnabled(bool) override {}
    void configureAutoVolume(AutoVolumeParams*) override {}
    void setEnhanceBassEnabled(bool) override {}
    void configureEnhanceBass(EnhanceBassParams*) override {}
    void setEqualizerType(EqualizerType) override {}
    void configureFiveBandEqualizer(float*) override {}
    void enableSurroundSound(bool) override {}
    void configureSurroundSound(SurroundSoundParams*) override {}
    void enableHardwareProcessor(bool) override {}
};

class HUsbAudio : public UsbAudio
{
  public:
    void beginFillData() override {}
    void fillChannelData(void*, int, int) override {}
    void finishFillData() override {}
};


class HUsbMidi : public UsbMidi
{
  public:
    bool isReadAvailable() override { return !::g_usbQueue.empty(); }
    bool takeBusLost() override
    {
        const bool lost = ::g_usbBusLost;
        ::g_usbBusLost = false;
        return lost;
    }
    void read() override
    {
        if (::g_usbQueue.empty()) return;
        ::g_usbReadLog.push_back(::g_usbQueue.front());
        ::g_usbQueue.pop_front();
        ::g_usbExtraUs += ::g_usbReadCostUs;
    }
    void sendRealTime(MessageType) override {}
    void sendControlChange(uint8_t index, uint8_t v, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi cc %u %u ch %u t %.1f\n", index, v, ch, ::g_audioUs / 1000.0);
    }
    void sendProgramChange(uint8_t v, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi pc %u ch %u t %.1f\n", v, ch, ::g_audioUs / 1000.0);
    }
    void sendAfterTouch(uint8_t v, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi at %u ch %u t %.1f\n", v, ch, ::g_audioUs / 1000.0);
    }
    void sendPolyPressure(uint8_t n, uint8_t v, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi pp %u %u ch %u t %.1f\n", n, v, ch, ::g_audioUs / 1000.0);
    }
    void sendPitchBend(int v, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi pb %d ch %u t %.1f\n", v, ch, ::g_audioUs / 1000.0);
    }
    void sendNoteOn(uint8_t n, uint8_t v, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi on %u %u ch %u t %.1f\n", n, v, ch, ::g_audioUs / 1000.0);
    }
    void sendNoteOff(uint8_t n, uint8_t, uint8_t ch) override
    {
        if (::g_midiLog) printf("midi off %u ch %u t %.1f\n", n, ch, ::g_audioUs / 1000.0);
    }
    void setReceiveActions(MidiReceiveActions*) override {}
};

#ifdef UISHOT_PLUS
// the real IMX USB MIDI driver, plus the host seam of the bus lost flag ("usbunplug", the usb_dev.c flag of the image)
class PlusUsbMidi : public UsbMidiImpl
{
  public:
    bool takeBusLost() override
    {
        const bool lost = ::g_usbBusLost;
        ::g_usbBusLost = false;
        return lost;
    }
};
#endif

class HUartMidi : public UartMidi
{
  public:
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType) override {}
    void sendControlChange(uint8_t, uint8_t, uint8_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendAfterTouch(uint8_t, uint8_t) override {}
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override {}
    void sendPitchBend(int, uint8_t) override {}
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void init() override {}
    void setReceiveActions(MidiReceiveActions*) override {}
    void setReceiveNoteOnAction(void (*)(uint8_t, uint8_t, uint8_t)) override {}
};

class HUsb : public Usb
{
  public:
    void init() override {}
    void reset() override {}
    void update() override {}
#ifdef FEATURE_USB_MSC
    // msc_drive.cpp: kStorage lends the card when the drive was enumerated at start
    void setClassState(ClassType t, bool on) override;
    bool getClassState(ClassType t) override;
#else
    void setClassState(ClassType, bool) override {}
    bool getClassState(ClassType) override { return false; }
#endif
    bool isAttached() override { return false; }
};

class HCrc : public CrcCalculator
{
  public:
    uint32_t calculate32bitCrc(const uint8_t* d, const uint16_t n) override
    {
        uint32_t c = 0xFFFFFFFFu;   // CRC-32 (IEEE), the value only has to be stable here
        for (uint16_t i = 0; i < n; i++)
        {
            c ^= d[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }
};

class HConfig : public ConfigManager
{
  public:
    bool readData(int address, void* data, int size) override
    {
        if (address < 0 || address + size > int(sizeof(mem_))) return false;
        memcpy(data, mem_ + address, size_t(size));
        return true;
    }
    void writeData(int address, void* data, int size) override
    {
        if (address < 0 || address + size > int(sizeof(mem_))) return;
        memcpy(mem_ + address, data, size_t(size));
    }

  private:
    uint8_t mem_[4096] = {};   // an erased EEPROM reads 0xFF on the device, the host starts with zeros: the UI then writes its defaults
};

// ------------------------------------------------------------------------------------------------ file system
struct HFile
{
    FILE* f = nullptr;
    std::string path;
    bool slot = false;   // holds a slot of the device's SdFile pool (g_filePoolUsed)
};

class HFileSystem : public FileSystem
{
  public:
    std::string abs(const char* p) const
    {
#ifdef FEATURE_USB_MSC
        if (hostp::g_driveOwned) hostp::g_driveBackend++;   // a path reached the card while the computer owns it
#endif
        HostFaultSuspend keep;   // simulator memory: the device's FatFs paths are fixed buffers
        std::string s = p ? p : "";
        for (char& c : s) if (c == '\\') c = '/';   // FatFs takes '\\' as a separator (IsSeparator): the bank copy and the Synth file paths use it
        while (!s.empty() && s[0] == '/') s.erase(0, 1);
        return s_root + "/" + s;
    }
    bool isSdCardInserted() override { return true; }
    void setMountEventCallback(CardStateChangeCallback) override {}
    bool remount() override { return true; }
    bool exists(const char* path) override { struct stat st; return stat(abs(path).c_str(), &st) == 0; }
    bool rename(const char* a, const char* b) override { return ::rename(abs(a).c_str(), abs(b).c_str()) == 0; }
    bool mkdir(uint8_t, const char* path, bool pFlag) override
    {
        HostFaultSuspend keep;
        std::string p = abs(path);
        if (pFlag)
            for (size_t i = s_root.size() + 1; i < p.size(); i++)
                if (p[i] == '/') ::mkdir(p.substr(0, i).c_str(), 0755);
        return ::mkdir(p.c_str(), 0755) == 0 || exists(path);
    }
    bool remove(const char* path) override { return ::remove(abs(path).c_str()) == 0; }
    bool removeDirWithFiles(const char* path) override { HostFaultSuspend keep; return system(("rm -rf '" + abs(path) + "'").c_str()) == 0; }
    bool removeOnlyDirFiles(const char* path) override { HostFaultSuspend keep; return system(("find '" + abs(path) + "' -type f -delete").c_str()) == 0; }
    size_t getFileSize(const char* path) override { struct stat st; return stat(abs(path).c_str(), &st) == 0 ? size_t(st.st_size) : 0; }
    size_t getFileSize(FileHandle h) override
    {
        backendHandle();
        auto* f = static_cast<HFile*>(h);
        if (!f || !f->f) return 0;
        const long cur = ftell(f->f);
        fseek(f->f, 0, SEEK_END);
        const long n = ftell(f->f);
        fseek(f->f, cur, SEEK_SET);
        return size_t(n);
    }
    size_t getFileAvailable(FileHandle h) override
    {
        backendHandle();
        auto* f = static_cast<HFile*>(h);
        if (!f || !f->f) return 0;
        return getFileSize(h) - size_t(ftell(f->f));
    }
    std::tuple<bool, FileHandle> openFile(const char* filepath, FileOpenMode mode, FileHandle* fileHandle) override
    {
        HostFaultSuspend keep;   // the device takes its SdFile from a fixed pool (FileSystemImpl::allocateSdFile) and checks it
        const char* m = mode == kRead ? "rb" : (mode == kWriteOpen ? "r+b" : (mode == kWriteAppend ? "ab" : "w+b"));   // kWriteCreateNew reads back too, as the card does: the IT export reads its own sample offset table
        const std::string p = abs(filepath);
        // UI_SHOT_FATFS_REOPEN=1: kWriteCreateNew of a handle that holds the same path keeps the bytes, what the device FatFs does when the
        // cluster chain freed by FA_CREATE_ALWAYS is allocated again in the same order (mtAudioExport::saveMasterBufferToSD reopens the master
        // WAV at every flush, see ref/export-bpm.md section 9). Default off: "w+b" truncates.
        if (mode == kWriteCreateNew && fileHandle && *fileHandle && getenv("UI_SHOT_FATFS_REOPEN") && static_cast<HFile*>(*fileHandle)->path == (filepath ? filepath : ""))
            m = "r+b";
        if (mode == kWriteCreateNew || mode == kWriteAppend)
        {
            const size_t sl = p.rfind('/');
            if (sl != std::string::npos) system(("mkdir -p '" + p.substr(0, sl) + "'").c_str());
        }
        FILE* f = cardOpen(filepath ? filepath : "") ? fopen(p.c_str(), m) : nullptr;
        if (!f && mode == kWriteOpen) f = fopen(p.c_str(), "w+b");
        HFile* h = (fileHandle && *fileHandle) ? static_cast<HFile*>(*fileHandle) : nullptr;
        if (!h)
        {   // FileSystemImpl::allocateSdFile: a free slot of the pool, else the open fails and the caller gets no handle
            static const int limit = getenv("UI_SHOT_FILE_POOL") ? atoi(getenv("UI_SHOT_FILE_POOL")) : 24;
            if (limit > 0 && hostp::g_filePoolUsed >= limit)
            {
                hostp::g_filePoolRefused++;
                if (f) fclose(f);
                return {false, nullptr};
            }
            h = new HFile;
            h->slot = true;
            hostp::g_filePoolHeld.insert(h);
            if (++hostp::g_filePoolUsed > hostp::g_filePoolPeak) hostp::g_filePoolPeak = hostp::g_filePoolUsed;
            if (fileHandle) *fileHandle = h;
        }
        h->f = f;
        h->path = filepath;
        if (!f && !fileHandle)
        {   // FileSystemImpl::openFile: a failed open without a caller's handle gives the slot back and answers no handle
            h->slot = false;
            hostp::g_filePoolUsed--;
            hostp::g_filePoolHeld.erase(h);
            delete h;
            return {false, nullptr};
        }
        return {f != nullptr, h};
    }
    int32_t readFile(void* d, size_t n, FileHandle h) override
    {
        backendHandle();
        auto* f = static_cast<HFile*>(h);
        if (!f || !f->f) return -1;
        const long ok = cardRead(f->path, ftell(f->f), long(n));
        if (ok < 0) return -1;
        return int32_t(fread(d, 1, size_t(ok), f->f));
    }
    int32_t writeFile(const void* d, size_t n, FileHandle h) override
    {
        backendHandle();
        auto* f = static_cast<HFile*>(h);
        return f && f->f ? int32_t(fwrite(d, 1, n, f->f)) : -1;
    }
    bool seekFileCur(int off, FileHandle h) override { auto* f = static_cast<HFile*>(h); return f && f->f && fseek(f->f, off, SEEK_CUR) == 0; }
    bool seekFile(uint32_t pos, FileHandle h) override { auto* f = static_cast<HFile*>(h); return f && f->f && fseek(f->f, long(pos), SEEK_SET) == 0; }
    void closeFile(FileHandle h, bool deleteOnClose) override
    {
        backendHandle();
        auto* f = static_cast<HFile*>(h);
        if (f && f->f) { cardClose(f->path); fclose(f->f); f->f = nullptr; }
        // the slot goes back to the pool only with deleteOnClose (the memory stays: a caller may still look at the handle, as the static pool of the device allows)
        if (f && deleteOnClose && f->slot) { f->slot = false; hostp::g_filePoolUsed--; hostp::g_filePoolHeld.erase(f); }
    }
    void endFile(FileHandle h) override { auto* f = static_cast<HFile*>(h); if (f && f->f) { fflush(f->f); if (ftruncate(fileno(f->f), ftell(f->f)) != 0) {} } }
    bool isFileOpen(FileHandle h) override { auto* f = static_cast<HFile*>(h); return f && f->f; }
    bool enableFastSeek(FileHandle, uint32_t*, uint32_t, uint32_t*, uint32_t*) override { return false; }
    bool readSectorsAsync(uint8_t*, uint32_t, uint32_t) override { return false; }
    bool pollAsyncRead(bool* ok) override { if (ok) *ok = false; return true; }
    bool waitAsyncRead() override { return true; }
    bool isAsyncReadBusy() override { return hostp::g_asyncBusy != 0; }

    // directory listings: names of the entries of path, folders first when asked (a stand-in: the file browsers are not drawn here)
    static std::vector<std::string> names(const std::string& dir, bool folders)
    {
        HostFaultSuspend keep;
        std::vector<std::string> v;
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                if (e->d_name[0] == '.') continue;
                struct stat st;
                if (stat((dir + "/" + e->d_name).c_str(), &st) != 0) continue;
                if ((S_ISDIR(st.st_mode) != 0) == folders) v.push_back(e->d_name);
            }
            closedir(d);
        }
        std::sort(v.begin(), v.end());
        return v;
    }
    // the firmware's SdDir::createInstrumentsList (SD.cpp): the files "N name.pti" of the folder, N = 1..48, list[N - 1] = new char[] with
    // the name between "N " and ".pti", a second file with the same N is skipped, sizeOut adds up the file sizes. (The older stub returned an
    // empty list, so a project opened from a card folder came up without its instruments.)
    int createInstrumentsList(const char* path, char** list, uint8_t length, uint32_t* sizeOut) override
    {
        if (sizeOut) *sizeOut = 0;
        int found = 0;
        for (const std::string& f : names(abs(path), false))
        {
            if (found >= length) break;
            const size_t n = f.size();
            if (n < 5 || !hasExt(f, ".pti")) continue;
            size_t i = 0;
            if (f[i] < '0' || f[i] > '9') continue;
            int idx = f[i++] - '0';
            if (f[i] >= '0' && f[i] <= '9') idx = idx * 10 + (f[i++] - '0');
            if (f[i] != ' ') continue;
            i++;
            if (idx < 1 || idx > 48 || list[idx - 1] != nullptr) continue;
            const size_t len = n - i - 4;
            list[idx - 1] = new char[len + 1];
            memcpy(list[idx - 1], f.c_str() + i, len);
            list[idx - 1][len] = 0;
            if (sizeOut)
            {
                struct stat st;
                if (stat((abs(path) + "/" + f).c_str(), &st) == 0) *sizeOut += uint32_t(st.st_size);
            }
            found++;
        }
        return found;
    }
    int countContainedFiles(const char* path) override { return int(names(abs(path), false).size()); }
    // the firmware's SdDir::createFilesList: frees what the list held, fills it from slot start_line on with new char[] entries
    // (folders as "/name", files by name), skips skip_count matches and returns the slot after the last entry, kListError when the
    // folder cannot be opened. Callers own the entries and delete them. A folder bigger than max_files_in_subdir is not split
    // into "(1-100)" parts here.
    static bool hasExt(const std::string& s, const char* e)
    {
        const size_t n = strlen(e);
        return s.size() > n && strcasecmp(s.c_str() + s.size() - n, e) == 0;
    }
    int createFilesList(const char* path, uint8_t startLine, char** list, uint8_t length, uint16_t, enListFileFilter filter, uint16_t skip, uint16_t, enListFileFilter) override
    {
        for (uint8_t i = 0; i < length; i++)
            if (list[i] != nullptr) { delete[] list[i]; list[i] = nullptr; }
        DIR* d = opendir(abs(path).c_str());
        if (d == nullptr) return kListError;
        closedir(d);
        const bool folders = filter == enListFileFilter_folder;
        auto v = names(abs(path), folders);
        int n = startLine;
        for (size_t i = 0; i < v.size() && n < length; i++)
        {
            if (!folders && filter == enListFileFilter_wave && !hasExt(v[i], ".wav")) continue;
            if (!folders && filter == enListFileFilter_wave_inst && !hasExt(v[i], ".wav") && !hasExt(v[i], ".pti")) continue;
            if (skip > 0) { skip--; continue; }
            const std::string e = folders ? "/" + v[i] : v[i];
            list[n] = new char[e.size() + 1];
            memcpy(list[n], e.c_str(), e.size() + 1);
            n++;
        }
        return n;
    }
    // the firmware's FileSystemImpl::listDirContents (platforms/mk66/source/FileSystemImpl.cpp): outCount comes in as the capacity of fileList and goes out as the
    // number of entries, a FOLDER name is stored with a leading '/' ("/New Project", the browser (FileBrowser.cpp) builds its paths as currentPath + name and
    // "%s%s/project.mt", so a stub without the '/' made every project row of the Open browser look like a plain folder: F8 Open did nothing), an entry longer
    // than maxNameLen is skipped, the list ends when the names buffer is full, the list is sorted with compareStrings (digits first as numbers, case blind) like the firmware
    void listDirContents(const char* dir, DirContentType type, std::function<bool(const char*)> nameFilter, char* buf, int bufSize, int maxNameLen, int& outCount, char** fileList) override
    {
        const int cap = outCount;
        outCount = 0;
        auto v = names(abs(dir), type == kFolders);
        int used = 0;
        for (auto& s : v)
        {
            if (outCount >= cap) break;
            if (nameFilter && !nameFilter(s.c_str())) continue;
            const bool folder = type == kFolders;
            const int nameLen = int(s.size()) + (folder ? 2 : 1);
            if (nameLen > bufSize - used) break;
            if (nameLen > maxNameLen) continue;
            char* at = buf + used;
            if (folder) *at++ = '/';
            memcpy(at, s.c_str(), s.size());
            at[s.size()] = 0;
            if (fileList) fileList[outCount] = buf + used;
            used += nameLen;
            outCount++;
        }
        if (fileList) std::sort(fileList, fileList + outCount, [](const char* a, const char* b) { return compareStrings(a, b); });
    }
    int readNbFiles(const char*) override { return 0; }
    // one directory walk at a time, like the firmware's
    bool nesDir_open(const char* path) override
    {
        nesDir_close();
        nesPath_ = path ? path : "";
        nesIndex_ = 0;
        cardAccess("dirOpen", nesPath_);
        nesBase_ = abs(path);
        nesDir_ = opendir(nesBase_.c_str());
        return nesDir_ != nullptr;
    }
    bool nesDir_readItem(char* name, uint8_t* isDir, uint8_t* isHid, uint32_t* size) override
    {
        while (nesDir_ != nullptr)
        {
            dirent* e = readdir(nesDir_);
            if (e == nullptr) return false;
            struct stat st;
            if (stat((nesBase_ + "/" + e->d_name).c_str(), &st) != 0) continue;
            bool hid = false;
            if (!cardDirItem(nesPath_, nesIndex_++, e->d_name, hid)) return false;
            strcpy(name, e->d_name);
            *isDir = S_ISDIR(st.st_mode) ? 1 : 0;
            *isHid = hid ? 1 : 0;
            if (size) *size = uint32_t(st.st_size);
            return true;
        }
        return false;
    }
    void nesDir_close() override
    {
        if (nesDir_ != nullptr) closedir(nesDir_);
        nesDir_ = nullptr;
    }

  private:
    static void backendHandle()
    {
#ifdef FEATURE_USB_MSC
        if (hostp::g_driveOwned) hostp::g_driveBackend++;
#endif
    }
    DIR* nesDir_ = nullptr;
    std::string nesBase_, nesPath_;
    int nesIndex_ = 0;
};

#ifdef FEATURE_USB_MSC
#include "host_drive.h"
#endif

class HPlatform : public Platform
{
  public:
    void setPowerMode(bool on) override
    {
        if (!on && g_powerOffCalls++ == 0) g_powerOffAtMs = g_nowMs;
        if (!on)
        {
            g_guardArmed = 0;   // the MK66 disarms it after the teardown, before the sleep
            g_startState = g_restartOnPowerOff ? 1 : 0;   // saveStartState(restart ? 1 : 0)
#ifdef UISHOT_HAVE_GUARD
            g_guard.disarm();
            if (g_restartOnPowerOff)
            {   // restart mode: the reset of a power-on instead of the sleep
                mcuReset("restart", false);
                return;
            }
#endif
            g_unitOff = true;
        }
    }
    // no override: also builds against a tree without the restart mode
    void setRestartOnPowerOff(bool on) { g_restartOnPowerOff = on; }
    // no override: also builds against a tree without the features (PlatformImpl::setStartOnPowerReturn, saveStartState)
    void setStartOnPowerReturn(bool on)
    {
        g_returnMirror = on ? 1 : 0;   // PlatformImpl: the mirror for the next start, then the start state now
        g_startState = on ? 1 : 0;
    }
    // no override: also builds against a tree without USB audio. UISHOT_NO_USB_AUDIO=1 models an image built without it
    bool usbAudioBuilt() { return getenv("UISHOT_NO_USB_AUDIO") == nullptr; }
    bool usbAudioRunning() { return g_usbAudioRunning != 0; }
    void setUsbAudioBoot(bool on) { g_usbAudioFlag = on ? 1 : 0; }
    // no override: also builds against a tree without the 8+3 mode. The sink is host_usbmc.cpp (the platform's interleave)
    uint8_t usbAudioRunningMode() { return g_usbAudioRunning && g_usbAudioRunningMode ? 1 : 0; }
    // no override: also builds against a tree without it
    uint8_t usbAudioStreaming() { return g_usbAudioRunning && g_usbAudioStreaming ? (usbAudioRunningMode() ? 2 : 1) : 0; }
    void setUsbAudioBootMode(uint8_t mode) { g_usbAudioMode = mode == 1 ? 1 : 0; }
    using HostMultiSink = void (*)(const int16_t* const* in);
    HostMultiSink usbAudioMultiSink() { return usbAudioRunningMode() ? hostUsbMultiSink : nullptr; }
#ifdef FEATURE_USB_MSC
    bool usbDriveRunning() override { return g_driveRunning != 0; }
    void setUsbDriveBoot(bool on) override { g_driveFlag = on ? 1 : 0; }
#endif
    // no override: also builds against a tree without the guard (bug mode)
    void setPowerOffGuard(bool armed)
    {
        g_guardArmed = armed;
#ifdef UISHOT_HAVE_GUARD
        if (g_wdogModel)
        {   // 5878f0e: the arm starts the K66 WDOG, the disarm does not stop it
            if (armed && g_wdogBiteAt == 0) g_wdogBiteAt = g_nowMs + power_off_guard::kTimeoutMs;
            return;
        }
        if (armed)
        {
            g_startState = 0;   // saveStartState(0)
            g_guard.arm(g_nowMs);
        }
        else
        {
            g_guard.disarm();
        }
#endif
    }
    void kickPowerOffGuard()
    {
        if (g_guardArmed) g_guardKicks++;
#ifdef UISHOT_HAVE_GUARD
        if (!g_wdogModel) g_guard.kick(g_nowMs);   // the K66 ignores the WDOG refresh
#endif
    }
    void resetMCU() override { g_resetRequested = true; }
    void runBootloader(char* name) override { hostp::simBootloaderHandoff(name); }
    void setPowerButtonCallback(PowerButtonCallback f) override { g_powerButtonCallback = f; }
    bool getPowerButtonState() override { return true; }
    bool readForceStartUp() override { return true; }
    Sgtl5000* getCodecAudio() override { return &codec; }
    Radio* getRadio() override { return &radio; }
    SwitchGroup* getButtons() override { return slots_.buttons; }
    SwitchGroup* getPads() override { return slots_.pads; }
    Leds* getLeds() override { return slots_.leds; }
    Battery* getBattery() override { return slots_.battery; }
    Timer* getTimer() override { return &timer; }
    Encoder* getEncoder() override { return slots_.encoder; }
    DisplayDriver* getDisplayDriver() override { return slots_.display; }
    UsbAudio* getUsbAudio() override { return &usbAudio; }
    UartMidi* getUartMidi() override { return &uartMidi; }
    UsbMidi* getUsbMidi() override { return &usbMidi; }
    ConfigManager* getConfigManager() override { return &config; }
#ifdef FEATURE_USB_MSC
    FileSystem* getFileSystem() override { return ptiFileSystem(&driveGate); }
#else
    FileSystem* getFileSystem() override { return ptiFileSystem(&fs); }   // the card's .pti files in the host's struct (host_pti.cpp)
#endif
    CrcCalculator* getCrcCalculator() override { return &crc; }
    SystemInfo* getSystemInfo() override { return &info; }
    Usb* getUsb() override { return &usb; }

    HPlatform()
    {
        slots_ = {&leds, &buttons, &pads, &battery, &encoder, &eve};
        extendPlatform(slots_);   // after the members are built: an extension may keep the default it replaces and call through to it
    }

    HwSlots slots_;
    HTimer timer;
    HEncoder encoder;
    HBattery battery;
    HLeds leds;
    HSwitch buttons{false}, pads{true};
    HRadio radio;
    HCodec codec;
    HUsbAudio usbAudio;
#ifdef UISHOT_PLUS
    // the Tracker+ / Mini MIDI is the real driver (UsbMidiImpl, UartMidiImpl) over the byte level stub of host_imxmidi.cpp
    PlusUsbMidi usbMidi;
    UartMidiImpl uartMidi;
#else
    HUsbMidi usbMidi;
    HUartMidi uartMidi;
#endif
    HUsb usb;
    HCrc crc;
    HConfig config;
    HFileSystem fs;
#ifdef FEATURE_USB_MSC
    HDriveGate driveGate{fs};
#endif
    EveModel eve;
    SystemInfo info = {hostp::simBuildId()};
};

HPlatform* g_platform;
}   // namespace

Platform* platform()
{
    if (!g_platform) g_platform = new HPlatform;
    return g_platform;
}

#ifdef FEATURE_USB_MSC
// msc_drive.cpp on the MK66: the card is lent only when the drive was enumerated at start (ff_usb_msc_on)
static HDriveGate* driveGate()
{
    return &static_cast<HPlatform*>(platform())->driveGate;
}
#endif

EveModel& eve()
{
    platform();
    return g_platform->eve;
}
}   // namespace hostp

#ifdef FEATURE_USB_MSC
namespace hostp
{
namespace
{
void HUsb::setClassState(ClassType t, bool on)
{
    if (t == ClassType::kStorage && on && g_driveRunning) driveGate()->lendToHost();
}

bool HUsb::getClassState(ClassType t)
{
    return t == ClassType::kStorage && driveGate()->hostOwned();
}
}   // namespace
}   // namespace hostp
#endif

namespace hostp
{
int g_padLed[64];
std::string filePoolHeldPaths()
{
    std::string r;
    for (const void* p : g_filePoolHeld) r += " " + static_cast<const HFile*>(p)->path;
    return r;
}
}
