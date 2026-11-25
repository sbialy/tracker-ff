// Host stand-ins for the Platform HAL (core/include/Platform.h): everything the UI core asks the hardware for. NOT firmware.
// Time is virtual (advance() moves it), the SD card is a folder on the Mac, the display is the FT812 model.
#pragma once

#include "Platform.h"
#include "eve_model.h"

#include <map>
#include <set>
#include <string>

// host_usbmc.cpp: the 8+3 mono sink (Platform::usbAudioMultiSink) and its checks, the "usbmc" command
void hostUsbMultiSink(const int16_t* const* in);
void hostUsbMcCommand(const std::string& args);

namespace hostp
{
extern uint32_t g_nowMs;        // virtual clock, milliseconds
void advance(uint32_t ms);
void setHeld(int idx, bool on);   // what SwitchGroup::isButtonPressed reports (Shift is read that way)
// The sim hardware extension point. The default platform (this file's .cpp) is everything the firmware tools (run_tests.sh, shot_parity, the *_sim.sh
// scripts) need and builds without any other checkout. A console build (CONSOLE=1) can swap a device for its own through extendPlatform(), which
// the platform calls once when it is built: sim features that only the console shows (pad LED recording, button and display observers) live in
// poly-devtools host/ui_shot/host_hw.cpp and never touch this tree. A slot starts at the default device, an extension may wrap it (keep the
// pointer, forward to it) or replace it. Bump HOSTP_HW_API when HwSlots changes: host_hw.cpp checks it.
#define HOSTP_HW_API 1
struct HwSlots
{
    Leds* leds;
    SwitchGroup* buttons;
    SwitchGroup* pads;
    Battery* battery;
    Encoder* encoder;
    DisplayDriver* display;   // the EveModel by default. hostp::eve() is the same model whatever the slot holds: shot and the frame still read it
};
void extendPlatform(HwSlots& slots);   // weak, does nothing in a plain build
Platform* platform();
EveModel& eve();
extern int g_padLed[64];   // the last level each pad LED was set to (0 off)
void setSdRoot(const std::string& dir);

// Development console (make CONSOLE=1, a separate checkout: host/ui_shot/host_usbtest.cpp). host_platform.cpp has weak
// defaults for a plain build: no key is held by the firmware's Keys class, the build id is "uishot", the bootloader hand-off does nothing.
bool keyPressed(bool pad, int idx);   // the firmware's Keys class holds the state of the buttons and pads
extern bool g_resetRequested;        // Platform::resetMCU was called
// The power button: what the firmware registered with setPowerButtonCallback (Tracker.cpp onPowerButtonChange), and the last
// setPowerMode the core asked for. On the original setPowerMode(false) is the last step of the shutdown (PlatformImpl.cpp: Snooze).
extern Platform::PowerButtonCallback g_powerButtonCallback;
extern int g_powerOffCalls;          // setPowerMode(false) calls
extern uint32_t g_powerOffAtMs;      // virtual time of the first one
extern int g_guardArmed, g_guardKicks; // power-off stall guard (Platform::setPowerOffGuard): armed now, kicks while armed
// MK66 power model (host_platform.cpp): EEPROM start state byte, MCU resets (all, and those that hit a unit that was off)
extern int g_startState;
extern int g_returnMirror;     // EEPROM byte 8: Power return Start of the restart mode feature (checkPowerState keeps the 1)
// The device's SdFile pool (FileSystemImpl.cpp, FF_FS_LOCK = 24 slots): openFile takes a slot when the caller has no handle yet, only closeFile
// with deleteOnClose (the default) gives it back, so closeFile(h, false) of a handle the caller does not keep leaks it until the next boot.
// "fspool" prints the counters. UI_SHOT_FILE_POOL=N changes the limit (0: no limit), default 24
extern int g_filePoolUsed, g_filePoolPeak, g_filePoolRefused;
extern std::set<const void*> g_filePoolHeld;   // the HFile of every slot in use
std::string filePoolHeldPaths();                // their paths, space separated
extern int g_asyncBusy;       // "fsbusy 1": isAsyncReadBusy() answers true (a sample stream reads the card), for the Capture take file tests
extern int g_usbAudioFlag;     // EEPROM byte 9: the USB audio boot flag, read by usb_init at every MCU start
extern int g_usbAudioRunning;  // what usb_init picked at the last start
extern int g_usbAudioStreaming; // the computer records the stream now (usbstream N)
extern int g_usbAudioMode;     // EEPROM byte 7: 1 = 8+3 mono
extern int g_usbAudioRunningMode;
extern bool g_restartOnPowerOff;
extern int g_mcuResets, g_resetsWhileOff;
void bootModel();                    // the start decision of the first boot (UISHOT_START_STATE), before the UI starts
void resetCommand();                 // an MCU reset now: prints the start decision
void stall(uint32_t ms);             // the main loop blocked in a call for MS: time and SysTick go on
const std::string& sdRoot();
const char* simBuildId();
void simBootloaderHandoff(const char* name);   // Platform::runBootloader

// host_simcard.cpp: card faults (SIM_CARD_FAULTS), the audio interrupt (SIM_AUDIO), counters. HFileSystem asks before each card call
struct CardStats
{
    uint32_t calls, opens, reads, injected, audioViolations, waitMs;
    uint64_t bytes;
};
struct AudioStats
{
    uint32_t blocks, maxGapMs, maxMainMs;
};
extern volatile bool g_inAudio;   // set while the simulated audio interrupt runs
extern CardStats g_card;
extern AudioStats g_audio;
void audioTick();                                      // runs the audio blocks due at g_nowMs (SIM_AUDIO=1)
void cardAccess(const char* op, const std::string& path);
bool cardOpen(const std::string& path);               // false: injected open failure
void cardClose(const std::string& path);
long cardRead(const std::string& path, long pos, long n);   // bytes the read may return, -1: injected error
bool cardDirItem(const std::string& dir, int index, const char* name, bool& hidden);   // false: injected end of the walk
bool audioViolated();
void syncSystick();
#ifdef FEATURE_USB_MSC
// USB drive (host_drive.h): the computer owns the card, backend calls while it does (must stay 0), calls the gate refused, calls on
// a handle of an earlier mount, refused calls by name, the EEPROM boot flag (byte 7, UISHOT_DRIVE_FLAG) and usb_init's pick
extern bool g_driveOwned;
extern unsigned g_driveBackend, g_driveRefused, g_driveStale;
extern std::map<std::string, unsigned> g_driveRefusedBy;
extern int g_driveFlag, g_driveRunning;
#endif
}   // namespace hostp
