#ifdef FEATURE_EXT_HOOKS

// Built-in features: restart mode, developer mode with the extension module, USB audio.

#include "features.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "ext_hooks.h"
#include "mtChord.h"
#include "mtSleep.h"

namespace features
{
namespace
{
const char* const kOnOff[] = { "Off", "On" };

Platform* platform()
{
    return TrackerImpl::getInstance()->getPlatform();
}

// ---- restart mode (RestartMode.h): on = a power off restarts the unit
const char* const kReturn[] = { "Stay off", "Start" };
const char* const kButton[] = { "Turn off", "Restart" };
const feat_param kRestartParams[] = {
    { "button", "Power button", FEAT_P_ENUM, 0, 0, 1, 0, 2, kButton, 0,
      "Restart: a power off restarts the unit. Turn off: the power button works as usual." },
    { "return", "Power return", FEAT_P_ENUM, 0, 0, 0, 0, 2, kReturn, 0,
      "Start: the unit starts by itself when power comes back after a cut while it was on." },
    { "reckey", "Record shortcut", FEAT_P_ENUM, 0, 0, 1, 0, 2, kOnOff, 0,
      "On: hold Record at start to switch the mode on, Record + power to switch it off." },
};

void restartApply(uint8_t on, const uint8_t* v, uint8_t)
{
    lowPower.setRestartMode(on != 0 && v[0] != 0, v[2] != 0);
    lowPower.setStartOnPowerReturn(on != 0 && v[1] != 0);
}

void restartBoot(uint8_t on, const uint8_t* v)
{
    restartApply(on, v, 0xFF);
}

const feat_desc kRestart = {
    sizeof(feat_desc), FEAT_ABI, 1, "restart", "Restart mode",
    "Power options: restart at power off, start when power returns.", 0, 3, kRestartParams, 0,
    restartBoot, restartApply, nullptr, nullptr, nullptr, nullptr,
};

// ---- developer mode: the CPU readouts and, with Extension Load, the module of /Plugins/Ext (read at start only)
const char* const kExt[] = { "Off", "Load" };
const feat_param kDevelopParams[] = {
    { "ext", "Extension", FEAT_P_ENUM, 0, 0, 1, 0, 2, kExt, 1, "Load the extension module of /Plugins/Ext at start." },
};

const char* developStatus(uint8_t on)
{
    return on ? ext_status_short() : "Off";
}

const feat_desc kDevelop = {
    sizeof(feat_desc), FEAT_ABI, 1, "develop", "Developer mode",
    "CPU readouts and the extension module. Takes effect at the next start.", FEAT_F_RESTART, 1, kDevelopParams, 0,
    nullptr, nullptr, developStatus, nullptr, nullptr, nullptr,
};

// ---- USB audio: the descriptor set is chosen in usb_init before the card is up, so the switch is mirrored into an EEPROM
// boot flag (platforms/mk66/usbaudio, byte 9) and needs a restart. The card file stays the truth, the flag follows it at start.
void usbAudioApply(uint8_t on, const uint8_t*, uint8_t)
{
    platform()->setUsbAudioBoot(on != 0);
}

uint8_t usbAudioRunning()
{
    return platform()->usbAudioRunning() ? 1 : 0;
}

const feat_desc kUsbAudio = {
    sizeof(feat_desc), FEAT_ABI, 1, "usbaudio", "USB audio",
    "The unit shows up as an audio input on a computer. Takes effect at the next start.", FEAT_F_RESTART | FEAT_F_BOOTFLAG, 0,
    nullptr, 0, nullptr, usbAudioApply, nullptr, nullptr, nullptr, usbAudioRunning,
};

// ---- Chord names: the pattern grid shows a MIDI Chord FX value as a short chord name (on, the default) or as the stock interval
// digits (off). Display only, the project and the playback do not change.
void chordNamesApply(uint8_t on, const uint8_t*, uint8_t)
{
    chordShortNamesOn = on != 0;
}

void chordNamesBoot(uint8_t on, const uint8_t* v)
{
    chordNamesApply(on, v, 0xFF);
}

const feat_desc kChordNames = {
    sizeof(feat_desc), FEAT_ABI, 1, "chordnm", "Chord names",
    "MIDI Chord cells in the pattern show short chord names (maj, m7, ^7). Off: the interval digits (47B).", FEAT_F_DEFAULT_ON, 0,
    nullptr, 0, chordNamesBoot, chordNamesApply, nullptr, nullptr, nullptr, nullptr,
};

// ---- Restart now: the power press sequence (bar, Workspace save, files) ending in a restart
void restartNowFn()
{
    lowPower.requestRestartOnce();
    mtInterface.handlePowerButtonAction(1);
}
}   // namespace

void registerBuiltins()
{
    add(&kRestart);
    const int dev = add(&kDevelop);
    // decision D8: the row only with /.develop on the card, or while the feature is on (so it can be switched off again)
    if (!platform()->getFileSystem()->exists("/.develop")) hideUnlessOn(dev);
    if (platform()->usbAudioBuilt()) add(&kUsbAudio);
    add(&kChordNames);   // an image without USB_AUDIO=1 does not list it
    setRestartNow(restartNowFn);
}
}   // namespace features

#endif
