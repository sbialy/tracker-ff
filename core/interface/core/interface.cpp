#ifdef FEATURE_PARTS
#include "parts.h"
#endif
#include <string.h>

#include "core/interface.h"
#include "ThemeManager.h"
#include "configEditor/configEditor.h"
#include "configEditor/configMenuActions.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "core/titleBar.h"

#include "imageViewer/imageViewer.h"
#include "instrumentEditor/instrumentEditor.h"
#include "masterParams/masterParams.h"
#include "patternEditor/patternEditor.h"
#include "performanceMode/performanceMode.h"
#include "projectEditor/projectEditor.h"
#include "sampleEditor/sampleEditor.h"
#include "sampleImporter/sampleImporter.h"
#include "samplePlayback/samplePlayback.h"
#include "sampleRecorder/sampleRecorder.h"
#include "songEditor/songEditor.h"
#ifdef FEATURE_SYNTH_OPEN
#include "synthPlay/synthPlay.h"
#endif

#include "fileManager.h"
#include "mtConfig.h"
#include "mtPadBoard.h"
#include "mtStructs.h"
#include "mtTest.h"

#include "Platform.h"
#include "VoiceStreamManager.h"
#include "config.h"
#include "log.h"
#include "mtHardware.h"
#include "mtSleep.h"
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif

InputProcessor ip(mtInterfacePotsDef::interfacePotsCount,
                  mtInterfaceButtonsDef::interfaceButtonsCount,
                  mtInterfacePadsDef::interfacePadsCount);
Interface mtInterface;

strMtConfig mtConfig;
OCRAM_BSS_SECTION strMtProject mtProject;
InterfaceGlobals interfaceGlobals;
strGrid mtGrid;

extern void unmuteAudioOutByKey();
extern void muteAudioOutByKey();
extern uint8_t isAudioOutMuted();

//=======================================================================
//							SETAP INTERFEJSU
//=======================================================================
//=======================================================================

const uint8_t Interface::modulesCount = 13;
const hModule Interface::modules[modulesCount] = {
    &projectEditor,    // 0
    &sampleImporter,   // 1
    &patternEditor,    // 2
    &samplePlayback,   // 3
    &songEditor,       // 4
    &instrumentEditor, // 5
    &sampleEditor,     // 6
    &configEditor,     // 7
    &sampleRecorder,   // 8
    &performanceMode,  // 9
    &imageViewer,      // 10
#ifdef FEATURE_SYNTH_OPEN
    &synthPlay, // 11, the Synth play page: no page key, entered with Shift + Perform (trySynthPlayEntry)
#else
    nullptr,           // 11
#endif
    &masterParams,     // 12
};

const uint8_t Interface::modulesButtonsCount = 11;
const uint32_t Interface::modulesButtons[modulesButtonsCount][3] = {
    { interfaceButtonPerformance, 9, 0 },
    { interfaceButtonMaster, 12, 0 },
    { interfaceButtonParams, 5, mtInstEditModeParams },
    { interfaceButtonFile, 0, 0 },
    { interfaceButtonPattern, 2, 0 },
    { interfaceButtonSamplePlay, 3, 0 },
    { interfaceButtonSampleEdit, 6, 0 },
    { interfaceButtonSampleRec, 8, 0 },
    { interfaceButtonSampleLoad, 1, 0 },
    { interfaceButtonSong, 4, 0 },
    { interfaceButtonConfig, 7, 0 },
};

//=======================================================================
//=======================================================================
//=======================================================================
//=======================================================================

Platform* Interface::getPlatform()
{
    return platform;
}

void Interface::begin(Platform* platform, Synth* synth)
{
    Interface::platform = platform;
    Interface::fileSystem = platform->getFileSystem();
    Interface::ip = &::ip;
    patternEditor.initExternalRamState();
    Interface::synth = synth;
    operatingMode = mtOperatingModeStartup;

    buttons.init(platform->getButtons());
    titleBar.init(synth, platform->getBattery());

    ModuleBase::setPlatform(platform);
    ModuleBase::setEventFunct(interfaceEnvents);
    ModuleBase::setInputProcessor(ip);
    ModuleBase::setButtons(&buttons);
    ModuleBase::setTitleBar(&titleBar);
    ModuleBase::setSynth(synth);

    mtTest.setCodecAudio(platform->getCodecAudio());
    mtTest.setRadio(platform->getRadio());
    mtTest.setButtons(platform->getButtons());
    mtTest.setEncoder(platform->getEncoder());
    mtTest.setTimer(platform->getTimer());
    mtTest.setBattery(platform->getBattery());
    mtTest.setFileSystem(platform->getFileSystem());

    ip->unblockAllInputs();
}

//=======================================================================
//=======================================================================
//=======================================================================
void Interface::update()
{
    lowPower.updateRestartMode(getPlatform()->getButtons()->isButtonPressed(interfaceButtonRec));

    processOperatingMode();

    if (onScreenModule != nullptr && onScreenModule->moduleRefresh)
        onScreenModule->update();

    mtPopups.update();

    mtTest.testLoop();

    commonThingsUpdate();
}

//=======================================================================
//=======================================================================
//=======================================================================

void Interface::processOperatingMode()
{
    switch (operatingMode)
    {
        case mtOperatingModeStartup:
        {
            doStartTasks(); // odczyt configu, wersji, init controlek popupow, ekran startowy/braku
                            // karty
            operatingMode = mtOperatingModeOpenProject;
            break;
        }
        case mtOperatingModeOpenProject:
        {
            if (fileSystem->isSdCardInserted())
            {
                hideDisplayNoSdCard();
                openStartupProject();
                operatingMode = mtOperatingModeShowStartScreen;
            }
            break;
        }
        case mtOperatingModeShowStartScreen:
        {
            processStartScreen();
            break;
        }
        case mtOperatingModePowerOffSequence:
        {
            processPowerOffSequence();
            break;
        }
        case mtOperatingModeSleep:
        {

            break;
        }
        case mtOperatingModeRun:
        {
            if (isAudioOutMuted())
            {
                unmuteAudioOutByKey();
            }
            updateConfig(false);
            break;
        }
        default: break; // mtOperatingModeRun
    }
}

void Interface::doStartTasks()
{
    readConfig();

    sequencer.initAfterReadConfig();
    initConfigValuesAfterReadConfig();

    if (mtConfig.firmware.beta > 0 && mtConfig.firmware.beta < 255)
    {
        sprintf(interfaceGlobals.currentFirmwareVersion,
                firmwareVersionLabelFormatBeta,
                PRODUCT_VER_1,
                PRODUCT_VER_2,
                PRODUCT_VER_3,
                PRODUCT_BETA);
    }
    else
    {
        sprintf(interfaceGlobals.currentFirmwareVersion,
                firmwareVersionLabelFormat,
                PRODUCT_VER_1,
                PRODUCT_VER_2,
                PRODUCT_VER_3);
    }
    interfaceGlobals.currentFirmwareBuild = platform->getSystemInfo()->buildId;

    ThemeManager::init(platform);
    refreshThemeAndFontMenuOptions();

    songTimer.initControls();
    mtPopups.initPopupsDisplayControls();
    mtMenuPopup.initPopupsDisplayControls();
    titleBar.initControls();
    ModuleBase::keyboardManager.initControls();
    ModuleBase::keyboardManager.init(platform->getLeds(), platform->getTimer());

    ThemeManager::applyTheme(mtConfig.general.theme);
    ThemeManager::applyFont(mtConfig.general.font);
    initStartScreen();

    if (!fileSystem->isSdCardInserted())
    {
        hideStartScreen();
        showDisplayNoSdCard();
    }

    display.processDisplayUpdateImmediate();

    engine.setHeadphonesVolume(mtConfig.audioCodecConfig.volume);
    engine.setLineInOutType(mtConfig.general.lineInOutType);
    engine.setPreviewVolume(mtConfig.general.previewVolume);
}

void Interface::processStartScreen()
{
    if (fileSystem->isSdCardInserted())
    {
        refreshStartScreen();

        if (detectProjectLoadState())
        {
            // koniec ekranu startowego
            destroyStartScreen();
            hideDisplayNoSdCard();
            operatingMode = mtOperatingModeRun;
            display.setModulesControlDestroyPoint();
#ifdef TRACKER_MINI
            initSpecialMiniTrackerButtons();
#endif
            activateModule(&patternEditor, 0);
            unmuteAudioOutByKey();

#ifdef TRACKER_MINI
            platform->getBattery()->enableMonitoring();
#endif
        }
    }
    else
    {
        hideStartScreen();
        showDisplayNoSdCard();
        operatingMode = mtOperatingModeOpenProject;
    }
}

void Interface::processPowerOffSequence()
{
    if (lowPower.getLowPowerState() == shutdownStateStart)
    {
        getPlatform()->kickPowerOffGuard(); // the main loop is alive (the guard resets the MCU when these stop)

        refreshDisplayShutdown();

        // A file operation that has ended (an End or an error status) waits for the screen that
        // started it to clear it. Some screens never do (Song, Master, Params: an autosave error
        // stays there), and no screen runs during the shutdown, so the status would block the save
        // below and the power off for good. Nothing reads it from here on. A save below that fails
        // is cleared the same way: the Workspace keeps what the last good save wrote and the unit
        // still powers off. An operation still running is waited for, as before.
        if (newFileManager.getStatus() >= fmLoadEnd &&
            newFileManager.getCurrentOperation() == fmNoOperation)
        {
            newFileManager.clearStatus();
        }

        if (shutdownSaveFlag == 0 && lowPower.getShutdownProgress() > 10 &&
            lowPower.getShutdownProgress() < 70)
        {
            if (newFileManager.saveProjectToWorkspace())
            {
                shutdownSaveFlag = 1;
            }
        }
        if (lowPower.getShutdownProgress() < 15 && !isAudioOutMuted())
        {
            muteAudioOutByKey();
        }

        // Wait for the file manager, but not for ever: a status left in a running value with no operation behind it is given
        // POWER_OFF_IDLE_WAIT, an operation that keeps running POWER_OFF_OPERATION_WAIT. Then the unit goes off anyway (an operation cut
        // there can leave a partly written file, as pulling the power does).
        const bool fmBusy = newFileManager.getStatus() != fmIdle;
        const uint32_t waitLimit = newFileManager.getCurrentOperation() == fmNoOperation ? POWER_OFF_IDLE_WAIT
                                                                                         : POWER_OFF_OPERATION_WAIT;
        if (lowPower.getTimeLeft() == 0 && (!fmBusy || lowPower.getOverrun() >= waitLimit))
        {
            operatingMode = mtOperatingModeSleep;
            lowPower.goLowPower();
        }
    }
}

void Interface::handleCardSlotAction(bool state)
{
    if (operatingMode != mtOperatingModeRun)
    {
        return;
    }

    if (state)
    {
        activateInterface();
    }
    else
    {
        if (specialScreenMode == usbStorageScreenMode)
        {
            ip->unblockAllInputs();
            handleFileTransferMode(0);
        }

        activateNoSdScreen();
    }
}

void Interface::handlePowerButtonAction(uint8_t state) // 1 = przycisk wcisniety
{
    if (state) // press
    {
        if (operatingMode != mtOperatingModeRun && fileSystem->isSdCardInserted())
        {
            return;
        }

        operatingMode = mtOperatingModePowerOffSequence;
        lowPower.startPowerOffSequence();

        if (!fileSystem->isSdCardInserted()) // jeseli wyjeta karta sd
        {
            hideDisplayNoSdCard();

            activateShutdownMode();
        }
        else // normalnie
        {
            activateShutdownMode();
        }
    }
    else // relase
    {
        if (operatingMode != mtOperatingModePowerOffSequence)
            return;

        shutdownSaveFlag = 0;
        lowPower.stopPowerOffSequence();
        operatingMode = mtOperatingModeRun;

        // specjalne zachowanie w zaleznosci od stanu sd
        if (fileSystem->isSdCardInserted())
        {
            activateInterface();
        }
        else
        {
            hideDisplayShutdown();
            showDisplayNoSdCard();
        }
    }
}

void Interface::handleFileTransferMode(uint8_t state)
{
    if (operatingMode != mtOperatingModeRun)
    {
        return;
    }

    if (state)
    {
        // The host is about to own the card: playback must not keep streaming
        // samples from it, and live recording must not leave changes behind
        // for autosave to write to it. The armed state covers the pre-roll.
        if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP || sequencer.isRec())
        {
            sequencer.stop();
        }
        sequencer.stopManualNotes();

        // Also the check that no other file operation is using the card.
        if (!newFileManager.flushWorkspaceSave())
        {
            return;
        }

        activateFileTransferModeScreen();

        // Closing the module above may have queued more to save. After this
        // the device must not use the card until the mode ends: not for
        // saving, and not for streaming the notes that are still sounding or
        // that MIDI input starts.
        updateConfig(true);
        newFileManager.flushWorkspaceSave();
        newFileManager.setWorkspaceSaveSuspended(true);
        voiceStreamManager.setSuspended(true);
        LOGE("USB storage mode on");
    }
    else
    {
        LOGE("USB storage mode off");
#ifdef FEATURE_USB_MSC
        bool reloadWorkspace = false;
#endif
#ifdef FEATURE_PLUG_HOST
        plug::cardChanged(); // the host may have changed /Plugins/Master: the Inserts page reads every file again
#endif

        // The host may have changed the card, so what the filesystem driver
        // and sample streaming remember about it can be wrong. When the mode
        // ends because the card was ejected, there is nothing to mount.
        if (fileSystem->remount())
        {
            voiceStreamManager.rebindAllInstrumentStreamFiles();
#ifdef FEATURE_USB_MSC
            // USB drive (decision 3 of ref/usb-mass-storage-feasibility.md): what the computer wrote to /Workspace wins. The
            // project in RAM was saved when the mode began and could not change since, so it is read back from the card
            // (a new project when /Workspace no longer holds one, as at start).
            reloadWorkspace = true;
#endif
        }
        voiceStreamManager.setSuspended(false);
        newFileManager.setWorkspaceSaveSuspended(false);
        activateInterface();
#ifdef FEATURE_USB_MSC
        if (reloadWorkspace)
        {
            // On the File screen, as when a project is opened there: inputs blocked and the progress popup while it loads, then
            // the pattern editor (cProjectEditor::update clears the end status)
            switchModuleByButton(onScreenModule, interfaceButtonFile, 0);
            openStartupProject();
            ip->blockAllInputs();
        }
#endif
    }

    Usb* usb = mtInterface.getPlatform()->getUsb();
    usb->setClassState(Usb::ClassType::kStorage, state == 1 ? true : false);
    usb->reset();
#ifdef FEATURE_USB_MSC
    if (state && !usb->getClassState(Usb::ClassType::kStorage))
    {
        // The platform did not lend the card (no card, or the drive is off this session): back out, nothing was lent
        LOGE("USB storage mode refused");
        ip->unblockAllInputs();
        voiceStreamManager.setSuspended(false);
        newFileManager.setWorkspaceSaveSuspended(false);
        activateInterface();
    }
#endif
}

bool Interface::setFileTransferMode(bool enable)
{
    bool active = specialScreenMode == usbStorageScreenMode;

    if (enable != active)
    {
        if (!enable)
        {
            ip->unblockAllInputs();
        }

        handleFileTransferMode(enable ? 1 : 0);
    }

    return specialScreenMode == usbStorageScreenMode;
}

void Interface::refreshInterfaceControlsColors()
{
    songTimer.refreshColors();
    mtPopups.refreshColors();
    mtMenuPopup.refreshColors();
    titleBar.refreshColors();
    ModuleBase::keyboardManager.refreshColors();
}

//=======================================================================
//=======================================================================
//=======================================================================
void Interface::activateModule(hModule module, uint32_t options)
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    onScreenModule = module;
    if (module != &patternEditor)
    {
        songTimer.show();
    }
    module->initDisplayControls();

    module->start(options);
    lastOptions = options;
}

void Interface::deactivateModule(hModule module)
{
    if (mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift))
    {
        ip->processButtonsInput(interfaceButtonShift, buttonRelease);
    }

    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    module->stop();
    if (ModuleBase::keyboardManager.getState())
    {
        ModuleBase::keyboardManager.deactivateKeyboard();
    }
    mtPopups.hideStepPopups();
    songTimer.hide();
    titleBar.hide();
    display.resetControlQueue();
    display.destroyModulesControls();

    if (module == onScreenModule)
    {
        onScreenModule = nullptr;
    }

    previousModule = module;
    previousModuleOptions = lastOptions;
}

#ifdef FEATURE_SYNTH_OPEN
// Shift + Perform, on every page except the Synth play page itself, the Performance page (Shift + Perform is its track page flip there)
// and the two that finish work of their own before they let go (the Sample Recorder and the Sample Editor). The press goes to the page as the press of another page key (Pattern, Song on the Pattern page)
// so the page runs its own checks (a popup, unsaved edits, Performance mode, a busy Song page) and then asks for a switch: the
// switch lands in switchModuleByButton, which opens the Synth play page instead of the page of that key. A page that does not switch
// (it refused, or it waits for an answer) leaves the user where they are.
bool Interface::trySynthPlayEntry()
{
    if (operatingMode != mtOperatingModeRun || specialScreenMode != specialScreenModeDisabled || onScreenModule == nullptr ||
        onScreenModule == &synthPlay || onScreenModule == &performanceMode || onScreenModule == &sampleRecorder ||
        onScreenModule == &sampleEditor)
    {
        return false;
    }
    synthPlayEntry = true;
    synthPlay.entryInstrument = mtProject.values.lastUsedInstrument;
    ip->processButtonsInput(onScreenModule == &patternEditor ? interfaceButtonSong : interfaceButtonPattern, buttonPress);
    const bool taken = !synthPlayEntry;
    synthPlayEntry = false;
    return taken;
}

// Perform on page 2 of Performance mode (the third page of the Perform key): the Synth play page opens straight from the page that asks, without the
// Pattern key press of trySynthPlayEntry. The caller keeps its own state (Performance mode keeps its FX and track states, see
// cPerformanceMode::openSynthPlay), a refusal leaves everything as it was
bool Interface::openSynthPlayFrom(hModule module)
{
    if (operatingMode != mtOperatingModeRun || specialScreenMode != specialScreenModeDisabled || onScreenModule != module || module == &synthPlay)
    {
        return false;
    }
    synthPlay.entryInstrument = mtProject.values.lastUsedInstrument;
    synthPlayEntry = true;
    switchModuleByButton(module, interfaceButtonPattern, 0);
    const bool taken = !synthPlayEntry;
    synthPlayEntry = false;
    return taken;
}
#endif

#ifdef FEATURE_PARTS
// Shift + Song: the press goes to the page as a plain Song press, so the page runs its own checks and switches. On the Song screen the
// view switches to Parts (and back to Patterns), with the Song screen's own refusal while something plays
bool Interface::tryPartsEntry()
{
    if (operatingMode != mtOperatingModeRun || specialScreenMode != specialScreenModeDisabled || onScreenModule == nullptr)
    {
        return false;
    }
    if (onScreenModule == &songEditor)
    {
        if (!songEditor.isBusy) songEditor.partsEnterView(!songEditor.partsView);
        return true;
    }
    ip->processButtonsInput(interfaceButtonSong, buttonPress);
    if (onScreenModule != &songEditor) return true;   // the page refused (its popup says why), the release is swallowed all the same
    if (!songEditor.partsView) songEditor.partsEnterView(true);
    return true;
}
#endif

void Interface::switchModuleByButton(hModule module, uint8_t button, uint32_t options)
{
    int8_t index = getButtonIndex(button);
    if (index < 0)
        return;
#ifdef FEATURE_SYNTH_OPEN
    if (synthPlayEntry)
    {
        synthPlayEntry = false;
        deactivateModule(module);
        activateModule(&synthPlay, 0);
        return;
    }
#endif
    if (modules[modulesButtons[index][1]] == module)
        return;

    uint32_t localOptions = options > 0 ? options : modulesButtons[index][2];

#ifdef FEATURE_PARTS
    // Performance mode swaps tracks into the playing pattern, which is a Part image while Parts play (risk R4)
    if (button == interfaceButtonPerformance && parts::playing())
    {
        mtPopups.show(0, "A Part is playing", "stop it to use Performance"); // timed: an info popup would stay up
        return;
    }
#endif

    deactivateModule(module);
    activateModule(modules[modulesButtons[index][1]], localOptions);
}

void Interface::switchModuleToPrevious(hModule module, uint8_t options)
{
    if (previousModule == module)
        return;

    hModule prevModule = previousModule;
    uint32_t prevModuleOptions = previousModuleOptions;

    uint32_t localOptions = options > 0 ? options : prevModuleOptions;

    deactivateModule(module);
    activateModule(prevModule, localOptions);
}

int8_t Interface::getButtonIndex(uint8_t button)
{
    for (uint8_t i = 0; i < modulesButtonsCount; i++)
    {
        if (button == modulesButtons[i][0])
        {
            return i;
        }
    }
    return -1;
}

void Interface::toggleActiveModule(uint8_t state)
{
    if (state)
    {
        if (toggledState && onScreenModule == nullptr)
        {
            toggledState = 0;

            if (previousModule != nullptr)
            {
                hModule prevModule = previousModule;
                uint32_t prevModuleOptions = previousModuleOptions;

                activateModule(prevModule, prevModuleOptions);

                // Hiding the module recorded it as its own predecessor. Put back the one
                // it had, or it can no longer switch back to the previous module.
                previousModule = moduleBeforeToggle;
                previousModuleOptions = moduleBeforeToggleOptions;
                return;
            }
        }
    }
    else
    {
        if (onScreenModule != nullptr)
        {
            moduleBeforeToggle = previousModule;
            moduleBeforeToggleOptions = previousModuleOptions;

            mtInterface.deactivateModule(onScreenModule);
            toggledState = 1;
        }
    }
}

bool Interface::isSongScreenActive()
{
    return isOnScreenModule(&songEditor);
}

#include DEVHOOK_DEFS(interface) // development hooks (devhooks.h): Interface::devPageIndex

bool Interface::isPatternScreenActive()
{
    return isOnScreenModule(&patternEditor);
}

bool Interface::isPerformanceModeScreenActive()
{
    return isOnScreenModule(&performanceMode);
}

bool Interface::isMasterScreenActive()
{
    return isOnScreenModule(&masterParams);
}

bool Interface::isInstrumentParamsScreenActive()
{
    return isOnScreenModule(&instrumentEditor);
}

bool Interface::isSamplePlayerScreenActive()
{
    return isOnScreenModule(&samplePlayback);
}

bool Interface::isOnScreenModule(ModuleBase* module)
{
    return onScreenModule == module;
}

//=======================================================================
//=======================================================================
//=======================================================================

void interfaceEnvents(uint8_t event, void* param1, void* param2, void* param3)
{
    switch (event)
    {
        case eventSwitchModule:
        {
            // param1 = uchwyt do modułu, param2 = index przycisku
            mtInterface.switchModuleByButton(
              (hModule)param1, *((uint8_t*)param2), param3 == nullptr ? 0 : *((uint32_t*)param3));
            break;
        }
        case eventSwitchToPreviousModule:
        {
            // param1 = handle to source module, param2 = start options
            mtInterface.switchModuleToPrevious((hModule)param1,
                                               param2 == nullptr ? 0 : *((uint8_t*)param2));
            break;
        }
        case eventActivateGameModule:
        {
            break;
        }
        case eventActivateImageViewer:
        {
            mtInterface.deactivateModule((hModule)param1);
            mtInterface.activateModule(&imageViewer, param2 == nullptr ? 0 : *((uint8_t*)param2));
            break;
        }
        case eventToggleActiveModule:
        {
            mtInterface.toggleActiveModule(*((uint8_t*)param3));
            break;
        }
        case eventActivateTestingProcedure:
        {
            mtInterface.deactivateModule((hModule)param1);
            mtTest.runTestingProcedure(mtInterface.ip);
            break;
        }
        case eventSetFileTransferMode:
        {
            mtInterface.handleFileTransferMode(*((uint8_t*)param1));
            break;
        }
        default: break;
    }
}
