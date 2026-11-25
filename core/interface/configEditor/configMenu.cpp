#include "configEditor/configMenu.h"
#include "configEditor/configEditor.h"

#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"
#include "mtSequencer.h"
#include "stereoPairs.h"
#include "mtTuning.h"
#include "mtTuneScale.h"
#include "ff_hooks.h"

#include "imageViewer/imageViewerDefs.h"

#include "polyassert.h"

#include <array>

#include "feature_flags.h"
#ifdef FEATURE_FEATURES
#include "features.h"
#endif

#ifdef TRACKER_MINI
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#endif

// clang-format off

// FEATURE_FF_CORE: + the Project Settings page of the File screen (the last group of the base list, hidden in Config)
#include "configMenu_ff.inc"

#ifdef TRACKER_MINI
constexpr uint8_t baseMenuGroupCount = 8 + snapGroupCount + featuresGroupCount;
constexpr uint8_t projectMenuGroupCount = 10;
constexpr uint8_t generalMenuGroupCount = 11;
#elif defined(PLATFORM_IMX)
constexpr uint8_t baseMenuGroupCount = 7 + snapGroupCount + featuresGroupCount;
constexpr uint8_t projectMenuGroupCount = 10;
constexpr uint8_t generalMenuGroupCount = 14;
#elif defined(FEATURE_EXT_HOOKS)
constexpr uint8_t baseMenuGroupCount = 8 + snapGroupCount; // + USB (hidden while USB audio and USB drive are off) + Features
constexpr uint8_t projectMenuGroupCount = 10;
constexpr uint8_t generalMenuGroupCount = 14;
#else
constexpr uint8_t baseMenuGroupCount = 6 + snapGroupCount;
constexpr uint8_t projectMenuGroupCount = 10;
constexpr uint8_t generalMenuGroupCount = 14;
#endif

// baza baz
cMenuGroup menuBase(menuBase, 0, baseMenuGroupCount);
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================

// grupy glowne

cMenuGroup menuProject(menuBase, "Project Settings", projectMenuGroupCount);
cMenuGroup menuGeneral(menuBase, "General", generalMenuGroupCount);
cMenuGroup menuMidi(menuBase, "MIDI", 11 + kSampleTracks + kFfMidiMenuRows);   // + Program in, the last row (a row past the count is not shown)

#ifdef PLATFORM_IMX
cMenuGroup menuUsb(menuBase, "USB", 2);
#elif defined(FEATURE_EXT_HOOKS) && !defined(TRACKER_MINI)
cMenuGroup menuUsb(menuBase, "USB", 4); // the original: Storage (USB_MSC=1), Audio mode, FX in 8+3, Restart now (usbMenuSync)
#endif

#ifdef FEATURE_FEATURES_FULL
static uint8_t usbMenuSync(); // 1 while the USB entry is hidden from the base list (Features: USB audio and USB drive both off)
#else
static uint8_t usbMenuSync() { return 0; }
#endif

cMenuGroup menuMetro(menuBase, "Metronome", 5);
#include "configMenu_featgroup.inc"
cMenuGroup menuFirmware(menuBase, "Firmware", 4);

#ifdef TRACKER_MINI
cMenuGroup menuFunctionButtons(menuBase, "Fn buttons binding", 8);
#endif

cMenuGroup menuCredits(menuBase, "Info", 4);

#include "configMenu_projgroup.inc"

// elementy/////////

// project settings
const strItemTypeListValues setupPatternDiv{ &mtProject.values.patternDiv, 0, 15, 1, 1 };
const strItemTypeListText setupPerfSource{ &mtConfig.general.performanceSource, 2, performanceSourceTexts };
const strItemTypeListTextWithAction setupPadsScale{ &mtProject.values.padBoardScale, 39, scaleNames, setPadBoardValues };
const strItemTypeListTextWithAction setupPadsRootNote{ &mtProject.values.padBoardRootNote,  37,  &allNotes[12], setPadBoardValues };
const strItemTypeListTextWithAction setupPadsLayout{ &mtProject.values.padBoardNoteOffset, 12, padsLayoutTexts, setPadBoardValues };
const strItemTypeListTextWithAction setupAntialiasing{ &mtProject.values.antialiasingEnable, 2, onOffStateTexts, setAntialiasingEnable };
const strItemTypeListTextWithAction setupLimiterMode{ &mtProject.values.limiterMode, 2, limiterModeTexts, setLimiterMode };
const strItemTypeListTextWithAction setupSendsMode{ &mtProject.values.sendsMode, 2, sendsModeTexts, setSendsMode };
const strItemTypeListTextWithAction setupHeadroomMode{ &mtProject.values.isHeadroomEnabled, 2, onOffStateTexts, setHeadroomMode };
const strItemTypeListTextWithAction setupSnapToZeroMode{ &mtProject.values.isSnapToZeroEnabled, 2, onOffStateTexts, setSnapToZeroMode };

cMenuItem melPerformanceSource(menuProject, "Performance presets", perfPresDesc, menuTypeItemListText, &setupPerfSource);
cMenuItem melPatternDiv(menuProject, "Pattern divider", pattDivDesc, menuTypeItemListValues, &setupPatternDiv);
cMenuItem melPadsScale(menuProject, "Notes scale", scaleDesc,  menuTypeItemListTextWithAction, &setupPadsScale);
cMenuItem melPadsRootNote(menuProject,  "Root note", rootNoteDesc, menuTypeItemListTextWithAction,  &setupPadsRootNote);
cMenuItem melPadsLayout(menuProject, "Notes layout", padLayDesc, menuTypeItemListTextWithAction, &setupPadsLayout);
cMenuItem melAntialiasingEnable(menuProject, "Anti-aliasing", antialDesc, menuTypeItemListTextWithAction, &setupAntialiasing);
cMenuItem melLimiterMode(menuProject, "Limiter mode", limiterDesc, menuTypeItemListTextWithAction, &setupLimiterMode);
cMenuItem melSendsMode(menuProject, "Sends mode", sendsDesc,  menuTypeItemListTextWithAction, &setupSendsMode);
cMenuItem melHeadroomMode(menuProject, "Extra headroom", headroomDesc,  menuTypeItemListTextWithAction, &setupHeadroomMode);
cMenuItem melSnapToZeroMode(menuProject, "Snap to zero", snapToZeroDesc,  menuTypeItemListTextWithAction, &setupSnapToZeroMode);
// general
const strItemTypeListTextWithAction setupRadioRegion{ &mtConfig.general.radioRegion, 4, radioRegion, setRadioRegion };
const strItemTypeListTextWithAction setupDispBrightness{ &mtConfig.general.dispBrightness, 3, brightness, setDisplayBrightness };
strItemTypeListTextWithAction setupDispTheme{ &mtConfig.general.theme, DisplayThemes::kCount, themeChoices, setDisplayTheme };
strItemTypeListTextWithAction setupDispFont{ &mtConfig.general.font, 2,  ptrFont, setDisplayFont };

void refreshThemeAndFontMenuOptions()
{
    setupDispTheme.count = ThemeManager::getThemeCount();
    setupDispTheme.ptrText = ThemeManager::getThemeNames();
    setupDispFont.count = ThemeManager::getFontCount();
    setupDispFont.ptrText = ThemeManager::getFontNames();
}

const strItemTypeListTextWithAction setupPadsBrightness{ &mtConfig.general.padsBrightness, 3, brightness, setPadsBrightness };
const strItemTypeListText setupRecQuantization{ &mtConfig.general.recOptions, 4, recOptionsTexts };
const strItemTypeListText setupRecMode{ &mtConfig.common.recReplace, 2, recModeTexts };
const strItemTypeListText setupRecThreshold{ &mtConfig.general.recThreshold, recThreshold_count, recThresholdTexts };
const strItemTypeListText setupHexMode{ &mtConfig.general.numberingMode, std::size(numberingModeTexts), numberingModeTexts };
const strItemTypeListText setupPadPreviewInPatternEdit{ &mtConfig.general.padPreviewInPatternEdit, 3, padPreviewInPatternEditTexts };
const strItemTypeListText setupPatternArrangement{ &mtConfig.general.patternArrangement, 3, patternArrangementTexts };
const strItemTypeListText setupPatternInfo{ &mtConfig.general.patternInfo, 3, patternInfoTexts };
const strItemTypeListText setupPatternVm{ &mtConfig.common.patternVmColumns, 2, patternVmTexts };
const strItemTypeListTextWithAction setupLineInOutType{ &mtConfig.general.lineInOutType, 3,  lineInOutType, setLineInOutType };

#ifndef TRACKER_MINI
cMenuItem melRadioReg(menuGeneral,
                      "Radio region",
                      radioRegDesc,
                      menuTypeItemListTextWithAction,
                      &setupRadioRegion);
#endif
cMenuItem melDispBright(menuGeneral,
                        "Display brightness",
                        displBrightDesc,
                        menuTypeItemListTextWithAction,
                        &setupDispBrightness);
cMenuItem melDispTheme(menuGeneral,
                       "Display theme",
                       displThemeDesc,
                       menuTypeItemListTextWithAction,
                       &setupDispTheme);
cMenuItem melDispFont(menuGeneral,
                      "Display font",
                      displFontDesc,
                      menuTypeItemListTextWithAction,
                      &setupDispFont);

#ifndef TRACKER_MINI
cMenuItem melPadsBright(menuGeneral,
                        "Pads brightness",
                        padsBrightDesc,
                        menuTypeItemListTextWithAction,
                        &setupPadsBrightness);
cMenuItem melPadPreview(menuGeneral,
                        "Pads preview",
                        padsPrevDesc,
                        menuTypeItemListText,
                        &setupPadPreviewInPatternEdit);
#endif
cMenuItem melRecQuantization(menuGeneral,
                             "Recording options",
                             recNotesDesc,
                             menuTypeItemListText,
                             &setupRecQuantization);
cMenuItem melRecMode(menuGeneral,
                     "Recording mode",
                     recModeDesc,
                     menuTypeItemListText,
                     &setupRecMode);
cMenuItem melRecThreshold(menuGeneral,
                          "Recording Threshold",
                          recThresholdDesc,
                          menuTypeItemListText,
                          &setupRecThreshold);
cMenuItem melHexMode(menuGeneral,
                     "Numbering mode",
                     hexModeDesc,
                     menuTypeItemListText,
                     &setupHexMode);
cMenuItem melPatternArrangement(menuGeneral,
                                "Pattern arrangement",
                                patternArraDesc,
                                menuTypeItemListText,
                                &setupPatternArrangement);
cMenuItem melPatternInfo(menuGeneral,
                         "Pattern top info",
                         patternInfo,
                         menuTypeItemListText,
                         &setupPatternInfo);
cMenuItem melPatternVm(menuGeneral,
                       "Pattern V/m columns",
                       patternVmDesc,
                       menuTypeItemListText,
                       &setupPatternVm);
cMenuItem melLinInOutType(menuGeneral,
                          "Line in channel",
                          lineInChannelDesc,
                          menuTypeItemListTextWithAction,
                          &setupLineInOutType);

//==================================================================================================================================================================
// Metroneome
const strItemTypeListTextWithAction setupMetroState{ &mtConfig.metronome.state, 2, onOffStateTexts, setMetronomValues };
const strItemTypeListTextWithAction setupMetroPreRoll{ &mtConfig.metronome.preRoll, 2, onOffStateTexts, setMetronomValues };
const strItemTypeListTextWithAction setupMetroTimeSigNum{ &mtConfig.metronome.timeSignatureNumerator, 12, metroTimeSig, setMetronomValues };
const strItemTypeListTextWithAction setupMetroTimeSigDenum{ &mtConfig.metronome.timeSignatureDenominator, 12, metroTimeSig, setMetronomValues };
const strItemTypeListValues setupMetroVolume{ &mtConfig.metronome.volume, 0, 100, 1, 0 };

cMenuItem melMetroState(menuMetro,
                        "State",
                        nullptr,
                        menuTypeItemListTextWithAction,
                        &setupMetroState);
cMenuItem melMetroPreRoll(menuMetro,
                          "Pre roll",
                          nullptr,
                          menuTypeItemListTextWithAction,
                          &setupMetroPreRoll);
cMenuItem melMetroTimeSigNum(menuMetro,
                             "Time signature num",
                             nullptr,
                             menuTypeItemListTextWithAction,
                             &setupMetroTimeSigNum);
cMenuItem melMetroTimeSigDenum(menuMetro,
                               "Time signature denum",
                               nullptr,
                               menuTypeItemListTextWithAction,
                               &setupMetroTimeSigDenum);
cMenuItem melMetroVolume(menuMetro, "Volume", nullptr, menuTypeItemListValues, &setupMetroVolume);

//==================================================================================================================================================================
// MIDI
const strItemTypeListText clockInSetup{ &mtConfig.midi.clkIn, 3, clockIn };
const strItemTypeListText clockOutSetup{ &mtConfig.midi.clkOut, 4, midiOut };
const strItemTypeListText transportInSetup{ &mtConfig.midi.transportIn, 3, clockIn };
const strItemTypeListText transportOutSetup{ &mtConfig.midi.transportOut, 4, midiOut };
const strItemTypeListText notesInModeSetup{ &mtConfig.midi.notesInMode, 4, midiIn };
const strItemTypeListText notesOutModeSetup{ &mtConfig.midi.notesOutMode, 4, midiOut };
const strItemTypeListText notesInChanneletup{ &mtConfig.midi.notesInChannel, 17, notesChannel };
const strItemTypeListText ccInModeSetup{ &mtConfig.midi.ccInMode, 4, midiIn };
const strItemTypeListText ccInChannelSetup{ &mtConfig.midi.ccInChannel, 17, notesChannel };
const strItemTypeListText middleCSetup{ &mtConfig.midi.middleC, 4, middleC };
const strItemTypeListTextWithAction slaveClockCompSetup{ &mtConfig.midi.slaveClockComp, std::size(slaveClockComp),  slaveClockComp, setSlaveClockCompValue };
const strItemTypeListText programInSetup{ &mtConfig.midi.programIn, 2, onOffStateTexts };

cMenuItem melClockIn(menuMidi, "Clock in", nullptr, menuTypeItemListText, &clockInSetup);
cMenuItem melClockOut(menuMidi, "Clock out", nullptr, menuTypeItemListText, &clockOutSetup);
cMenuItem melTransportIn(menuMidi,
                         "Transport in",
                         nullptr,
                         menuTypeItemListText,
                         &transportInSetup);
cMenuItem melTransportOut(menuMidi,
                          "Transport out",
                          nullptr,
                          menuTypeItemListText,
                          &transportOutSetup);
cMenuItem melNotsInMode(menuMidi, "Notes in", nullptr, menuTypeItemListText, &notesInModeSetup);
cMenuItem melNotesInChannel(menuMidi,
                            "Notes in channel",
                            nullptr,
                            menuTypeItemListText,
                            &notesInChanneletup);
const strItemTypeListText trackNotesInChannelSetup[kSampleTracks] = {
    { &mtConfig.midi.trackNotesInChannel[0], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[1], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[2], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[3], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[4], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[5], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[6], 17, trackNotesInChannel },
    { &mtConfig.midi.trackNotesInChannel[7], 17, trackNotesInChannel },
};
static const char* const trackNotesInDesc =
  "Notes received on this channel play directly on this track, using the instrument that last sounded there. Overrides 'Notes in channel' for that channel.";
cMenuItem melTrack1NotesIn(menuMidi, "Track 1 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[0]);
cMenuItem melTrack2NotesIn(menuMidi, "Track 2 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[1]);
cMenuItem melTrack3NotesIn(menuMidi, "Track 3 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[2]);
cMenuItem melTrack4NotesIn(menuMidi, "Track 4 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[3]);
cMenuItem melTrack5NotesIn(menuMidi, "Track 5 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[4]);
cMenuItem melTrack6NotesIn(menuMidi, "Track 6 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[5]);
cMenuItem melTrack7NotesIn(menuMidi, "Track 7 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[6]);
cMenuItem melTrack8NotesIn(menuMidi, "Track 8 in channel", trackNotesInDesc, menuTypeItemListText, &trackNotesInChannelSetup[7]);
cMenuItem melNotsOutMode(menuMidi, "MIDI out", nullptr, menuTypeItemListText, &notesOutModeSetup);
cMenuItem melCCInMode(menuMidi, "CC in", supportedCcInDesc, menuTypeItemListText, &ccInModeSetup);
cMenuItem melCCInChannel(menuMidi,
                         "CC in channel",
                         supportedCcInDesc,
                         menuTypeItemListText,
                         &ccInChannelSetup);
cMenuItem melMiddleC(menuMidi, "Middle C", nullptr, menuTypeItemListText, &middleCSetup);
cMenuItem melSlaveClockComp(menuMidi,
                            "Clock sync correction",
                            slaveClockCompDesc,
                            menuTypeItemListTextWithAction,
                            &slaveClockCompSetup);
static const char* const programInDesc =
  "A MIDI Program Change 1 to 16 on channel 1 to 3 picks bank slot 1 to 16 of Synth 1 to 3 (the Patch FX slot). On the Synth play page it picks the slot of the side on that channel. USB and MIDI In jack.";
cMenuItem melProgramIn(menuMidi, "Program in", programInDesc, menuTypeItemListText, &programInSetup);

// Linked stereo pairs (stereoPairs.h): mtMidi skips the follower track of a pair, so its "Track N in channel" row would do nothing. It is not listed while
// the pair is linked (the value stays in mtConfig, unlinking lists the row again). Same trick as the USB entry: the children are rewritten in order
// (midiMenuSync, called when the Config menu is built and after a link change)
static cMenuItem* const midiRows[] = {
    &melClockIn,      &melClockOut,     &melTransportIn,  &melTransportOut, &melNotsInMode,   &melNotesInChannel, &melTrack1NotesIn, &melTrack2NotesIn,
    &melTrack3NotesIn, &melTrack4NotesIn, &melTrack5NotesIn, &melTrack6NotesIn, &melTrack7NotesIn, &melTrack8NotesIn, &melNotsOutMode,  &melCCInMode,
    &melCCInChannel,  &melMiddleC,      &melSlaveClockComp,
};
static_assert(std::size(midiRows) == 11 + kSampleTracks, "every MIDI row is listed");
constexpr int kFirstTrackRow = 6;   // the index of melTrack1NotesIn in midiRows

void midiMenuSync()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < std::size(midiRows); i++)
    {
        const int track = int(i) - kFirstTrackRow;
        if (track >= 0 && track < int(kSampleTracks) && stereopairs::isFollower(track)) continue;
        menuMidi.setChild(n++, midiRows[i], midiRows[i]->getName());
    }
    if (kFfMidiMenuRows)
    {
        menuMidi.setChild(n++, &melProgramIn, melProgramIn.getName());   // Program in, the last row
    }
    menuMidi.setCount(n);
}

//==================================================================================================================================================================
// Firmware
const strItemTypeActionButton updateFirmwareSetup{ firmwareUpgradeActivate, nullptr };
const strItemTypeLabel currentVersionSetup{ interfaceGlobals.currentFirmwareVersion };
const strItemTypeLabelPtr currentBuildSetup{ &interfaceGlobals.currentFirmwareBuild };
const strItemTypeActionButton resetConfigSetup{ resetConfigShowPopup, nullptr };

cMenuItem melUpdateFirmware(menuFirmware,
                            "Firmware update",
                            firmUpdDesc,
                            menuTypeItemActionButton,
                            &updateFirmwareSetup);
cMenuItem melCurrentVersion(menuFirmware,
                            "Current version",
                            nullptr,
                            menuTypeItemLabel,
                            &currentVersionSetup);
cMenuItem melCurrentBuild(menuFirmware,
                            "Build",
                            nullptr,
                            menuTypeItemLabelPtr,
                            &currentBuildSetup);
cMenuItem melResetConfig(menuFirmware,
                         "Reset config",
                         nullptr,
                         menuTypeItemActionButton,
                         &resetConfigSetup);

//==================================================================================================================================================================
// USB

#ifdef PLATFORM_IMX
const strItemTypeActionButton usbStorageSetup{ usbStorageEnter, nullptr };
const strItemTypeListTextWithAction usbAudioSetup{ &mtConfig.common.usbAudioMode,
                                              sizeof(kUsbAudioList)/sizeof(kUsbAudioList[0]), kUsbAudioList, usbAudioChange };

cMenuItem melUsbStorage(menuUsb,
                          "Storage",
						  nullptr,
						  menuTypeItemActionButton,
                          &usbStorageSetup);

cMenuItem melUsbAudio(menuUsb,
                          "Audio",
						  usbAudioDesc,
						  menuTypeItemListTextWithAction,
                          &usbAudioSetup);
#elif defined(FEATURE_USB_MSC) && defined(FEATURE_EXT_HOOKS) && !defined(TRACKER_MINI)
// USB_MSC=1 on the original: the card as a USB drive (Drive mode). Works when Config > Features > USB drive was on at start
const strItemTypeActionButton usbStorageSetup{ usbStorageEnter, nullptr };

cMenuItem melUsbStorage(menuUsb,
                          "Storage",
                          usbStorageDesc,
                          menuTypeItemActionButton,
                          &usbStorageSetup);
#endif
//==================================================================================================================================================================
// Project Settings page (FEATURE_FF_CORE): the File screen's Project Settings entry opens the Config editor on menuProjectPage. The rows
// are the settings of project.mtff and what the sequencer keeps with the project
#include "configMenu_projpage.inc"

//==================================================================================================================================================================
// File transfer
// const strItemTypeActionButton startfileTransferSetup 		{ startFileTransferMode, nullptr};
// cMenuItem melStartFileTransfer	(menuFileTransfer, "Start file transfer",	nullptr,
// menuTypeItemActionButton, &startfileTransferSetup);

//==================================================================================================================================================================
// Credits & Info

const strItemTypeActionButton creditsSetup{ openCreditsAction, nullptr };
const strItemTypeActionButton manual1Setup{ openManual1Action, nullptr };
const strItemTypeActionButton manual2Setup{ openManual2Action, nullptr };
const strItemTypeActionButton manual3Setup{ openManual3Action, nullptr };

cMenuItem melOpenCredits(menuCredits,
                         "About",
                         nullptr,
                         menuTypeItemActionButton,
                         &creditsSetup);

cMenuItem melOpenManual1(menuCredits,
                         &manualFolderNames[0][0],
                         nullptr,
                         menuTypeItemActionButton,
                         &manual1Setup);

cMenuItem melOpenManual2(menuCredits,
                         &manualFolderNames[1][0],
                         nullptr,
                         menuTypeItemActionButton,
                         &manual2Setup);

cMenuItem melOpenManual3(menuCredits,
                         &manualFolderNames[2][0],
                         nullptr,
                         menuTypeItemActionButton,
                         &manual3Setup);


//==================================================================================================================================================================
// Function keys binding
#ifdef TRACKER_MINI

const strItemTypeListTextWithAction setupFunctionKeys1{ &mtConfig.interface.functionKeysBinding[0],
	                                          kMenuModulesCount,  kModuleNamesList, setFunctionKey1 };
const strItemTypeListTextWithAction setupFunctionKeys2{ &mtConfig.interface.functionKeysBinding[1],
                                              kMenuModulesCount,  kModuleNamesList, setFunctionKey2 };
const strItemTypeListTextWithAction setupFunctionKeys3{ &mtConfig.interface.functionKeysBinding[2],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey3 };
const strItemTypeListTextWithAction setupFunctionKeys4{ &mtConfig.interface.functionKeysBinding[3],
	                                          kMenuModulesCount,  kModuleNamesList, setFunctionKey4 };
const strItemTypeListTextWithAction setupFunctionKeys5{ &mtConfig.interface.functionKeysBinding[4],
	                                          kMenuModulesCount, kModuleNamesList, setFunctionKey5 };
const strItemTypeListTextWithAction setupFunctionKeys6{ &mtConfig.interface.functionKeysBinding[5],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey6 };
const strItemTypeListTextWithAction setupFunctionKeys7{ &mtConfig.interface.functionKeysBinding[6],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey7 };
const strItemTypeListTextWithAction setupFunctionKeys8{ &mtConfig.interface.functionKeysBinding[7],
                                              kMenuModulesCount, kModuleNamesList, setFunctionKey8 };

cMenuItem melFunctionButtons1(menuFunctionButtons,
                              "Fn1",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys1);
cMenuItem melFunctionButtons2(menuFunctionButtons,
                              "Fn2",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys2);
cMenuItem melFunctionButtons3(menuFunctionButtons,
                              "Fn3",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys3);
cMenuItem melFunctionButtons4(menuFunctionButtons,
                              "Fn4",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys4);
cMenuItem melFunctionButtons5(menuFunctionButtons,
                              "Fn1+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys5);
cMenuItem melFunctionButtons6(menuFunctionButtons,
                              "Fn2+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys6);
cMenuItem melFunctionButtons7(menuFunctionButtons,
                              "Fn3+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys7);
cMenuItem melFunctionButtons8(menuFunctionButtons,
                              "Fn4+Shift",
                              setupFunctionKeysDesc,
							  menuTypeItemListTextWithAction,
                              &setupFunctionKeys8);

#endif

// clang-format on

//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================
//=====================================================================================================================================

#include "configMenu_features.inc"

#include "configMenu_usb.inc"

#include "configMenu_featrefresh.inc"

hMenuItem activeSubmenu;

void cConfigEditor::createConfigMenu()
{
    midiMenuSync();
#ifdef FEATURE_FF_CORE
    projectPageSetup();
#else
    menuBase.setCount(baseMenuGroupCount - usbMenuSync());
#endif
    activeSubmenu = menuBase.getSelChild();

    createMenuBaseList();
    refreshConfigMenu();
}

void cConfigEditor::hideConfigMenu()
{

    display.setControlHide(configLabel);

    display.setControlHide(configListControl);
    display.setControlHide(configBasemenuListControl);
    display.setControlHide(configSubmenuListControl);
    display.setControlHide(frameControl);
    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(blankSpaceCover[1]);

    display.refreshControl(frameControl);
}

void cConfigEditor::createMenuBaseList()
{
    basemenuList.linesCount = 14;
    basemenuList.start = menuBase.getSelectedItem();
    basemenuList.length = menuBase.getCount();
    basemenuList.data = menuBase.getNames();
    FF_CFG_PROJECT_PAGE_LIST(basemenuList); // the Project Settings page: only the page itself

    display.setControlData(configBasemenuListControl, &basemenuList);
    display.refreshControl(configBasemenuListControl);
    display.setControlShow(configBasemenuListControl);
}

void cConfigEditor::reloadSubmenu()
{
    cMenuGroup* submenu = (cMenuGroup*)activeSubmenu;

    submenuList.linesCount = 14;
    submenuList.start = submenu->getSelectedItem();
    submenuList.length = submenu->getCount();
    submenuList.params = submenu->getNames();
    submenuList.values = submenu->getValues();
    submenuList.valueEditActive = 0;

    if (submenu->parentMenu != &menuBase || projectPage)
        changeLabelText(6, "Back");
    else
        changeLabelText(6, "");

    display.setControlData(configSubmenuListControl, &submenuList);
    display.refreshControl(configSubmenuListControl);
    display.setControlShow(configSubmenuListControl);
}

void cConfigEditor::loadConfigTextList(strItemTypeListText* itemSetup)
{
    changeLabelText(7, "Apply");
    changeLabelText(6, "Cancel");

    showConfigList4(2, *itemSetup->value, itemSetup->count, (char**)itemSetup->ptrText);
}

void cConfigEditor::loadConfigValuesList(strItemTypeListValues* itemSetup)
{
    changeLabelText(7, "Apply");
    changeLabelText(6, "Cancel");

    showConfigList4Val(itemSetup->value,
                       itemSetup->min,
                       itemSetup->max,
                       itemSetup->interval,
                       itemSetup->dev,
                       itemSetup->zeroText);
}

// wyjdz do menu wyzszego poziomu
bool cConfigEditor::menuGoOut()
{
    // tylko jesli nadrzede menu jest inne niz bazowe
    if (((cMenuGroup*)activeSubmenu)->parentMenu != &menuBase)
    {
        activeSubmenu = ((cMenuGroup*)activeSubmenu)->parentMenu;

        refreshConfigMenu();

        return true;
    }

    return false;
}

// wejdz do nizszego podmenu
void cConfigEditor::menuGoIn()
{
    // wykonaj akcje na zaznaczonej pozycji wybranej listy
    if (((cMenuGroup*)activeSubmenu)->type == menuTypeGroup)
    {
        // jesli to kolejna grupa to ustaw ja jako aktywna i odswiez
        if (((cMenuGroup*)activeSubmenu)->getSelChild()->type == menuTypeGroup)
        {
            cMenuGroup* parent = (cMenuGroup*)activeSubmenu;
            if (parent->onEnter)
            {
                parent->onEnter(parent->getSelectedItem());
            }
            activeSubmenu = parent->getSelChild();

            refreshConfigMenu();
        }
        // jesli to element ostateczny przejdz do jego edycji/akcji
        else if (((cMenuGroup*)activeSubmenu)->getSelChild()->type == menuTypeItem)
        {
            ((cMenuGroup*)activeSubmenu)->executeItem();
        }
    }
}

// source = 0 - encoder, = 1 - klawisze pod ekranem
void cConfigEditor::changeMenuListPosition(uint8_t list, int16_t value, uint8_t)
{
    if (projectPage && list == 0)
        return; // the page is the only group

    // sprawdzanie czy taka lista wogole dostepna
    if (list == 1 && menuBase.getSelChild()->type != menuTypeGroup)
        return;

    // wykownywanie akcji na zjechanie/ukrycie itemu listy ActionButton
    uint8_t itemPositionChanged = 0;
    void (*action_funct)(void) = nullptr;
    cMenuItem* selectedMenuItem = nullptr;

    if (itemEditorShown && list == 0)
    {
        itemEditorClose();
    }

    if (list == 0)
    {
        uint8_t start_position = menuBase.selectedItem;

        if (menuBase.selectedItem + value < 0)
            menuBase.selectedItem = 0;
        else if (menuBase.selectedItem + value > menuBase.childsCount - 1)
            menuBase.selectedItem = menuBase.childsCount - 1;
        else
            menuBase.selectedItem += value;

        if (start_position != menuBase.selectedItem)
            itemPositionChanged++;

        activeSubmenu = menuBase.getSelChild();

        display.setControlValue(configBasemenuListControl, menuBase.selectedItem);
        display.refreshControl(configBasemenuListControl);
    }
    else if (list == 1)
    {
        cMenuGroup* submenu = (cMenuGroup*)activeSubmenu;

        uint8_t start_position = submenu->selectedItem;

        uint8_t* item_selected = &submenu->selectedItem;
        uint8_t item_max = submenu->childsCount - 1;

        if (*item_selected + value < 0)
            *item_selected = 0;
        else if (*item_selected + value > item_max)
            *item_selected = item_max;
        else
            *item_selected += value;

        display.setControlValue(configSubmenuListControl, submenu->selectedItem);
        display.refreshControl(configSubmenuListControl);

        if (start_position != submenu->selectedItem)
            itemPositionChanged++;
    }

    if (itemPositionChanged)
    {
        // odswiezenie menu
        if (selectedMenuItem != nullptr)
            selectedMenuItem->resetValueEditState();

        refreshConfigMenu();

        // sprawdzanie czy zmieniono element menu z wczesniejszego typu actionbutton
        if (action_funct != nullptr)
        {
            action_funct();
        }
    }
}

void cConfigEditor::chanegeItemValue(void* selectedMenuItem, int16_t value)
{
    if (selectedMenuItem == nullptr)
        return;
    if (((cMenuItem*)selectedMenuItem)->getItemType() != menuItemTypeValueU8)
        return; // narazie tylko dla U8

    strItemTypeValue8* itemSetup =
      (strItemTypeValue8*)((cMenuItem*)selectedMenuItem)->getItemSetup();

    if (*itemSetup->value + value < itemSetup->min)
        *itemSetup->value = itemSetup->min;
    else if (*itemSetup->value + value > itemSetup->max)
        *itemSetup->value = itemSetup->max;
    else
        *itemSetup->value += value;
}

void cConfigEditor::refreshConfigMenu()
{
    reloadConfigLabels();

    assert(menuBase.getSelectedItem() < menuBase.getCount());
    changeLabelText(0, menuBase.getNames()[menuBase.getSelectedItem()]);

    if (menuBase.getSelChild()->type == menuTypeGroup)
    {
        assert(menuBase.getSelChild()->getSelectedItem() < menuBase.getSelChild()->getCount());
        changeLabelText(
          3, menuBase.getSelChild()->getNames()[menuBase.getSelChild()->getSelectedItem()]);
    }

    if (menuBase.type == menuTypeItem)
    {
        hideSubmenu();
        return;
    }
    else if (menuBase.type == menuTypeGroup)
    {
        reloadSubmenu();
        showSubmenu();

        // ustawianie etykiety przycisku edycji/akcji
        if (((cMenuGroup*)activeSubmenu)->getSelChild()->type == menuTypeItem)
        {
            cMenuItem* selected_child = (cMenuItem*)((cMenuGroup*)activeSubmenu)->getSelChild();

            setLabelByMenuItemType(
              7, selected_child->getItemType(), selected_child->getValueEditState());
        }
        else
        {
            changeLabelText(7, "Enter"); // xxx wejscie do podmenu
        }

        refreshListsColors();
    }

    display.synchronizeRefresh();
}

void cConfigEditor::setLabelByMenuItemType(uint8_t label, menu_item_t type, uint8_t variant)
{
    switch (type)
    {
        case menuItemTypeValueU8:
        {
            if (variant == 0)
                changeLabelText(label, "Enter");
            else if (variant == 1)
                changeLabelText(label, "Confirm");
        }
        break;
        case menuTypeItemListValues: changeLabelText(label, "Enter"); break;
        case menuTypeItemListText: changeLabelText(label, "Enter"); break;
        case menuTypeItemListTextWithAction: changeLabelText(label, "Enter"); break;
        case menuTypeItemLabel: changeLabelText(label, ""); break;
        case menuTypeItemLabelPtr: changeLabelText(label, ""); break;
        case menuItemTypeEmpty: changeLabelText(label, ""); break;
        case menuTypeItemActionButton: changeLabelText(label, "Enter"); break;
        default: changeLabelText(label, ""); break;
    }
}

// edytowanie wartosci elementu menu
void cConfigEditor::itemEditorOpen(menu_item_t type,
                                   const char* title,
                                   const char* description,
                                   const void* setup)
{
    hideSubmenu();

    itemEditorShown = 1;

    showConfigItemLabel(title);
    showConfigItemDesctription(description);

    switch (type)
    {
        case menuItemTypeValueU8:
        {
            configEditor.loadConfigValuesList((strItemTypeListValues*)setup);
            break;
        }
        case menuTypeItemListValues:
        {
            configEditor.loadConfigValuesList((strItemTypeListValues*)setup);
            break;
        }
        case menuTypeItemListText:
        {
            configEditor.loadConfigTextList((strItemTypeListText*)setup);
            break;
        }
        case menuTypeItemListTextWithAction:
        {
            if (setup == &setupDispTheme || setup == &setupDispFont)
            {
                ThemeManager::scanThemesAndFonts();
                refreshThemeAndFontMenuOptions();
            }
            configEditor.loadConfigTextList((strItemTypeListText*)setup);
            break;
        }
        case menuTypeItemActionButton:
        {

            break;
        }

        default: break;
    }
}

void cConfigEditor::itemEditorClose()
{
    itemEditorShown = 0;
    hideConfigList();
    hideConfigItemLabel();
    hideConfigItemDesctription();

    refreshConfigMenu();

    cMenuItem* selected_child = (cMenuItem*)((cMenuGroup*)activeSubmenu)->getSelChild();
    switch (selected_child->getItemType())
    {
        case menuTypeItemActionButton:
        {
            if (((strItemTypeActionButton*)selected_child->getItemSetup())->funct2 != nullptr)
            {
                ((strItemTypeActionButton*)selected_child->getItemSetup())->funct2();
            }
            break;
        }

        default: break;
    }
}

void cConfigEditor::itemEditorApply()
{
    configListConfirm(configEditor.selectedConfigListPosition);

    itemEditorClose();
}

void cConfigEditor::itemEditorChangeValue(int16_t value)
{
    changeConfigListPosition(value);
}

void cConfigEditor::configListConfirm(uint8_t list_pos)
{
    if (((cMenuGroup*)activeSubmenu)->getSelChild()->type != menuTypeItem)
    {
        return;
    }

    cMenuItem* selected_child = (cMenuItem*)((cMenuGroup*)activeSubmenu)->getSelChild();

    switch (selected_child->getItemType())
    {
        case menuItemTypeValueU8:
        {

            break;
        }
        case menuTypeItemListValues:
        {
            strItemTypeListValues* temp_str =
              (strItemTypeListValues*)selected_child->getItemSetup();
            if (list_pos <= temp_str->max - temp_str->min)
                *(temp_str->value) = temp_str->min + list_pos;
            if (temp_str->funct1 != nullptr)
                temp_str->funct1();
            break;
        }
        case menuTypeItemListText:
        {
            strItemTypeListText* temp_str = (strItemTypeListText*)selected_child->getItemSetup();
            if (list_pos < temp_str->count)
                *(temp_str->value) = list_pos;
            break;
        }
        case menuTypeItemListTextWithAction:
        {
            strItemTypeListTextWithAction* temp_str =
              (strItemTypeListTextWithAction*)selected_child->getItemSetup();
            if (list_pos < temp_str->count)
                *(temp_str->value) = list_pos;
            temp_str->funct1();
            FF_CFG_FEATURE_ROW_CHANGED(); // a feature page: the State row shows or hides the params, the Status row follows
            break;
        }
        case menuTypeItemActionButton:
        {

            break;
        }
        default: break;
    }

    saveConfigToEeprom();
}

// wykonywanie akcji/edycji na elemencie menu
void cMenuGroup::executeItem()
{
    if (childs[selectedItem]->type != menuTypeItem)
        return;

    cMenuItem* selected_child = (cMenuItem*)childs[selectedItem];

    switch (selected_child->itemType)
    {
        case menuItemTypeValueU8:
        case menuTypeItemListValues:
        case menuTypeItemListText:
        case menuTypeItemListTextWithAction:
            configEditor.itemEditorOpen(selected_child->itemType,
                                        selected_child->itemName,
                                        selected_child->itemDesc,
                                        selected_child->itemSetup);
            break;
        case menuTypeItemLabel: break;
        case menuTypeItemLabelPtr: break;
        case menuItemTypeEmpty: break;
        case menuTypeItemActionButton:
        {
            if (((strItemTypeActionButton*)selected_child->itemSetup)->funct1 != nullptr)
            {
                ((strItemTypeActionButton*)selected_child->itemSetup)->funct1();
            }

            break;
        }

        default: break;
    }
}

void cMenuGroup::reloadValues()
{
    for (uint8_t i = 0; i < childsCount; i++)
    {
        if (childs[i]->type == menuTypeItem)
        {
            char* valueText = ((cMenuItem*)childs[i])->getValue();

            childsValues[i] = valueText != nullptr ? valueText : (char*)ptrEmptyName;
        }
        else
        {
            const char* v = childValue ? childValue(i) : nullptr;
            childsValues[i] = v != nullptr ? (char*)v : (char*)ptrEmptyName;
        }
    }
}
