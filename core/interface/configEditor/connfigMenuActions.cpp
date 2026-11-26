
#include <string.h>

#include "configEditor/configEditor.h"
#include "configEditor/configMenu.h"

#include "fileManager.h"

#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtNoteName.h"
#include "mtTuneScale.h"
#include "mtMidi.h"
#include "mtSequencer.h"
#include "stereoPairs.h"
#ifdef FEATURE_PARTS
#include "parts.h"
#endif

#include "Platform.h"
#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"

#include "Tracker.h"
#include "core/interface.h"
#include "core/interfacePopups.h"

#ifdef FEATURE_FF_CORE
#include "plug_host.h"   // project.mtff settings through the plugin host's wrappers (projectSidecar.h, PLUG_HOST is on with FF_CORE)
#include "fx34_io.h"
#endif

// device startup actions after Config read
void initConfigValuesAfterReadConfig()
{
    usbAudioChange();
    setDisplayBrightness();
}

// reset config actions
void restartConfigAllActions()
{
    usbAudioChange();

    setRadioRegion();
    setDisplayBrightness();
    setPadsBrightness();

    setPadBoardValues();

    setAntialiasingEnable();

    setMetronomValues();
}

void setRadioRegion()
{
    if (mtConfig.general.radioRegion > 3)
        mtConfig.general.radioRegion = 0;

    mtInterface.getPlatform()->getRadio()->setRegion((Radio::Region)mtConfig.general.radioRegion);
}

void setDisplayBrightness()
{
    if (mtConfig.general.dispBrightness > 2)
        mtConfig.general.dispBrightness = 2;

    switch (mtConfig.general.dispBrightness)
    {
        case 0: display.setBacklightBrightness(20); break;
        case 1: display.setBacklightBrightness(50); break;
        case 2: display.setBacklightBrightness(128); break;
        default: break;
    }
}

void setDisplayTheme()
{
    if (mtConfig.general.theme >= ThemeManager::getThemeCount())
        mtConfig.general.theme = DisplayThemes::kOriginal;
    ThemeManager::applyTheme(mtConfig.general.theme);
    mtInterface.deactivateModule(&configEditor);
    mtInterface.activateModule(&configEditor, 0);
}

void setDisplayFont()
{
    if (mtConfig.general.font >= ThemeManager::getFontCount())
        mtConfig.general.font = 0;
    ThemeManager::applyFont(mtConfig.general.font);
    mtInterface.deactivateModule(&configEditor);
    mtInterface.activateModule(&configEditor, 0);
}

void setLineInOutType()
{
    if (mtConfig.general.lineInOutType > 2)
        mtConfig.general.lineInOutType = 2;
    engine.setLineInOutType(mtConfig.general.lineInOutType);
}

void setPadsBrightness()
{
    if (mtConfig.general.padsBrightness > 2)
        mtConfig.general.padsBrightness = 2;

    mtConfig.values.padsLightBack =
      (PADS_LIGHT_BACK_DEFAULT * (mtConfig.general.padsBrightness + 1)) / 3;
    mtConfig.values.padsLightFront =
      (PADS_LIGHT_FRONT_DEFAULT * (mtConfig.general.padsBrightness + 1)) / 3;
    mtConfig.values.padsLightBackDim =
      ((PADS_LIGHT_BACK_DEFAULT / 2) * (mtConfig.general.padsBrightness + 1)) / 3;
}

void setPadBoardValues()
{
    mtPadBoard.setPadNotes(mtProject.values.padBoardScale,
                           mtProject.values.padBoardNoteOffset,
                           mtProject.values.padBoardRootNote);
    newFileManager.setProjectStructChanged();
}

void setScaleSnap()
{
    newFileManager.setProjectStructChanged(); // the Workspace save writes the project sidecar (plug::projectSave)
}

void setAntialiasingEnable()
{
    engine.setInterpolationEnable(mtProject.values.antialiasingEnable);
    newFileManager.setProjectStructChanged();
}

void setLimiterMode()
{
    engine.setLimiterAttack(mtProject.values.limiterAttack);
    engine.setLimiterRelease(mtProject.values.limiterRelease);
    newFileManager.setProjectStructChanged();
}

void setSendsMode()
{
    engine.refreshAllSends();
    newFileManager.setProjectStructChanged();
}

void setHeadroomMode()
{
    engine.setHeadroomEnable((bool)mtProject.values.isHeadroomEnabled);
    newFileManager.setProjectStructChanged();
}

void setSnapToZeroMode()
{
    engine.setSnapToZeroEnable((bool)mtProject.values.isSnapToZeroEnabled);
    newFileManager.setProjectStructChanged();
}

void setRandomSeedValue()
{
    // the seed is read at the next Play (Sequencer::resetChanceState) and written to the project's sidecar by the Workspace save
    newFileManager.setProjectStructChanged();
}

void rerollRandomSeedAction()
{
    sequencer.rerollRandomSeed();
    setRandomSeedValue();
    configEditor.refreshConfigMenu(); // the Random seed row shows the new seed
}

#ifdef FEATURE_FF_CORE
void openProjectPageAction()
{
    configEditor.openProjectPage();
}

// The Project Settings page (the File screen entry opens it): the three rows that are not a plain variable keep a copy here, read back
// from the project every time the page opens and after every change (a refused change shows what the project holds)
uint8_t projFxSlots;      // 0 Off, 1 On
uint8_t projMacroMode;    // 0 per patch, 1 Add, 2 Scale
uint8_t projStrumVolume;  // tens of percent, 0..10
uint8_t projMpeOut;       // MPE out member channels, 0 Off .. 15
#ifndef PLATFORM_IMX
uint8_t projPair[4];       // linked stereo pairs 1+2 3+4 5+6 7+8, 0 Mono, 1 Stereo
#endif
uint8_t projTuneNames;   // Note names: 0 Degrees, 1 Ups+downs (tuning::cfg.names)
uint8_t projTuneFile;   // Scale file: 0 EDO, 1.. the files of /Tunings (mtTuneScale.h)
uint8_t projTuneN = 11, projTunePeriod, projTuneRoot = 60, projTuneHz;   // Tuning: notes per period, period preset, 1/1 key, 1/1 Hz preset
// the fine rows: the period in 1e-3 cent as 100 cents, cents, 0.01 cent and 0.001 cent digits, the 1/1 Hz in 1e-4 Hz as 100 Hz, Hz, 0.01 Hz and
// 0.0001 Hz digits (a list holds at most 256 positions, so a value is edited as a few rows). Nothing of these is stored: they edit the same four
// project.mtff keys as the presets (tune.period, tune.hz), which are written only when the value differs from the default
uint8_t projPerH, projPerC, projPerD, projPerM, projHzH, projHzU, projHzD, projHzE;
static uint8_t lastPeriodPreset = 7, lastHzPreset = 8;   // the preset rows as the page showed them: a changed one wins over the digit rows
static constexpr uint32_t kTunePeriods[7] = { 1200000, 1901955, 701955, 2400000, 1586314, 1205000, 1210000 };   // 1e-3 cent, the last of the list is Custom
static constexpr uint32_t kTuneHz[8] = { 2616256, 2640000, 2560000, 2568687, 2200000, 4320000, 4400000, 4420000 };   // 1e-4 Hz

void projectPageSync()
{
    projFxSlots = fx34::enabled() ? 1 : 0;
    projMacroMode = uint8_t(plug::macroMode());
    projStrumVolume = uint8_t((plug::strumRampDepth() + 5) / 10);
    projMpeOut = mpeOutMembers();
#ifndef PLATFORM_IMX
    for (int k = 0; k < 4; ++k) projPair[k] = stereopairs::pairLinked(k) ? 1 : 0;
#endif
    projTuneN = uint8_t(tuning::cfg.n - 1);   // the list shows 1..128 (stored 0..127 with the shown offset of 1, a list starts on its stored value)
    projTuneRoot = tuning::cfg.root;
    projTuneFile = uint8_t(tunescale::position());
    projTuneNames = tuning::cfg.names;
    projTunePeriod = 7;   // Custom unless a preset
    for (uint8_t i = 0; i < 7; ++i)
        if (kTunePeriods[i] == tuning::cfg.periodMc) projTunePeriod = i;
    projTuneHz = 8;
    for (uint8_t i = 0; i < 8; ++i)
        if (kTuneHz[i] == tuning::cfg.hzE4) projTuneHz = i;
    lastPeriodPreset = projTunePeriod;
    lastHzPreset = projTuneHz;
    const uint32_t p = tuning::cfg.periodMc, h = tuning::cfg.hzE4;
    projPerH = uint8_t(p / 100000);
    projPerC = uint8_t(p / 1000 % 100);
    projPerD = uint8_t(p / 10 % 100);
    projPerM = uint8_t(p % 10);
    projHzH = uint8_t(h / 1000000);
    projHzU = uint8_t(h / 10000 % 100);
    projHzD = uint8_t(h / 100 % 100);
    projHzE = uint8_t(h % 100);
}

// Tuning (project.mtff tune.*, mtTuning.h): the sample pitch tables are filled again, so not while the sequencer plays (a sounding voice would
// jump). A preset row that changed sets its preset, otherwise the digit rows are the value (a Custom period or Hz stays what the project holds
// until a digit row moves it). tuning::set clamps a value outside the range (period 10..4800 cents, Hz 8..4000) and the page shows the result
void setProjTune()
{
    if (sequencer.isPlay())
    {
        mtPopups.showInfoPopup("Tuning", "stop the playback first");
    }
    else
    {
        uint32_t period = uint32_t(projPerH) * 100000 + uint32_t(projPerC) * 1000 + uint32_t(projPerD) * 10 + projPerM;
        uint32_t hz = uint32_t(projHzH) * 1000000 + uint32_t(projHzU) * 10000 + uint32_t(projHzD) * 100 + projHzE;
        if (projTunePeriod != lastPeriodPreset && projTunePeriod < 7) period = kTunePeriods[projTunePeriod];
        if (projTuneHz != lastHzPreset && projTuneHz < 8) hz = kTuneHz[projTuneHz];
        // a Scala tuning keeps its degrees only while its note count and period stand: a change of either one is equal steps again
        if (tuning::cfg.file && (projTuneN + 1 != tuning::cfg.n || period != tuning::cfg.periodMc)) tunescale::off();
        tuning::set(projTuneN + 1, period, projTuneRoot, hz);
        tuningChanged();
        newFileManager.setProjectStructChanged();
    }
    projectPageSync();
    configEditor.refreshConfigMenu();
}

// Scale file (project.mtff tune.file and tune.cents): a .scl of /Tunings becomes the tuning, the reason of a refusal is a popup. Position 0 is N equal steps
void setProjTuneFile()
{
    if (sequencer.isPlay())
    {
        mtPopups.showInfoPopup("Tuning", "stop the playback first");
    }
    else if (projTuneFile == 0)
    {
        if (tuning::cfg.file)
        {
            tunescale::off();
            newFileManager.setProjectStructChanged();
        }
    }
    else if (projTuneFile <= tunescale::files())
    {
        if (tunescale::load(tunescale::list()[projTuneFile]))
            newFileManager.setProjectStructChanged();
        else
            mtPopups.showInfoPopup("Scale file", tunescale::message());
    }
    projectPageSync();
    configEditor.refreshConfigMenu();
}

// Note names (project.mtff tune.names): no pitch changes, so it works while the sequencer plays. Ups+downs for a tuning that has no such names is refused
void setProjTuneNames()
{
    if (projTuneNames && !(tuning::cfg.udOk && !tuning::cfg.file && tuning::cfg.periodMc == tuning::kDefPeriodMc))
        mtPopups.showInfoPopup("Note names", "no ups and downs here");
    else if (projTuneNames != tuning::cfg.names)
    {
        tuning::cfg.names = projTuneNames;
        newFileManager.setProjectStructChanged();
        mtPadBoard.refreshNotes();
    }
    projectPageSync();
    configEditor.refreshConfigMenu();
}

// 4 FX Slots (project.mtff fx.slots, fx34.h): not while the sequencer plays (the sets are rebuilt). Off keeps the FX3 and FX4 files on the
// card and says so, On brings them back
void setProjFxSlots()
{
    if ((projFxSlots != 0) != fx34::enabled())
    {
        if (sequencer.isPlay())
        {
            mtPopups.showInfoPopup("4 FX", "stop the playback first");
        }
        else
        {
            if (projFxSlots)
            {
                fx34io::turnOn();
                mtPopups.showInfoPopup("4 FX on", "FX3 and FX4, not CE compatible");
            }
            else if (fx34io::turnOff())
            {
                mtPopups.showInfoPopup("4 FX off", "FX3 and FX4 kept on the card, not played");
            }
            else
            {
                mtPopups.showInfoPopup("4 FX off", "2 FX slots again");
            }
            newFileManager.setProjectStructChanged();
        }
    }
    projectPageSync();
}

#ifndef PLATFORM_IMX
// Stereo pairs (project.mtff track.N.pair, stereoPairs.h): a linked pair plays one stereo voice. Not while the sequencer plays (the
// engine cuts both tracks). The first link of a project reloads the samples, the right channel of a stereo .pti loads only then
void setProjPair()
{
    uint8_t mask = 0;
    for (int k = 0; k < 4; ++k) mask |= uint8_t((projPair[k] ? 1 : 0) << k);
    if (mask != stereopairs::g_mask)
    {
        if (sequencer.isPlay())
        {
            mtPopups.showInfoPopup("Stereo pairs", "stop the playback first");
        }
        else
        {
            const bool wasStereo = stereopairs::g_mask != 0;
            const uint8_t oldMask = stereopairs::g_mask;
            engine.setStereoPairs(mask);
            midiMenuSync();
            // a fresh link hides the follower track: tell once when it holds notes or FX in the open pattern, in any pattern of the song playlist
            // or in any pattern a Part uses (the UI shows it when the reload popup is gone). The open pattern is read from memory, the others from
            // the per pattern track bitmask the project keeps (it counts a step with only an FX too, so only a note in the open pattern proves
            // "notes", every other hit says "notes or FX")
            for (int k = 0; k < stereopairs::kPairs && !stereopairs::g_linkNotice; ++k)
            {
                if (!((mask >> k) & 1) || ((oldMask >> k) & 1)) continue;
                const int ftrack = 2 * k + 1;
                auto& tr = sequencer.getActualPattern()->track[ftrack];
                bool hasNote = false;
                bool has = false;
                for (int st = 0; st <= tr.length && !hasNote; ++st)
                {
                    if (!tr.step[st].isNoteEmpty()) hasNote = true;
                    else if (tr.step[st].hasAnyFx()) has = true;
                }
                auto inBitmask = [&](int pat)   // 1 based pattern number
                {
                    return pat > 0 && pat <= PATTERN_INDEX_MAX && ((mtProject.values.allPatternsBitmask[pat - 1] >> ftrack) & 1);
                };
                for (int pos = 0; pos < SONG_MAX && !hasNote && !has && mtProject.song.playlist[pos]; ++pos)
                    has = inBitmask(mtProject.song.playlist[pos]);
#ifdef FEATURE_PARTS
                {
                    const parts::List& pl = parts::list();
                    for (int i = 0; i < int(pl.count) && i < parts::kMax && !hasNote && !has; ++i)
                        has = inBitmask(pl.part[i].src[ftrack]);
                }
#endif
                if (hasNote || has)
                {
                    stereopairs::g_linkNotice = uint8_t(ftrack + 1);
                    stereopairs::g_linkNoticeFx = !hasNote;
                }
            }
            newFileManager.setStereoSampleMode(mask != 0);
            if (!wasStereo && mask != 0) newFileManager.reloadSamplesFromWorkspace();
            newFileManager.setProjectStructChanged();
        }
    }
    projectPageSync();
    configEditor.refreshConfigMenu();
}
#endif

// MPE out (project.mtff mpe.out): the notes that sound are let go, the zone configuration goes out a few messages per audio block
void setProjMpeOut()
{
    mpeOutSet(projMpeOut);
    newFileManager.setProjectStructChanged();
}

void setProjMacroMode()
{
    plug::setMacroMode(projMacroMode);
    newFileManager.setProjectStructChanged();
}

void setProjStrumVolume()
{
    plug::setStrumRampDepth(projStrumVolume * 10);
    newFileManager.setProjectStructChanged();
}
#endif

void setMetronomValues()
{
    // wlaczenie wylaczenie metronomu
    // aktualizajca metrum
    // volume mozna brac pod uwage na bierzaco tam gdzie przetwarzanie metronomu

    // mtConfig.metronome.state;
    // mtConfig.metronome.timeSignature;
}

void setSlaveClockCompValue()
{
    sequencer.setExternalClockComp(mtConfig.midi.slaveClockComp);
}

void resetConfigShowPopup()
{
    configEditor.showResetConfigPopout();
    configEditor.ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);
}

void usbStorageEnter()
{
    startFileTransferMode();
}

void usbAudioChange()
{
    Usb* usb = mtInterface.getPlatform()->getUsb();

    switch (mtConfig.common.usbAudioMode)
    {
        case 1:
            usb->setClassState(Usb::ClassType::kAudioIn, true);
            usb->setClassState(Usb::ClassType::kAudioOut, false);
            break;
        case 2:
            usb->setClassState(Usb::ClassType::kAudioIn, false);
            usb->setClassState(Usb::ClassType::kAudioOut, true);
            break;
        case 3:
            usb->setClassState(Usb::ClassType::kAudioIn, true);
            usb->setClassState(Usb::ClassType::kAudioOut, true);
            break;
        default:
            usb->setClassState(Usb::ClassType::kAudioIn, false);
            usb->setClassState(Usb::ClassType::kAudioOut, false);
            break;
    }
#ifdef PLATFORM_IMX
    engine.refreshOutputSourceOutsideTheRecorder();
#endif
    usb->reset();
}

void checkFuncitonKey(int key)
{
    for (int idx = 0; idx < 8; idx++)
    {
        if (idx != key && mtConfig.interface.functionKeysBinding[idx] ==
                            mtConfig.interface.functionKeysBinding[key])
        {
            int firstFreeFunction = 0;
            bool assigned;

            for (uint8_t functionIdx = 0; functionIdx < kMenuModulesCount; functionIdx++)
            {
                assigned = false;
                for (uint8_t keyIdx = 0; keyIdx < 8; keyIdx++)
                {
                    if (mtConfig.interface.functionKeysBinding[keyIdx] == functionIdx)
                    {
                        assigned = true;
                    }
                }

                if (!assigned)
                {
                    firstFreeFunction = functionIdx;
                    break;
                }
            }

            mtConfig.interface.functionKeysBinding[idx] = firstFreeFunction;
        }
    }
}

void setFunctionKey1()
{
    checkFuncitonKey(0);
}

void setFunctionKey2()
{
    checkFuncitonKey(1);
}

void setFunctionKey3()
{
    checkFuncitonKey(2);
}

void setFunctionKey4()
{
    checkFuncitonKey(3);
}

void setFunctionKey5()
{
    checkFuncitonKey(4);
}

void setFunctionKey6()
{
    checkFuncitonKey(5);
}

void setFunctionKey7()
{
    checkFuncitonKey(6);
}

void setFunctionKey8()
{
    checkFuncitonKey(7);
}
