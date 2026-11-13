#include "Platform.h"
#ifdef FEATURE_EXT_HOOKS
#include "features.h"
#endif
#include "Profiler.h"
#include "configEditor.h"
#include "configEditor/configEditor.h"
#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"
#include "configMenu.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtGridEditor.h"
#include "mtHardware.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtTest.h"
#include "patternEditor/patternEditor.h"

cConfigEditor configEditor;
static cConfigEditor* CE = &configEditor;

extern strMtProject mtProject;

#ifdef GRID_EDITOR_ENABLED
// GRID SCREEN
static void functSaveGridScreen();
static void functCancelGridScreen();
static void functConfirmGridScreen();
static void functUpGridScreen();
static void functDownGridScreen();
static void functLeftGridScreen();
static void functRightGridScreen();
static void functActionButtonGridScreen(uint8_t button, uint8_t state);
static void functPadsGridScreen(uint8_t pad, uint8_t state, int16_t velo);
static void functEncoderGridScreen(int16_t value);

// PAD SCREEN
static void functConfirmPadScreen();
static void functUpPadScreen();
static void functDownPadScreen();
static void functLeftPadScreen();
static void functRightPadScreen();
static void functPadsPadScreen(uint8_t pad, uint8_t state, int16_t velo);
static void functActionButtonPadScreen(uint8_t button, uint8_t state);
static void functEncoderPadScreen(int16_t value);
#endif

//////////////
static void functPlayAction();
static void functRecAction();

static void functLeft();
static void functRight();
static void functUp();
static void functDown();

static void functActionButton(uint8_t button, uint8_t state);

static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMenu(uint8_t state);

// config

static void functEncoder(int16_t value);

static void functSwitchModule(uint8_t button);

static void functCloseCreditsButton();

static void functMiniMiddle();

//##########################################################################################
void cConfigEditor::update()
{
    uint8_t managerStatus = newFileManager.getStatus();

    if (managerStatus == fmBrowseFirmwaresEnd)
    {
        firmwareFoundNum = newFileManager.getFirmwaresList(&ptrfirmwareNamesList);
        CE->flashingState = 1;
        CE->changeLabelText(6, "Cancel");
        CE->changeLabelText(7, "Select");
        CE->showConfigList4(3, 0, firmwareFoundNum, ptrfirmwareNamesList);
        // CE->selectConfigList(3);
        newFileManager.clearStatus();
    }
    else if (managerStatus >= fmError)
    {
        newFileManager.clearStatus();
        // setDefaultScreenFunct();
    }

    if (processUpdate)
    {
        processUpdate = 0;
        CE->platform->runBootloader(CE->ptrfirmwareNamesList[CE->selectedConfigListPosition]);
    }

    if (interfaceGlobals.devMode && infoDataRefresh > 1000)
    {
        infoDataRefresh = 0;
        showBatteryInfo();
        showDiagnosticInfo();
    }
}
extern cMenuGroup menuMetro;
void cConfigEditor::start(uint32_t options)
{
    moduleRefresh = 1;

    exitOnButtonRelease = 0;

    // ustawienie funkcji
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    // ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);

    // ip->setButtonObj(interfaceButtonConfig, functSwitchModeConfig);
    // ip->setButtonObj(interfaceButtonMaster, functSwitchModeMaster);

    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    // powraca tylko do 1 i 2 kolumny
    if (selectedPlace > 1)
        selectedPlace = 1;

    ThemeManager::scanThemesAndFonts();
    refreshThemeAndFontMenuOptions();

    showDefaultConfigScreen();
    setConfigScreenFunct();

    //--------------------------------------------------------------------

    createConfigMenu();

    //--------------------------------------------------------------------

    activateLabelsBorder();

    if (options == kOpenMetronomeConfig)
    {
        int moveDelta = menuMetro.getSlot() - basemenuList.start;

        changeMenuListPosition(0, moveDelta);
        if (CE->selectedPlace == 0 && CE->submenuShown)
        {
            CE->selectedPlace = 1;
            CE->refreshListsColors();
        }
    }
}

void cConfigEditor::stop()
{
    updateConfig(true);

    songTimer.show();

    // czyszczenie dynamicznie zaalokowanej pamieci na liste
    if (ptrTempValuesList != nullptr)
    {
        for (uint16_t i = 0; i < lastTempListLength; i++)
        {
            delete[] ptrTempValuesList[i];
        }

        delete[] ptrTempValuesList;
        ptrTempValuesList = nullptr;
    }

    exitOnButtonRelease = 0;
    moduleRefresh = 0;
}

void cConfigEditor::setConfigScreenFunct()
{
    // funkcje
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRecAction);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);

    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functMiniMiddle);

    //	ip->setButtonObj(interfaceButtonConfig, functSwitchModeConfig);
    //	ip->setButtonObj(interfaceButtonMaster, functSwitchModeMaster);

    ip->setPotObj(interfacePot0, functEncoder);

    ip->setButtonObj(interfaceButton0, functActionButton);
    ip->setButtonObj(interfaceButton1, functActionButton);
    ip->setButtonObj(interfaceButton2, functActionButton);
    ip->setButtonObj(interfaceButton3, functActionButton);
    ip->setButtonObj(interfaceButton4, functActionButton);
    ip->setButtonObj(interfaceButton5, functActionButton);
    ip->setButtonObj(interfaceButton6, functActionButton);
    ip->setButtonObj(interfaceButton7, functActionButton);

    ip->setPadsGlobal(functPads);
}
#ifdef GRID_EDITOR_ENABLED
void cConfigEditor::setGridScreenFunction()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);
    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearGlobalPads();
    ip->clearAllPots();

    ip->setButtonObj(interfaceButton0, functActionButtonGridScreen);
    ip->setButtonObj(interfaceButton1, functActionButtonGridScreen);
    ip->setButtonObj(interfaceButton2, functActionButtonGridScreen);

    ip->setButtonObj(interfaceButton5, buttonPress, functSaveGridScreen);
    ip->setButtonObj(interfaceButton6, buttonPress, functCancelGridScreen);
    ip->setButtonObj(interfaceButton7, buttonPress, functConfirmGridScreen);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeftGridScreen);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRightGridScreen);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUpGridScreen);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDownGridScreen);

    ip->setPotObj(interfacePot0, functEncoderGridScreen, -1);

    ip->setPadsGlobal(functPadsGridScreen);
}

void cConfigEditor::setPadScreenFunction()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);
    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearGlobalPads();
    ip->clearAllPots();

    ip->setButtonObj(interfaceButton0, functActionButtonPadScreen);
    ip->setButtonObj(interfaceButton1, functActionButtonPadScreen);
    ip->setButtonObj(interfaceButton2, functActionButtonPadScreen);

    ip->setButtonObj(interfaceButton7, buttonPress, functConfirmPadScreen);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeftPadScreen);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRightPadScreen);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUpPadScreen);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDownPadScreen);

    ip->setPotObj(interfacePot0, functEncoderPadScreen, -1);

    ip->setPadsGlobal(functPadsPadScreen);
}

void cConfigEditor::reloadPadScreenDisplayedValue(uint8_t value)
{
    switch (value)
    {
        case 0:
            padScreenDisplayedValue[value] =
              map(mtGrid.pad[gridEditor.getSelectedPad()].note, MIN_NOTE, MAX_NOTE, 0, 100);
            interfaceGlobals.padNamesPointer[gridEditor.getSelectedPad()] =
              (char*)mtNotes[mtGrid.pad[gridEditor.getSelectedPad()].note];
            break;
        case 1:
            padScreenDisplayedValue[value] = mtGrid.pad[gridEditor.getSelectedPad()].microtune;
            sprintf(interfaceGlobals.padFinetuneNames[gridEditor.getSelectedPad()],
                    "%d",
                    mtGrid.pad[gridEditor.getSelectedPad()].microtune);
            break;
        case 2:
            padScreenDisplayedValue[value] =
              mtGrid.pad[gridEditor.getSelectedPad()].ledEnable ? 0 : 1;
            break;
        default: break;
    }
}
#endif
//##############################################################################################
//###############################        ACTION BUTTONS        #################################
//##############################################################################################
static void functActionButton(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        switch (button)
        {
            case 0:
            {
                if (CE->selectedPlace == 0)
                {
                    CE->changeMenuListPosition(0, -1);
                }
                else
                {
                    CE->selectedPlace = 0;
                }
                break;
            }
            case 1:
            {
                if (CE->selectedPlace == 0)
                {
                    CE->changeMenuListPosition(0, 1);
                }
                else
                {
                    CE->selectedPlace = 0;
                }
                break;
            }
            case 2:
            {
                if (CE->selectedPlace == 1)
                {
                    if (CE->itemEditorShown)
                    {
                        CE->itemEditorChangeValue(-1);
                    }
                    else
                    {
                        CE->changeMenuListPosition(1, -1, 1);
                    }
                }
                else
                {
                    CE->selectedPlace = 1;
                }
                break;
            }
            case 3:
            {
                if (CE->selectedPlace == 0)
                {

                    CE->selectedPlace = 1;
                }
                break;
            }
            case 4:
            {
                if (CE->selectedPlace == 0)
                {

                    CE->selectedPlace = 1;
                }
                break;
            }
            case 5:
            {
                if (CE->selectedPlace == 1)
                {
                    if (CE->itemEditorShown)
                    {
                        CE->itemEditorChangeValue(1);
                    }
                    else
                    {
                        CE->changeMenuListPosition(1, 1, 1);
                    }
                }
                else
                {
                    CE->selectedPlace = 1;
                }
                break;
            }
            case 6:
            {
                if (CE->updatePopupShown)
                {
                    CE->updateFirmware(false);
                }
                else if (CE->resetConfigPopupShown)
                {
                    CE->resetTrackerConfig(false);
                }
                else if (CE->selectedPlace == 1)
                {
                    if (CE->itemEditorShown)
                    {
                        CE->itemEditorClose();
                    }
                    else
                    {
                        CE->menuGoOut();
                    }
                }
                else
                {
                    // CE->selectedPlace = 1;
                }
                break;
            }
            case 7:
            {
                if (CE->updatePopupShown)
                {
                    CE->updateFirmware(true);
                }
                else if (CE->resetConfigPopupShown)
                {
                    CE->resetTrackerConfig(true);
                }
                else if (CE->selectedPlace == 1)
                {
                    if (CE->flashingState == 1)
                    {
                        if (CE->firmwareFoundNum == 0)
                        {
                            return;
                        }
                        CE->ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);
                        CE->showFirmwareUpdatePopout();
                    }
                    else if (CE->itemEditorShown)
                    {
                        CE->itemEditorApply();
                    }
                    else
                    {
                        CE->menuGoIn();
                    }
                }
                else
                {
                    CE->selectedPlace = 1;
                }
                break;
            }

            default: break;
        }

        if (mtTest.runTestByMiniCombinaion(button))
        {
            CE->eventFunct(eventActivateTestingProcedure, CE, 0, 0);
        }
    }
    else if (state == buttonRelease)
    {
    }

    CE->activateLabelsBorder();
    CE->refreshListsColors();
}

//==============================================================================================================

static void functPads(uint8_t pad, uint8_t state, int16_t)
{
    if (CE->updatePopupShown)
    {
        return;
    }
    if (state != buttonPress)
    {
        return;
    }

    if (mtTest.runTestByCombinaion(pad))
    {
        CE->eventFunct(eventActivateTestingProcedure, CE, 0, 0);
    }
}

static void functEncoder(int16_t value)
{
    if (CE->updatePopupShown)
    {
        return;
    }

    switch (CE->selectedPlace)
    {
        case 0:
        {
            value = clamp<int>(value, -3, 3);
            CE->changeMenuListPosition(0, interfaceGlobals.invertValForList ? -value : value);
            break;
        }
        case 1:
        {
            if (CE->configListShown)
            {
                CE->changeConfigListPosition(interfaceGlobals.invertValForList ? -value : value);
            }
            else
            {
                value = clamp<int>(value, -3, 3);
                CE->changeMenuListPosition(1, interfaceGlobals.invertValForList ? -value : value);
            }
            break;
        }
        case 2:
        {
            break;
        }
        case 3:
        {
            break;
        }

        default: break;
    }
}

static void functLeft()
{
    if (CE->updatePopupShown)
    {
        return;
    }

    if (CE->itemEditorShown)
    {
        CE->itemEditorClose();
        {
            return;
        }
    }
    else if (CE->menuGoOut())
    {
        CE->selectedPlace = 1;
        return;
    }

    if (CE->selectedPlace > 0)
    {
        CE->selectedPlace--;
        CE->refreshListsColors();
    }

    CE->activateLabelsBorder();
}

static void functRight()
{
    if (CE->updatePopupShown)
    {
        return;
    }

    if (CE->itemEditorShown)
    {
    }
    else if (CE->selectedPlace == 0 && CE->submenuShown)
    {
        CE->selectedPlace = 1;
        CE->refreshListsColors();
    }
    else if (CE->selectedPlace == 1)
    {
        CE->menuGoIn();
    }

    CE->activateLabelsBorder();
}

static void functUp()
{
    if (CE->updatePopupShown)
    {
        return;
    }

    switch (CE->selectedPlace)
    {
        case 0:
        {
            CE->changeMenuListPosition(0, -1);
            break;
        }
        case 1:
        {
            if (CE->configListShown)
                CE->changeConfigListPosition(-1);
            else
                CE->changeMenuListPosition(1, -1);
            break;
        }

        default: break;
    }
}

static void functDown()
{
    if (CE->updatePopupShown)
    {
        return;
    }

    switch (CE->selectedPlace)
    {
        case 0:
        {
            CE->changeMenuListPosition(0, 1);
            break;
        }
        case 1:
        {
            if (CE->configListShown)
                CE->changeConfigListPosition(1);
            else
                CE->changeMenuListPosition(1, 1);
            break;
        }

        default: break;
    }
}
#ifdef GRID_EDITOR_ENABLED
////////////////////////////////////////////////
//              GRID SCREEN
static void functSaveGridScreen()
{
    return 1;
}

static void functCancelGridScreen()
{
    gridEditor.close();
    CE->setConfigScreenFunct();

    CE->hideGridScreen();
    display.setControlShow(CE->configBasemenuListControl);
    display.refreshControl(CE->configBasemenuListControl);
    display.setControlShow(CE->configSubmenuListControl);
    display.refreshControl(CE->configSubmenuListControl);
    CE->showDefaultConfigScreen();
    padsBacklight.clearGlobalPads(0, 1, 0);
    return 1;
}
static void functConfirmGridScreen()
{
    gridEditor.confirmSelectedPad();
    CE->setPadScreenFunction();

    CE->hideGridScreen();
    CE->showPadScreen();
    return 1;
}
static void functUpGridScreen()
{
    gridEditor.changeSelectedPad(mtGridEditor::enChangePadDirectionUp);
    CE->refreshGridScreen();
    return 1;
}
static void functDownGridScreen()
{
    gridEditor.changeSelectedPad(mtGridEditor::enChangePadDirectionDown);
    CE->refreshGridScreen();
    return 1;
}
static void functLeftGridScreen()
{
    gridEditor.changeSelectedPad(mtGridEditor::enChangePadDirectionLeft);
    CE->refreshGridScreen();
    return 1;
}
static void functRightGridScreen()
{
    gridEditor.changeSelectedPad(mtGridEditor::enChangePadDirectionRight);
    CE->refreshGridScreen();
    return 1;
}
static void functActionButtonGridScreen(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        CE->selectedPlaceGridScreen = button;
        CE->refreshGridScreenFrame();
    }

    return 1;
}
static void functPadsGridScreen(uint8_t pad, uint8_t state, int16_t velo)
{
    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
        gridEditor.changeSelectedPad(pad);
        CE->refreshGridScreen();
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
    }

    return 1;
}

static void functEncoderGridScreen(int16_t value)
{

    switch (CE->selectedPlaceGridScreen)
    {
        case 0: gridEditor.changeNote(value); break;
        case 1: gridEditor.changeMicrotune(value); break;
        case 2: gridEditor.changeLedEnable(-value); break;
        default: break;
    }
    CE->reloadPadScreenDisplayedValue(CE->selectedPlaceGridScreen);
    CE->refreshPadValue(CE->selectedPlaceGridScreen, enScreenType::screenTypeGridScreen);
    display.setControlValue(CE->gridPadsControl, gridEditor.getSelectedPad());
    display.refreshControl(CE->gridPadsControl);

    return 1;
}
//////////////////////////////////////////////////////////
// PAD SCREEN
static void functConfirmPadScreen()
{
    CE->setGridScreenFunction();
    CE->hidePadScreen();
    CE->showGridScreen();
    gridEditor.cancelSelectedPad();
    return 1;
}
static void functUpPadScreen()
{
    switch (CE->selectedPlacePadScreen)
    {
        case 0: gridEditor.changeNote(1); break;
        case 1: gridEditor.changeMicrotune(1); break;
        case 2: gridEditor.changeLedEnable(1); break;
        default: break;
    }
    CE->reloadPadScreenDisplayedValue(CE->selectedPlacePadScreen);
    CE->refreshPadValue(CE->selectedPlacePadScreen, enScreenType::screenTypePadScreen);
    return 1;
}
static void functDownPadScreen()
{
    switch (CE->selectedPlacePadScreen)
    {
        case 0: gridEditor.changeNote(-1); break;
        case 1: gridEditor.changeMicrotune(-1); break;
        case 2: gridEditor.changeLedEnable(-1); break;
        default: break;
    }
    CE->reloadPadScreenDisplayedValue(CE->selectedPlacePadScreen);
    CE->refreshPadValue(CE->selectedPlacePadScreen, enScreenType::screenTypePadScreen);
    return 1;
}
static void functLeftPadScreen()
{
    if (CE->selectedPlacePadScreen > 0)
        CE->selectedPlacePadScreen--;
    CE->refreshPadScreenFrame();
    return 1;
}
static void functRightPadScreen()
{
    if (CE->selectedPlacePadScreen < 2)
        CE->selectedPlacePadScreen++;
    CE->refreshPadScreenFrame();
    return 1;
}
static void functPadsPadScreen(uint8_t pad, uint8_t state, int16_t velo)
{
    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
        gridEditor.changeSelectedPad(pad);
        for (uint8_t i = 0; i < 3; i++)
        {
            CE->reloadPadScreenDisplayedValue(i);
            CE->refreshPadValue(i, enScreenType::screenTypePadScreen);
        }
        sprintf(CE->padScreenTitleLabelName, "Grid Editor Pad %d", gridEditor.getSelectedPad() + 1);

        display.setControlText(CE->titleLabel, CE->padScreenTitleLabelName);
        display.refreshControl(CE->titleLabel);
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
    }

    return 1;
}
static void functActionButtonPadScreen(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        CE->selectedPlacePadScreen = button;
    }
    CE->refreshPadScreenFrame();
    return 1;
}
static void functEncoderPadScreen(int16_t value)
{
    switch (CE->selectedPlacePadScreen)
    {
        case 0: gridEditor.changeNote(value); break;
        case 1: gridEditor.changeMicrotune(value); break;
        case 2: gridEditor.changeLedEnable(-value); break;
        default: break;
    }
    CE->reloadPadScreenDisplayedValue(CE->selectedPlacePadScreen);
    CE->refreshPadValue(CE->selectedPlacePadScreen, enScreenType::screenTypePadScreen);
    return 1;
}
//////////////

#endif
static void functPlayAction()
{
    if (CE->updatePopupShown)
    {
        return;
    }

    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (CE->buttons->isButtonPressed(interfaceButtonShift))
        {
            sequencer.playSong();
        }
        else
        {
            sequencer.playPattern();
        }
    }
    else
    {
        sequencer.stop();
    }

    Profiler::getInstance()->resetMeasuringAudio();
}

static void functRecAction()
{
    if (CE->updatePopupShown)
    {
        return;
    }
    ////////////////////////TEST GRID EDITOR//////////////////////

    //	CE->setGridScreenFunction();
    //	CE->showGridScreen();
    //	gridEditor.open();

    ////////////////////////TEST GRID EDITOR//////////////////////
}

static void functSwitchModule(uint8_t button)
{
    if (CE->updatePopupShown)
    {
        return;
    }

    if (CE->itemEditorShown)
    {
        CE->itemEditorClose();
    }

    CE->eventFunct(eventSwitchModule, CE, &button, 0);
}

//======================================================================================================================

void firmwareUpgradeActivate()
{
    CE->itemEditorOpen(menuTypeItemActionButton, "Firmware update", firmUpdDesc, nullptr);

    newFileManager.browseFirmwares();
}

void cConfigEditor::changeConfigListPosition(int16_t value)
{
    value = clamp<int>(value, -3, 3);

    if (selectedConfigListPosition + value < 0)
        selectedConfigListPosition = 0;
    else if (selectedConfigListPosition + value > configList.length - 1)
        selectedConfigListPosition = configList.length - 1;
    else
        selectedConfigListPosition += value;

    display.setControlValue(configListControl, selectedConfigListPosition);
    display.refreshControl(configListControl);
}

void cConfigEditor::updateFirmware(bool yes)
{
    if (yes)
    {
        if (firmwareFoundNum == 0)
        {
            return;
        }
        if (selectedConfigListPosition > firmwareFoundNum)
        {
            return;
        }

        processUpdate = 1;
    }

    CE->hideConfigList();

    CE->itemEditorShown = 0;

    CE->updatePopupShown = 0;

    CE->ip->unblockAllInputs();

    CE->start(0);
}

//==============================================================================================================
// reset config

void cConfigEditor::resetTrackerConfig(bool yes)
{

    if (yes)
    {
        resetConfig();
        checkConfig();
        saveConfigAsap();
        restartConfigAllActions();
#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(TRACKER_MINI)
        features::resetAll(); // decision D5: the features go back to their defaults too, /Features/<id>.cfg removed
#endif
    }

    CE->hidePopout();

    refreshConfigMenu();

    CE->ip->unblockAllInputs();

    resetConfigPopupShown = 0;
}

//==============================================================================================================
// credits

void cConfigEditor::openCredits()
{
    // funkcje
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);

    ip->setButtonObj(interfaceButton7, buttonPress, functCloseCreditsButton);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functCloseCreditsButton);

    ip->setPadsGlobal(functPads);

    songTimer.hide();

    hideConfigMenu();
    showCreditsControls();
}

void cConfigEditor::closeCredits()
{

    songTimer.show();

    hideCreditsControls();
    start(0);
}

static void functCloseCreditsButton()
{
    CE->closeCredits();
}

void openCreditsAction()
{
    CE->openCredits();
}

void startFileTransferMode()
{
    uint8_t state = 1;
    CE->eventFunct(eventSetFileTransferMode, &state, 0, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(CE);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(CE);
    }
}

static void functMiniMiddle()
{
    functActionButton(7, buttonPress);
}

//==============================================================================================================

//==============================================================================================================

//==============================================================================================================

void cConfigEditor::saveConfigToEeprom()
{
    saveConfig();
}

//==============================================================================================================

//==============================================================================================================

//==============================================================================================================

void openManual1Action()
{
    uint8_t folderIndex = 0;
    CE->eventFunct(eventActivateImageViewer, CE, &folderIndex, 0);
}

void openManual2Action()
{
    uint8_t folderIndex = 1;
    CE->eventFunct(eventActivateImageViewer, CE, &folderIndex, 0);
}

void openManual3Action()
{
    uint8_t folderIndex = 2;
    CE->eventFunct(eventActivateImageViewer, CE, &folderIndex, 0);
}

