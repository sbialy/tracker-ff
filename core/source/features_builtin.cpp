#ifdef FEATURE_EXT_HOOKS

// Built-in features: restart mode, developer mode with the extension module, USB audio.

#include "features.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "fx_auto_mute.h"
#include "fileManagerDefs.h"
#include "ext_hooks.h"
#include "mtCapture.h"
#include "mtChord.h"
#include "midiRemote.h"
#include "mtSleep.h"
#include "skipback.h"

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
// boot flag (platforms/mk66/usbaudio, byte 9) and needs a restart. The card file stays the truth, the flags follow it at start.
// Mode: Stereo (the master L/R, as before the mode existed) or 8+3 mono (11 channels: tracks 1-8 dry pre pan, synth parts 1-3).
// It has its own EEPROM byte (7) and is in the file only when it is not Stereo, so a missing line, an older image and a blank
// EEPROM all mean Stereo, and an older image that reads "mode 1" skips the unknown key.
// FX in 8+3: while the computer records the 8+3 stream, the master Delay and Reverb sleep (a runtime override on the Mixer's Mute, the
// project is never changed, fx_auto_mute.h). Same storage as the mode: a global setting, in the file only when it is not Auto mute, so a
// missing line, an older image and a blank card all mean Auto mute. Takes effect at once. Stereo never auto mutes.
const char* const kUsbMode[] = { "Stereo", "8+3 mono" };
const char* const kUsbFxMute[] = { "Auto mute", "Off" };
const feat_param kUsbAudioParams[] = {
    { "mode", "Audio mode", FEAT_P_ENUM, 0, 0, 0, 0, 2, kUsbMode, FEAT_PR_RESTART | FEAT_PR_OMIT_DEFAULT | FEAT_PR_ELSEWHERE,
      "Stereo: the master mix. 8+3 mono: tracks 1-8 dry before pan, synth parts 1-3. Takes effect at the next start." },
    { "fxmute", "FX in 8+3", FEAT_P_ENUM, 0, 0, 0, 0, 2, kUsbFxMute, FEAT_PR_OMIT_DEFAULT | FEAT_PR_ELSEWHERE,
      "Auto mute: while the computer records the 8+3 stream, Delay and Reverb sleep to save CPU. The Mute keys are locked and your mute state returns afterwards. Off: they run as set." },
};
uint8_t s_usbMode;   // the file's mode (boot, then each change)

void usbAudioFxMute(const uint8_t* v)
{
#ifdef AUDIO_NULL_IS_SILENCE
    fxAutoMute.setEnabled(v[1] == 0);   // Off releases the override at once
#else
    (void)v;
#endif
}

void usbAudioApply(uint8_t on, const uint8_t* v, uint8_t which)
{
    s_usbMode = v[0];
    usbAudioFxMute(v);
    if (which == 0xFF) platform()->setUsbAudioBoot(on != 0);
    else if (which == 0) platform()->setUsbAudioBootMode(v[0]);
}

void usbAudioBoot(uint8_t, const uint8_t* v)
{
    s_usbMode = v[0];
    usbAudioFxMute(v);
    platform()->setUsbAudioBootMode(v[0]);   // the mirror follows the file (writes only on a change)
}

const char* usbAudioStatus(uint8_t on)
{
    if (!on) return "Off";
    // the session runs another mode than the file asks for (the EEPROM mirror was out of step at this start): the next start
    if (platform()->usbAudioRunning() && platform()->usbAudioRunningMode() != s_usbMode) return "Restart";
    return kUsbMode[s_usbMode < 2 ? s_usbMode : 0];
}

uint8_t usbAudioRunning()
{
    return platform()->usbAudioRunning() ? 1 : 0;
}

const feat_desc kUsbAudio = {
    sizeof(feat_desc), FEAT_ABI, 1, "usbaudio", "USB audio",
    "The unit shows up as an audio input on a computer. Takes effect at the next start. Config > USB picks Stereo or 8+3 mono.", FEAT_F_RESTART | FEAT_F_BOOTFLAG, 2,
    kUsbAudioParams, 0, usbAudioBoot, usbAudioApply, usbAudioStatus, nullptr, nullptr, usbAudioRunning,
};

#ifdef FEATURE_USB_MSC
// ---- USB drive (USB_MSC=1): the mass storage interface is chosen in usb_init like USB audio, so the switch is an EEPROM boot
// flag too (platforms/mk66/usbmsc, byte 7) and needs a restart. Drive mode itself starts from Config > USB > Storage.
void usbDriveApply(uint8_t on, const uint8_t*, uint8_t)
{
    platform()->setUsbDriveBoot(on != 0);
}

uint8_t usbDriveRunning()
{
    return platform()->usbDriveRunning() ? 1 : 0;
}

const feat_desc kUsbDrive = {
    sizeof(feat_desc), FEAT_ABI, 1, "usbdisk", "USB drive",
    "Config > USB > Storage shows the SD card on a computer as a USB drive. Back up the card first. Takes effect at the next "
    "start.", FEAT_F_RESTART | FEAT_F_BOOTFLAG, 0,
    nullptr, 0, nullptr, usbDriveApply, nullptr, nullptr, nullptr, usbDriveRunning,
};
#endif

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

// ---- Play stop lock: Play alone no longer stops a running song (a touch on the key during a live set), Shift + Play does.
void playLockApply(uint8_t on, const uint8_t*, uint8_t)
{
    interfaceGlobals.playStopLock = on != 0;
}

void playLockBoot(uint8_t on, const uint8_t* v)
{
    playLockApply(on, v, 0xFF);
}

const feat_desc kPlayLock = {
    sizeof(feat_desc), FEAT_ABI, 1, "playlock", "Play stop lock",
    "On: Play does not stop a running song, Shift + Play does. Starting a song is not affected.", 0, 0,
    nullptr, 0, playLockBoot, playLockApply, nullptr, nullptr, nullptr, nullptr,
};

// ---- Demo start: a start with an empty project that leaves the Workspace of the last session alone. The Workspace folder is parked
// as Workspace.keep, the unit finds no Workspace and opens a new project, which autosaves into a fresh Workspace as always. Switched off,
// the demo Workspace is dropped and Workspace.keep comes back. Whatever the demo made is gone unless it was saved to Projects.
void demoBoot(uint8_t on, const uint8_t*)
{
    FileSystem* fs = platform()->getFileSystem();
    const bool parked = fs->exists(cWorkspaceKeepPath);
    if (on != 0 && fs->exists(cWorkspacePath))
    {
        if (parked)
        {
            fs->removeDirWithFiles(cWorkspacePath);   // the Workspace of an earlier demo start
        }
        else
        {
            fs->rename(cWorkspacePath, cWorkspaceKeepPath);   // fails: the Workspace stays and opens as it always did
        }
    }
    else if (on == 0 && parked)
    {
        if (fs->exists(cWorkspacePath))
        {
            fs->removeDirWithFiles(cWorkspacePath);
        }
        fs->rename(cWorkspaceKeepPath, cWorkspacePath);   // fails: still parked, the next start tries again
    }
}

const feat_desc kDemo = {
    sizeof(feat_desc), FEAT_ABI, 1, "demo", "Demo start",
    "On: the unit starts with an empty project and leaves your last Workspace alone. Off: it comes back. Takes effect at the next start.",
    FEAT_F_RESTART, 0, nullptr, 0, demoBoot, nullptr, nullptr, nullptr, nullptr, nullptr,
};

// ---- MIDI remote (midiRemote.h): a controller on one channel plays the Tracker's keys and pads, CC 96 and 97 turn the encoder
const feat_param kMidiRemoteParams[] = {
    { "channel", "Channel", FEAT_P_INT, 0, 15, 15, 1, 0, nullptr, 0, "The MIDI channel the remote listens on." },
    { "base", "First note", FEAT_P_INT, 0, 95, 36, 0, 0, nullptr, 0,
      "MIDI note of the first key (F1). 33 notes are the keys, the next 48 the pads. 36 is C2." },
};

void midiRemoteApply(uint8_t on, const uint8_t* v, uint8_t)
{
    midi_remote::set(on != 0, uint8_t(v[0] + 1), v[1]);
}

void midiRemoteBoot(uint8_t on, const uint8_t* v)
{
    midiRemoteApply(on, v, 0xFF);
}

const feat_desc kMidiRemote = {
    sizeof(feat_desc), FEAT_ABI, 1, "midirmt", "MIDI remote",
    "On: notes on one MIDI channel press the Tracker's keys and pads, CC 96 and 97 turn the encoder.", 0, 2,
    kMidiRemoteParams, 0, midiRemoteBoot, midiRemoteApply, nullptr, nullptr, nullptr, nullptr,
};
#ifdef FEATURE_SKIPBACK
// ---- Skip-back: the last seconds of the master output in an SDRAM ring (skipback.h), Shift + Sample Rec saves them to /Skipback.
// Off by default: the audio block returns at the first test and the card is not touched.
const char* const kSkipChannels[] = { "Stereo", "Mono" };
const char* const kSkipSeconds[] = { "1 s", "2 s", "3 s", "4 s", "5 s", "6 s" };
static_assert(sizeof(kSkipSeconds) / sizeof(kSkipSeconds[0]) == skipback::kMaxSeconds, "one text per second");
const feat_param kSkipbackParams[] = {
    // Mono by default: the original Tracker plays its samples in mono, and the ring holds twice as long
    { "chan", "Channels", FEAT_P_ENUM, 0, 1, skipback::kMono, 0, 2, kSkipChannels, 0,
      "Mono (left + right) keeps the last 5.9 s, Stereo the last 2.9 s. A change clears what was captured." },
    // an enum, not FEAT_P_INT: an INT row of the page edits the byte without calling features::changed (configMenu.cpp), so it would
    // neither apply nor save. Index + 1 = seconds
    { "secs", "Seconds", FEAT_P_ENUM, 0, skipback::kMaxSeconds - 1, skipback::kMaxSeconds - 1, 0, skipback::kMaxSeconds, kSkipSeconds, 0,
      "How many seconds Shift + Sample Rec saves, up to what the buffer holds (2.9 s Stereo, 5.9 s Mono)." },
};

void skipbackApply(uint8_t on, const uint8_t* v, uint8_t)
{
    skipback::configure(on != 0, v[0], uint8_t(v[1] + 1));
}

void skipbackBoot(uint8_t on, const uint8_t* v)
{
    skipbackApply(on, v, 0xFF);
}

const char* skipbackStatus(uint8_t on)
{
    if (!on) return "Off";
    return skipback::saving() ? "Saving" : "On";
}

const feat_desc kSkipback = {
    sizeof(feat_desc), FEAT_ABI, 1, "skipback", "Skip-back",
    "Keeps the last seconds of the master output. Shift + Sample Rec saves them as a WAV file in /Skipback.", 0, 2, kSkipbackParams, 0,
    skipbackBoot, skipbackApply, skipbackStatus, nullptr, nullptr, nullptr,
};
#endif

// ---- Capture: the pattern editor keeps what is played (green mode) and under-screen button 3 puts the last pass of the pattern
// into the copy buffer while the pattern plays (the stock Paste places it). Off by default: the button stays the stock (greyed Fill) one.
const feat_param kCaptureParams[] = {
    { "idle", "Idle time", FEAT_P_INT, kCaptureIdleMin, kCaptureIdleMax, kCaptureIdleDefault, 0, 0, nullptr, 0,
      "Seconds without a played note after which Capture forgets the notes, so a Capture takes only the latest phrase. 20 by default." },
};

void captureApply(uint8_t on, const uint8_t* v, uint8_t)
{
    captureFeatureOn = on != 0;
    captureIdleSec = v[0] < kCaptureIdleMin ? kCaptureIdleMin : (v[0] > kCaptureIdleMax ? kCaptureIdleMax : v[0]);
}

void captureBoot(uint8_t on, const uint8_t* v)
{
    captureApply(on, v, 0xFF);
}

const feat_desc kCapture = {
    sizeof(feat_desc), FEAT_ABI, 1, "capture", "Capture",
    "Pattern editor, play mode: button 3 Capture puts the last pass you played on the clipboard. Paste with Shift + Copy. "
    "Hold button 3 to forget the played notes. The square in the top bar shows the state.", 0, 1,
    kCaptureParams, 0, captureBoot, captureApply, nullptr, nullptr, nullptr, nullptr,
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
    const int restart = add(&kRestart);
    const int dev = add(&kDevelop);
    if (platform()->usbAudioBuilt()) add(&kUsbAudio);
#ifdef FEATURE_USB_MSC
    add(&kUsbDrive);
#endif
    add(&kChordNames);   // an image without USB_AUDIO=1 does not list it
    add(&kPlayLock);
    const int demo = add(&kDemo);
    add(&kMidiRemote);
#ifdef FEATURE_SKIPBACK
    add(&kSkipback);
#endif
    const int capture = add(&kCapture);
    // decision D8: the row only with /.develop on the card, or while the feature is on (so it can be switched off again).
    // Restart mode, Demo start and Capture follow the same rule, hiding only drops the row from the list, the feature itself is unchanged
    if (!platform()->getFileSystem()->exists("/.develop"))
    {
        hideUnlessOn(restart);
        hideUnlessOn(dev);
        hideUnlessOn(demo);
        hideUnlessOn(capture);
    }
    setRestartNow(restartNowFn);
}
}   // namespace features

#endif
