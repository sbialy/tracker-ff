#include "Platform.h"
#include "Profiler.h"
#include "configEditor.h"
#include "mtGridEditor.h"
#include "mtPadsBacklight.h"

static const uint16_t framesPlacesConfig[4][4] = {
    { (800 / 8) * 0 + 1, 31, 800 / 4 - 3, 391 },
    { (800 / 8) * 2 + 1, 31, 800 / 2 - 3, 391 },
    { (800 / 8) * 5 + 1, 31, 800 / 4 - 3, 391 },
    { (800 / 8) * 5 + 1, 31, 600 / 2 - 3, 391 },
};

static const uint16_t framesPlacesConfigPadScreen[3][4] = {
    { (800 / 8) * 0 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 1 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 2 + 1, 31, 800 / 8 - 3, 391 }
};

static const uint16_t framesPlacesConfigGridScreen[3][4] = {
    { (800 / 8) * 0 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 1 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 2 + 1, 424, 800 / 8 - 3, 55 }
};

const char creditsText[] = {
    "Polyend Tracker Community Edition (CE)"
    "\nLicensed under the MIT License"
    "\n(No Wholesale Commercial Clones; see LICENSE)."
#ifdef FEATURE_SYNTH_OPEN
    "\n"
    "\ntracker-ff 1.2.0: unofficial community firmware,"
    "\nnot affiliated with Polyend."
    "\nSynth engines: OpenTracker (Apache-2.0),"
    "\ngithub.com/sandlbn/polyend-tracker-re"
    "\nand opentracker-synths (MIT),"
    "\ngithub.com/opentracker-fw/opentracker-synths"
#endif
    "\n"
    "\nAll provided demo projects and samples are under a CC BY-NC 4.0 license."
    "\n"
    "\nMore licenses can be found at github.com/polyend/tracker-ce"
    "\nand polyend.com/licenses"
    "\n\n\n\n\n\n\n\n\n\n\n\n\n"
    "Sandroid was here ;)"
};

void cConfigEditor::initDisplayControls()
{
#ifdef GRID_EDITOR_ENABLED
    strControlProperties gridNameLabelProp;
    gridNameLabelProp.style =
      (controlStyleHide | controlStyleRightX | controlStyleCenterY | controlStyleFont4);
    gridNameLabelProp.x = 790;
    gridNameLabelProp.y = 13;
    gridNameLabel = display.createControl<cLabel>(&gridNameLabelProp);
#endif

    strControlProperties prop9;
    prop9.x = 400;
    prop9.colors = display.getTheme().configConfirmPopUpLabelColors;
    prop9.y = 320;
    prop9.h = 100;
    prop9.w = 800 - 10;
    prop9.style =
      (controlStyleBackground | controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
    prop9.text = (char*)"";
    popoutWindowLabel = display.createControl<cLabel>(&prop9);

    strControlProperties prop3;
    prop3.style = 0;
    prop3.value = 0;
    prop3.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop3);

    labelArrow[0].bitmaps[0].bitmapIndex = displayArrowU;
    labelArrow[0].bitmaps[0].xValue = (800 / 8) * 0 + (800 / 16) - 30;
    labelArrow[0].bitmaps[0].yValue = 450;
    labelArrow[0].bitmaps[1].bitmapIndex = displayArrowD;
    labelArrow[0].bitmaps[1].xValue = (800 / 8) * 1 + (800 / 16) + 30;
    labelArrow[0].bitmaps[1].yValue = 450;

    labelArrow[1].bitmaps[0].bitmapIndex = displayArrowU;
    labelArrow[1].bitmaps[0].xValue = (800 / 8) * 2 + (800 / 16);
    labelArrow[1].bitmaps[0].yValue = 450;
    labelArrow[1].bitmaps[1].bitmapIndex = displayArrowD;
    labelArrow[1].bitmaps[1].xValue = (800 / 8) * 5 + (800 / 16);
    labelArrow[1].bitmaps[1].yValue = 450;

    strControlProperties prop2;
    prop2.value = 0;
    prop2.colors = display.getTheme().activeLabelsColors;
    prop2.w = 800 / 8 - 6;
    prop2.y = 424;
    prop2.h = 55;

    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.x = (800 / 8) * i + ((i == 0 || i == 3) ? (800 / 8) : (800 / 16));
        prop2.data = nullptr;
        prop2.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);

        if (i == 0)
        {
            prop2.style |= controlStyleShowBitmap;
            prop2.data = &labelArrow[0];
        }
        if (i == 3)
        {
            prop2.style |= controlStyleShowBitmap;
            prop2.data = &labelArrow[1];
        }

        label[i] = display.createControl<cLabel>(&prop2);
    }

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.data = nullptr;
    prop2.value = 255; // 36;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    prop2.value = 0;
    prop2.style = controlStyleBackground | controlStyleFont1 |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 0 + 1;
    prop2.y = 29;
    prop2.w = 800 / 4 - 3;
    prop2.h = 394;
    prop2.data = &basemenuList;
    configBasemenuListControl = display.createControl<cList>(&prop2);

    prop2.style = controlStyleFont1 | controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 2 + 1;
    prop2.y = 29;
    prop2.w = 800 / 2 - 3;
    prop2.h = 394;
    prop2.data = &submenuList;
    configSubmenuListControl = display.createControl<cParamValueList>(&prop2);

    prop2.style = controlStyleFont1 | controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 5 + 1;
    prop2.y = 29;
    prop2.w = 800 / 4 - 3;
    prop2.h = 394;
    configListControl = display.createControl<cList>(&prop2);

    prop2.data = nullptr;
    prop2.colors = display.getTheme().configItemEditorTextLabelColors;
    prop2.style = (controlStyleFont3);
    prop2.x = (800 / 8) * 2 + 15;
    prop2.w = 800 / 4 - 18;
    prop2.y = 27;
    prop2.h = 55;
    configLabel = display.createControl<cLabel>(&prop2);

    prop2.style = controlStyleFont5;
    prop2.colors = display.getTheme().configItemEditorTextLabelColors;
    prop2.value = 3;
    prop2.x = 0;
    prop2.y = 30;
    prop2.w = 780;
    prop2.h = 450;
    prop2.data = nullptr; //&textBoxData;
    creditsTextBox = display.createControl<cTextBox>(&prop2);

    prop2.style = controlStyleFont5;
    prop2.colors = display.getTheme().configItemEditorTextLabelColors;
    prop2.value = 3;
    prop2.x = (800 / 8) * 2 + 10;
    prop2.w = 800 / 4 - 15;
    prop2.y = 60;
    prop2.h = 365;
    prop2.data = nullptr; //&textBoxData;
    descriptionTextBox = display.createControl<cTextBox>(&prop2);

#ifdef GRID_EDITOR_ENABLED
    strControlProperties gridPadsProp;

    for (uint8_t i = 0; i < 48; i++)
    {
        interfaceGlobals.padNamesPointer[i] = (char*)mtNotes[mtGrid.pad[i].note];
        sprintf(interfaceGlobals.padFinetuneNames[i], "%d", mtGrid.pad[i].microtune);
    }

    padNamesStruct.length = 5;
    padNamesStruct.noteName = interfaceGlobals.padNamesPointer;
    padNamesStruct.finetuneName = interfaceGlobals.padFinetuneNames;

    gridPadsProp.style = 0;
    gridPadsProp.x = 13;
    gridPadsProp.y = 130;
    gridPadsProp.w = 780;
    gridPadsProp.h = 260;
    gridPadsProp.colors = nullptr;
    gridPadsProp.value = gridEditor.getSelectedPad();
    gridPadsProp.data = &padNamesStruct;
    gridPadsControl = display.createControl<cGridEditorPopup>(&gridPadsProp);

    strControlProperties gridPadBarProp;
    gridPadBarProp.x = (800 / 8) * 0 + 1;
    gridPadBarProp.y = 29;
    gridPadBarProp.w = 800 / 8 - 3;
    gridPadBarProp.style = controlStyleValue_0_100 | controlStyleBackground;
    gridPadBarProp.h = 394;
    padBar[0] = display.createControl<cBar>(&gridPadBarProp);

    gridPadBarProp.x = (800 / 8) * 1 + 1;
    gridPadBarProp.y = 29;
    gridPadBarProp.w = 800 / 8 - 3;
    gridPadBarProp.style = controlStyleValueLeftRight_100_100 | controlStyleBackground;
    gridPadBarProp.h = 394;
    padBar[1] = display.createControl<cBar>(&gridPadBarProp);

//	strControlProperties gridPadListProp;
//	padListData.linesCount = 5;
//	padListData.start = 0;
//	padListData.length = 2;
//	padListData.data = (char**)padListNames;
//	gridPadListProp.style = controlStyleBackground;
//	gridPadListProp.x = (800/8)*(2)+1;
//	gridPadListProp.y = 29;
//	gridPadListProp.w = 800/8-3;
//	gridPadListProp.h = 394;
//	gridPadListProp.data = &padListData;
//	padList = display.createControl<cList>(&gridPadListProp);
#endif

#ifdef TRACKER_MINI
    prop2.x = 615;
    prop2.y = 30;
    prop2.w = 100;
    prop2.h = 60;
    prop2.value = 1;
    prop2.colors = display.getTheme().activeButtonLabelsColors;
    prop2.style = controlStyleFont3;
    batteryLabelControl = display.createControl<cLabel>(&prop2);
#endif

#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
    prop2.x = 615;
    prop2.y = 240;
    prop2.w = 100;
    prop2.h = 60;
    prop2.value = 1;
    prop2.colors = display.getTheme().activeButtonLabelsColors;
    prop2.style = controlStyleFont3;
    audioPerformanceLabelControl = display.createControl<cLabel>(&prop2);
#endif
#ifdef MEMORY_USAGE_INFO_ENABLED
    prop2.x = 615;
    prop2.y = 300;
    prop2.w = 100;
    prop2.h = 60;
    prop2.value = 1;
    prop2.colors = display.getTheme().activeButtonLabelsColors;
    prop2.style = controlStyleFont3;
    memoryUsageLabelControl = display.createControl<cLabel>(&prop2);
#endif

    prop2.value = 0;
    prop2.data = nullptr;
    prop2.colors = display.getTheme().blankSpaceCoverLabelColors;
    prop2.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 6 + 1;
    prop2.w = 800 / 4 - 2;
    prop2.y = 29;
    prop2.h = 394;
    blankSpaceCover[0] = display.createControl<cLabel>(&prop2);
    prop2.x = (800 / 8) * 2 + 1;
    prop2.w = 800 / 4 - 2;
    blankSpaceCover[1] = display.createControl<cLabel>(&prop2);
}

void cConfigEditor::showDefaultConfigScreen()
{
    display.setControlHide(gridPadsControl);

    display.setControlStyle(
      label[0],
      (controlStyleCenterX | controlStyleFont3 | controlStyleCenterY | controlStyleShowBitmap));
    display.setControlStyle(
      label[3],
      (controlStyleCenterX | controlStyleFont3 | controlStyleCenterY | controlStyleShowBitmap));

    display.setRemoveControlStyle(label[6], controlStyleBackground);
    display.setRemoveControlStyle(label[7], controlStyleBackground);

    display.setControlStyle(label[0],
                            controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                              controlStyleShowBitmap);
    display.setControlStyle(label[3],
                            controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                              controlStyleShowBitmap);

    reloadConfigLabels();

    titleBar->show();
    titleBar->setTitle("Config");

    display.setControlText(label[7], "Enter");

    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    frameData.placesCount = 4;
    frameData.startPlace = 0;
    frameData.places[0] = &framesPlacesConfig[0][0];
    frameData.places[1] = &framesPlacesConfig[1][0];
    frameData.places[2] = &framesPlacesConfig[2][0];
    frameData.places[3] = &framesPlacesConfig[3][0];

    display.setControlValue(bgLabel, 0b11000100); // 68
    display.refreshControl(bgLabel);

    display.setControlShow(frameControl);

    display.setControlHide(popoutWindowLabel);
    display.refreshControl(popoutWindowLabel);

    activateLabelsBorder();
    display.synchronizeRefresh();
}

//==============================================================================================================
void cConfigEditor::activateLabelsBorder()
{
    if (selectedPlace > frameData.placesCount - 1)
        return;

    display.setControlValue(frameControl, selectedPlace);
    display.refreshControl(frameControl);
}

void cConfigEditor::refreshListsColors()
{
    //    if (selectedPlace == 0)
    //    {
    //        display.setControlColors(configBasemenuListControl,
    //        display.getTheme().projectActiveListColors);
    //        display.setControlColors(configSubmenuListControl, display.getTheme().activeListColors);
    //    }
    //    else if (selectedPlace == 1)
    //    {
    //        display.setControlColors(configBasemenuListControl, display.getTheme().activeListColors);
    //        display.setControlColors(configSubmenuListControl,
    //        display.getTheme().projectActiveListColors);
    //    }
    //
    //    display.refreshControl(configBasemenuListControl);
    //    display.refreshControl(configSubmenuListControl);
}

void cConfigEditor::changeLabelText(uint8_t labelIdx, const char* text)
{
    display.setControlText(label[labelIdx], text);
    display.setControlShow(label[labelIdx]);
    display.refreshControl(label[labelIdx]);
}

void cConfigEditor::showConfigList4(uint8_t list_width,
                                    uint8_t start,
                                    uint8_t length,
                                    char** listText)
{
    configListShown = 1;
    selectedConfigListPosition = start;

    if (list_width == 3)
    {
        display.setControlPosition(configListControl, (800 / 16) * 7 + 1, -1);
        display.setControlSize(configListControl, ((800 / 16) * 5) - 3, -1);
        frameData.places[2] = &framesPlacesConfig[3][0];
    }
    else
    {
        display.setControlPosition(configListControl, (800 / 8) * 4 + 1, -1);
        display.setControlSize(configListControl, ((800 / 8) * 2) - 3, -1);
        frameData.places[2] = &framesPlacesConfig[2][0];
    }

    configList.linesCount = 14;
    configList.start = start;
    configList.length = length;
    configList.data = listText;

    display.setControlData(configListControl, &configList);
    display.setControlShow(configListControl);
    display.refreshControl(configListControl);

    display.setControlShow(label[3]);

    display.synchronizeRefresh();
}

void cConfigEditor::showConfigList4Val(uint8_t* value,
                                       uint8_t min,
                                       uint8_t max,
                                       uint8_t interval,
                                       uint8_t dev)
{
    configListShown = 1;
    selectedConfigListPosition = *value;

    display.setControlPosition(configListControl, (800 / 8) * 4 + 1, -1);
    display.setControlSize(configListControl, ((800 / 8) * 2) - 3, -1);

    configList.linesCount = 14;
    configList.start = *value;
    configList.length = (max - min) + 1;

    if (ptrTempValuesList != nullptr)
    {
        for (uint16_t i = 0; i < lastTempListLength; i++)
        {
            delete[] ptrTempValuesList[i];
        }

        delete[] ptrTempValuesList;
        ptrTempValuesList = nullptr;
    }

    lastTempListLength = configList.length;
    ptrTempValuesList = new char*[lastTempListLength];

    for (uint16_t i = 0; i < lastTempListLength; i++)
    {
        ptrTempValuesList[i] = new char[5];

        sprintf(ptrTempValuesList[i], "%d", min + i + dev);
    }

    configList.data = ptrTempValuesList;

    display.setControlData(configListControl, &configList);
    display.refreshControl(configListControl);
    display.setControlShow(configListControl);

    display.setControlShow(label[3]);

    display.synchronizeRefresh();
}

void cConfigEditor::hideConfigList()
{
    flashingState = 0;

    display.setControlHide(configListControl);
    display.setControlHide(configLabel);
    display.refreshControl(configLabel);

    configListShown = 0;
}

void cConfigEditor::showConfigItemLabel(const char* text)
{
    display.setControlShow(blankSpaceCover[1]);
    display.refreshControl(blankSpaceCover[1]);

    display.setControlShow(configLabel);
    display.setControlText(configLabel, text);
    display.refreshControl(configLabel);
}

void cConfigEditor::hideConfigItemLabel()
{
    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(configLabel);
    display.refreshControl(configLabel);
}

void cConfigEditor::showConfigItemDesctription(const char* text)
{
    if (text == nullptr)
    {
        display.setControlHide(descriptionTextBox);
    }
    else
    {
        display.setControlShow(descriptionTextBox);
        display.setControlText(descriptionTextBox, text);
    }

    display.refreshControl(descriptionTextBox);
}

void cConfigEditor::hideConfigItemDesctription()
{
    display.setControlHide(descriptionTextBox);
    display.refreshControl(descriptionTextBox);
}

void cConfigEditor::reloadConfigLabels()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }
}

void cConfigEditor::showSubmenu()
{
    submenuShown = 1;

    display.setControlShow(configSubmenuListControl);
    display.setControlShow(label[2]);
    display.setControlShow(label[3]);
    display.setControlShow(label[4]);
    display.setControlShow(label[5]);

    display.refreshControl(label[4]);
}

void cConfigEditor::hideSubmenu()
{
    display.setControlHide(configSubmenuListControl);
    display.setControlHide(label[2]);
    display.setControlHide(label[3]);
    display.setControlHide(label[4]);
    display.setControlHide(label[5]);

    display.refreshControl(label[4]);

    submenuShown = 0;
}

//==============================================================================================================
//==============================================================================================================
//==============================================================================================================
// firmware update confirm popup
void cConfigEditor::showFirmwareUpdatePopout()
{
    updatePopupShown = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(label[i]);
    }

    display.setControlShow(label[7]);
    display.setControlShow(label[6]);
    display.setControlText(label[7], "Yes");
    display.setControlText(label[6], "Cancel");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[7], controlStyleBackground);
    display.setAddControlStyle(label[6], controlStyleBackground);

    display.refreshControl(label[7]);
    display.refreshControl(label[6]);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    display.setControlText(popoutWindowLabel, "Do you want to flash new firmware?");
    display.setControlShow(popoutWindowLabel);
    display.refreshControl(popoutWindowLabel);

    display.synchronizeRefresh();
}

void cConfigEditor::hidePopout()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.refreshControl(label[i]);
    }

    display.setRemoveControlStyle(label[7], controlStyleBackground);
    display.setRemoveControlStyle(label[6], controlStyleBackground);

    display.setControlShow(frameControl);
    display.refreshControl(frameControl);

    display.setControlHide(popoutWindowLabel);
    display.refreshControl(popoutWindowLabel);

    display.synchronizeRefresh();
}

//===============================================================
// reset config confirm popup
void cConfigEditor::showResetConfigPopout()
{
    resetConfigPopupShown = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(label[i]);
    }

    display.setControlShow(label[7]);
    display.setControlShow(label[6]);
    display.setControlText(label[7], "Yes");
    display.setControlText(label[6], "Cancel");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setAddControlStyle(label[7], controlStyleBackground);
    display.setAddControlStyle(label[6], controlStyleBackground);

    display.refreshControl(label[7]);
    display.refreshControl(label[6]);

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);

    display.setControlText(popoutWindowLabel, "Are you sure you want to clear the config?");
    display.setControlShow(popoutWindowLabel);
    display.refreshControl(popoutWindowLabel);

    display.synchronizeRefresh();
}

//==============================================================================
// credits
void cConfigEditor::showCreditsControls()
{
    titleBar->show();
    titleBar->setTitle("Info");

    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(blankSpaceCover[1]);

    for (uint8_t i = 0; i < 7; i++)
    {
        display.setControlHide(label[i]);
    }

    display.setControlShow(label[7]);
    display.setControlText(label[7], "Close");
    display.refreshControl(label[7]);

    display.setControlHide(frameControl);

    display.setControlHide(bgLabel);

    display.setControlShow(creditsTextBox);
    display.setControlText(creditsTextBox, creditsText);
    display.refreshControl(creditsTextBox);

    display.synchronizeRefresh();
}

void cConfigEditor::hideCreditsControls()
{
    display.setControlHide(creditsTextBox);

    display.setControlShow(bgLabel);
    display.setControlValue(bgLabel, 255); // 68
    display.refreshControl(bgLabel);

    display.setControlText(label[7], "");
    display.refreshControl(label[7]);
}

#ifdef GRID_EDITOR_ENABLED
void cConfigEditor::showGridScreen()
{

    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(configBasemenuListControl);
    display.setControlHide(configSubmenuListControl);

    display.setControlText(titleLabel, "Grid Editor");
    display.refreshControl(titleLabel);

    display.setControlText(gridNameLabel, mtGrid.name);
    display.setControlShow(gridNameLabel);
    display.refreshControl(gridNameLabel);

    display.setControlValue(bgLabel, 255); // 0b11101111
    display.refreshControl(bgLabel);

    for (uint8_t i = 0; i < 48; i++)
    {
        interfaceGlobals.padNamesPointer[i] = (char*)mtNotes[mtGrid.pad[i].note];
        sprintf(interfaceGlobals.padFinetuneNames[i], "%d", mtGrid.pad[i].microtune);

        if (mtGrid.pad[i].ledEnable)
            padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, i);
        else
            padsBacklight.setBackLayer(0, 0, i);
    }

    display.setControlShow(gridPadsControl);
    display.setControlValue(gridPadsControl, gridEditor.getSelectedPad());
    display.refreshControl(gridPadsControl);

    for (uint8_t i = 0; i < 5; i++)
    {
        display.setControlHide(label[i]);
        display.refreshControl(label[i]);
    }

    display.setControlText(label[0], "Note");
    display.setControlText(label[1], "Microtune");
    display.setControlText(label[2], "LED");

    for (uint8_t i = 0; i < 3; i++)
    {
        display.setControlStyle(label[i],
                                (controlStyleCenterX | controlStyleCenterY | controlStyleFont3));
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.setControlValue(label[i], 1);

        reloadPadScreenDisplayedValue(i);
        refreshPadValue(i, enScreenType::screenTypeGridScreen);
    }

    frameData.placesCount = 3;
    frameData.startPlace = selectedPlacePadScreen;
    frameData.places[0] = &framesPlacesConfigGridScreen[0][0];
    frameData.places[1] = &framesPlacesConfigGridScreen[1][0];
    frameData.places[2] = &framesPlacesConfigGridScreen[2][0];

    refreshGridScreenFrame();

    display.setControlStyle(
      label[5],
      (controlStyleCenterX | controlStyleCenterY | controlStyleFont3 | controlStyleCenterY));
    display.setControlText(label[5], "Save");
    display.setControlShow(label[5]);
    display.refreshControl(label[5]);
    display.setControlStyle(
      label[6],
      (controlStyleCenterX | controlStyleCenterY | controlStyleFont3 | controlStyleCenterY));
    display.setControlText(label[6], "Cancel");
    display.setControlShow(label[6]);
    display.refreshControl(label[6]);
    display.setControlText(label[7], "Select");
    display.setControlShow(label[7]);
    display.refreshControl(label[7]);

    display.synchronizeRefresh();
}

void cConfigEditor::refreshGridScreen()
{
    display.setControlValue(gridPadsControl, gridEditor.getSelectedPad());
    display.refreshControl(gridPadsControl);

    for (uint8_t i = 0; i < 3; i++)
    {
        reloadPadScreenDisplayedValue(i);
        refreshPadValue(i, enScreenType::screenTypeGridScreen);
    }
}

void cConfigEditor::hideGridScreen()
{
    display.setControlHide(gridPadsControl);
    display.refreshControl(gridPadsControl);
    display.setControlHide(gridNameLabel);
    display.refreshControl(gridNameLabel);
}
void cConfigEditor::showPadScreen()
{
    display.setControlShow(padBar[0]);
    display.setControlShow(padBar[1]);
    display.setControlShow(padList);

    display.setControlText(label[0], "Note");
    display.setControlText(label[1], "Microtune");
    display.setControlText(label[2], "LED");

    for (uint8_t i = 0; i < 3; i++)
    {
        display.setControlStyle(label[i],
                                (controlStyleCenterX | controlStyleCenterY | controlStyleFont3));
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.setControlValue(label[i], 1);

        reloadPadScreenDisplayedValue(i);
        refreshPadValue(i, enScreenType::screenTypePadScreen);
    }

    for (uint8_t i = 3; i < 7; i++)
    {
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlHide(label[i]);
    }

    display.setControlText(label[7], "Back");

    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }

    sprintf(padScreenTitleLabelName, "Grid Editor Pad %d", gridEditor.getSelectedPad() + 1);

    display.setControlText(titleLabel, padScreenTitleLabelName);
    display.refreshControl(titleLabel);

    display.setControlText(gridNameLabel, mtGrid.name);
    display.setControlShow(gridNameLabel);
    display.refreshControl(gridNameLabel);

    display.setControlValue(bgLabel, 255); // 0b10001111
    display.refreshControl(bgLabel);

    display.refreshControl(padBar[0]);
    display.refreshControl(padBar[1]);
    display.refreshControl(padList);

    frameData.placesCount = 3;
    frameData.startPlace = selectedPlacePadScreen;
    frameData.places[0] = &framesPlacesConfigPadScreen[0][0];
    frameData.places[1] = &framesPlacesConfigPadScreen[1][0];
    frameData.places[2] = &framesPlacesConfigPadScreen[2][0];

    refreshPadScreenFrame();
    display.setControlShow(frameControl);
    display.synchronizeRefresh();
}
void cConfigEditor::hidePadScreen()
{
    display.setControlHide(padBar[0]);
    display.setControlHide(padBar[1]);
    display.setControlHide(padList);

    display.refreshControl(padBar[0]);
    display.refreshControl(padBar[1]);
    display.refreshControl(padList);
}

void cConfigEditor::refreshPadScreenFrame()
{
    display.setControlValue(frameControl, selectedPlacePadScreen);
    display.refreshControl(frameControl);
}

void cConfigEditor::refreshGridScreenFrame()
{
    display.setControlValue(frameControl, selectedPlaceGridScreen);
    display.refreshControl(frameControl);
}

void cConfigEditor::refreshPadValue(uint8_t value, enScreenType screen)
{
    switch (value)
    {
        case 0:
            if (screen == screenTypePadScreen)
            {
                display.setControlValue(padBar[value], padScreenDisplayedValue[value]);
                display.refreshControl(padBar[value]);
            }
            display.setControlText2(label[value],
                                    mtNotes[mtGrid.pad[gridEditor.getSelectedPad()].note]);
            display.refreshControl(label[value]);
            break;
        case 1:
            if (screen == screenTypePadScreen)
            {
                display.setControlValue(padBar[value], padScreenDisplayedValue[value]);
                display.refreshControl(padBar[value]);
            }
            sprintf(microtuneValue, "%d", mtGrid.pad[gridEditor.getSelectedPad()].microtune);
            display.setControlText2(label[value], microtuneValue);
            display.refreshControl(label[value]);
            break;
        case 2:
            if (screen == screenTypePadScreen)
            {
                display.setControlValue(padList, padScreenDisplayedValue[value]);
                display.refreshControl(padList);
            }
            display.setControlText2(label[value], padListNames[padScreenDisplayedValue[value]]);
            display.refreshControl(label[value]);
            if (mtGrid.pad[gridEditor.getSelectedPad()].ledEnable)
                padsBacklight.setBackLayer(
                  1, mtConfig.values.padsLightBack, gridEditor.getSelectedPad());
            else
                padsBacklight.setBackLayer(0, 0, gridEditor.getSelectedPad());
            break;
        default: break;
    }
}
#endif

void cConfigEditor::showBatteryInfo()
{
#if defined(TRACKER_MINI)
    Battery* battery = platform->getBattery();
    if (!battery->getState())
    {
        return;
    }

    sprintf(batteryText, "Battery: %d%% %.2fV", battery->getLevel(), battery->getVoltage());
    display.setControlText(batteryLabelControl, batteryText);

    int min = battery->getTimeTo();
    int hour = min / 60;
    min = min % 60;
    sprintf(batteryText2, "%s%dh %dmin", battery->getCharging() ? "Charging... " : "", hour, min);
    display.setControlText2(batteryLabelControl, batteryText2);

    display.setControlShow(batteryLabelControl);
    display.refreshControl(batteryLabelControl);
#endif
}

void cConfigEditor::showDiagnosticInfo()
{
#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
    Profiler::AudioProcessorUsage usage;
    Profiler::getInstance()->getAudioProcessingUsagePercent(usage);

    snprintf(audioPerformanceText,
             sizeof(audioPerformanceText),
             "min: %d%%, max: %d%%",
             usage.minimum,
             usage.maximum);

    display.setControlText(audioPerformanceLabelControl, audioPerformanceText);

    snprintf(audioPerformanceText2,
             sizeof(audioPerformanceText2),
             "avg: %d%%, last: %d%%",
             usage.average,
             usage.last);

    display.setControlText2(audioPerformanceLabelControl, audioPerformanceText2);

    display.setControlShow(audioPerformanceLabelControl);
    display.refreshControl(audioPerformanceLabelControl);
#endif

#ifdef MEMORY_USAGE_INFO_ENABLED
    size_t freeBytes = Profiler::getInstance()->freeRamBytes();
    snprintf(heapUsageText, sizeof(heapUsageText), "free heap: %d B", freeBytes);
    size_t maxStackBytes = Profiler::getInstance()->maxStackBytes();
    snprintf(stackUsageText, sizeof(stackUsageText), "max stack: %d B", maxStackBytes);

    display.setControlText(memoryUsageLabelControl, heapUsageText);
    display.setControlText2(memoryUsageLabelControl, stackUsageText);
    display.setControlShow(memoryUsageLabelControl);
    display.refreshControl(memoryUsageLabelControl);
#endif
}
