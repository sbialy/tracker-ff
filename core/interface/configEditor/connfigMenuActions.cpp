
#include <string.h>

#include "configEditor/configEditor.h"
#include "configEditor/configMenu.h"

#include "fileManager.h"

#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtSequencer.h"

#include "Platform.h"
#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"

#include "Tracker.h"
#include "core/interface.h"
#include "core/interfacePopups.h"

#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
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

#ifdef FEATURE_PLUG_HOST
// The Project Settings page (the File screen entry opens it): the three rows that are not a plain variable keep a copy here, read back
// from the project every time the page opens and after every change (a refused change shows what the project holds)
uint8_t projFxSlots;      // 0 Off, 1 On
uint8_t projMacroMode;    // 0 per patch, 1 Add, 2 Scale
uint8_t projStrumVolume;  // tens of percent, 0..10

void projectPageSync()
{
    projFxSlots = fx34::enabled() ? 1 : 0;
    projMacroMode = uint8_t(plug::macroMode());
    projStrumVolume = uint8_t((plug::strumRampDepth() + 5) / 10);
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
