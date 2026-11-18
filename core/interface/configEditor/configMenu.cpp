#include "configEditor/configMenu.h"
#include "configEditor/configEditor.h"

#include "configEditor/configMenuActions.h"
#include "ThemeManager.h"
#include "mtSequencer.h"

#include "imageViewer/imageViewerDefs.h"

#include "polyassert.h"

#include <array>

#ifdef FEATURE_EXT_HOOKS
#include "features.h"
#endif

#ifdef TRACKER_MINI
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#endif

// clang-format off

// FEATURE_PLUG_HOST: + the Project Settings page of the File screen (the last group of the base list, hidden in Config)
#ifdef FEATURE_PLUG_HOST
constexpr uint8_t snapGroupCount = 1;
#else
constexpr uint8_t snapGroupCount = 0;
#endif

#ifdef TRACKER_MINI
constexpr uint8_t baseMenuGroupCount = 8 + snapGroupCount;
constexpr uint8_t projectMenuGroupCount = 10;
constexpr uint8_t generalMenuGroupCount = 11;
#elif defined(PLATFORM_IMX)
constexpr uint8_t baseMenuGroupCount = 7 + snapGroupCount;
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
cMenuGroup menuMidi(menuBase, "MIDI", 11 + kSampleTracks);

#ifdef PLATFORM_IMX
cMenuGroup menuUsb(menuBase, "USB", 2);
#elif defined(FEATURE_EXT_HOOKS) && !defined(TRACKER_MINI)
cMenuGroup menuUsb(menuBase, "USB", 4); // the original: Storage (USB_MSC=1), Audio mode, FX in 8+3, Restart now (usbMenuSync)
#endif

#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(TRACKER_MINI)
static uint8_t usbMenuSync(); // 1 while the USB entry is hidden from the base list (Features: USB audio and USB drive both off)
#else
static uint8_t usbMenuSync() { return 0; }
#endif

cMenuGroup menuMetro(menuBase, "Metronome", 5);
#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(TRACKER_MINI)
cMenuGroup menuFeatures(menuBase, "Features", features::kMax);
// one page for every feature: State, up to kMaxParams params, Status, Restart now. Re-pointed in featurePageEnter
cMenuGroup menuFeaturePage(menuFeatures, "Feature", 3 + features::kMaxParams);
#endif
cMenuGroup menuFirmware(menuBase, "Firmware", 4);

#ifdef TRACKER_MINI
cMenuGroup menuFunctionButtons(menuBase, "Fn buttons binding", 8);
#endif

cMenuGroup menuCredits(menuBase, "Info", 4);

#ifdef FEATURE_PLUG_HOST
// The Project Settings page: the settings of project.mtff, opened by the File screen (kOpenProjectSettings) on this one group. The last
// group of the base list, so Config hides it by the count (cConfigEditor::createConfigMenu). Rows: 4 FX Slots, Macro depth, Strum
// volume, Random seed, Reroll seed, Scale snap (a group of its own: one row per track)
#ifdef FEATURE_SYNTH_OPEN
constexpr uint8_t projectPageRows = 6;
#else
constexpr uint8_t projectPageRows = 5;
#endif
cMenuGroup menuProjectPage(menuBase, "Project Settings", projectPageRows);
#endif

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
// Project Settings page (FEATURE_PLUG_HOST): the File screen's Project Settings entry opens the Config editor on menuProjectPage. The rows
// are the settings of project.mtff and what the sequencer keeps with the project
#ifdef FEATURE_PLUG_HOST
static const char* const fxSlotsDesc =
  "Gives every pattern step two more FX slots, FX3 and FX4. Off keeps their data on the card but does not play it. "
  "Not compatible with stock CE and firmware 1.3.x, which play slots 1 and 2 only. Only while the sequencer is stopped.";
static const char* const strumVolTexts[11] = { "0 %", "10 %", "20 %", "30 %", "40 %", "50 %", "60 %", "70 %", "80 %", "90 %", "100 %" };
static const char* const strumVolDesc =
  "How much the notes of a strummed chord fall in volume, in the order they sound. 0 % is an even strum, 100 % the full ramp. "
  "The Strum FX keeps its own speed and direction.";
const strItemTypeListTextWithAction setupFxSlots{ &projFxSlots, 2, onOffStateTexts, setProjFxSlots };
cMenuItem melFxSlots(menuProjectPage, "4 FX Slots", fxSlotsDesc, menuTypeItemListTextWithAction, &setupFxSlots);
#ifdef FEATURE_SYNTH_OPEN
static const char* const macroModeTexts[3] = { "Per patch", "Add", "Scale" };
static const char* const macroModeDesc =
  "The macro depth mode of a synth patch that is added to the project: a new patch, an Init after a Model change and a patch loaded "
  "from the library. Add moves a parameter by the Amount of its whole range, Scale by the room to its end. "
  "Per patch keeps what each patch does today. Patches already in the project never change.";
const strItemTypeListTextWithAction setupMacroMode{ &projMacroMode, 3, macroModeTexts, setProjMacroMode };
cMenuItem melMacroMode(menuProjectPage, "Macro depth", macroModeDesc, menuTypeItemListTextWithAction, &setupMacroMode);
#endif
const strItemTypeListTextWithAction setupStrumVolume{ &projStrumVolume, 11, strumVolTexts, setProjStrumVolume };
cMenuItem melStrumVolume(menuProjectPage, "Strum volume", strumVolDesc, menuTypeItemListTextWithAction, &setupStrumVolume);
// seeded random takes (mtSequencer.h, seqRandom.h): 0 shows Off, 1 to 255 are the seeds
const strItemTypeListValues setupRandomSeed{ &sequencer.randomSeed, 0, 255, 1, 0, setRandomSeedValue, "Off" };
const strItemTypeActionButton setupRerollSeed{ rerollRandomSeedAction, nullptr };
cMenuItem melRandomSeed(menuProjectPage, "Random seed", randomSeedDesc, menuTypeItemListValues, &setupRandomSeed);
cMenuItem melRerollSeed(menuProjectPage, "Reroll seed", rerollSeedDesc, menuTypeItemActionButton, &setupRerollSeed);

// Scale snap: per track, played notes move to the Notes scale and Root note of Config > Project Settings (the pattern keeps what was written)
cMenuGroup menuScaleSnap(menuProjectPage, "Scale snap", kScaleSnapTracks);
static const char* const scaleSnapTexts[kSnapModeCount] = { "Off", "Nearest", "Down", "Up" };
static const char* const scaleSnapDesc =
  "Notes of this track are moved to the Notes scale and Root note of Config > Project Settings while they play. "
  "Nearest takes the closest scale note (a tie goes down), Down the one at or below, Up the one at or above. "
  "The pattern keeps the notes you wrote. The Chromatic scale changes nothing.";
const strItemTypeListTextWithAction scaleSnapSetup[kScaleSnapTracks] = {
    { &scaleSnapMode[0], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[1], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[2], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[3], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[4], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[5], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[6], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[7], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[8], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[9], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[10], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[11], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[12], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[13], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[14], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[15], kSnapModeCount, scaleSnapTexts, setScaleSnap },
};
cMenuItem melScaleSnap1(menuScaleSnap, "Track 1", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[0]);
cMenuItem melScaleSnap2(menuScaleSnap, "Track 2", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[1]);
cMenuItem melScaleSnap3(menuScaleSnap, "Track 3", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[2]);
cMenuItem melScaleSnap4(menuScaleSnap, "Track 4", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[3]);
cMenuItem melScaleSnap5(menuScaleSnap, "Track 5", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[4]);
cMenuItem melScaleSnap6(menuScaleSnap, "Track 6", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[5]);
cMenuItem melScaleSnap7(menuScaleSnap, "Track 7", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[6]);
cMenuItem melScaleSnap8(menuScaleSnap, "Track 8", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[7]);
cMenuItem melScaleSnap9(menuScaleSnap, "Track 9", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[8]);
cMenuItem melScaleSnap10(menuScaleSnap, "Track 10", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[9]);
cMenuItem melScaleSnap11(menuScaleSnap, "Track 11", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[10]);
cMenuItem melScaleSnap12(menuScaleSnap, "Track 12", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[11]);
cMenuItem melScaleSnap13(menuScaleSnap, "Track 13", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[12]);
cMenuItem melScaleSnap14(menuScaleSnap, "Track 14", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[13]);
cMenuItem melScaleSnap15(menuScaleSnap, "Track 15", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[14]);
cMenuItem melScaleSnap16(menuScaleSnap, "Track 16", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[15]);

// the value column of the Scale snap row: how many tracks snap
static const char* projectPageRowValue(uint8_t row)
{
    static char text[12];
    if (row != projectPageRows - 1) return nullptr;
    int n = 0;
    for (uint8_t t = 0; t < kScaleSnapTracks; t++) n += scaleSnapMode[t] != 0;
    if (!n) return "Off";
    snprintf(text, sizeof(text), "%d tracks", n);
    return text;
}

// Config start, before the lists are made: Config hides the page (the last group of the base list), the page shows only itself. The base
// list and the page group keep their selection while the page is open, so Config opens where it was left
void cConfigEditor::projectPageSetup()
{
    static uint8_t configGroup = 0;   // the base list selection of Config, while the page has it
    constexpr uint8_t page = baseMenuGroupCount - 1;
    const uint8_t shown = page - usbMenuSync();   // the groups Config lists: the USB entry is not one of them while it is hidden
    if (menuBase.getCount() == baseMenuGroupCount) menuBase.selectedItem = configGroup < shown ? configGroup : 0;   // the page was open
    menuBase.setCount(shown);
    menuProjectPage.childValue = projectPageRowValue;
    if (!projectPage) return;
    configGroup = menuBase.selectedItem;
    projectPageSync();
    menuBase.setCount(baseMenuGroupCount);
    menuBase.selectedItem = page;
    menuProjectPage.selectedItem = 0;
}
#endif

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

#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(TRACKER_MINI)
//==================================================================================================================================================================
// Features (core/source/features.h): Config > Features lists every feature with its state, Enter opens its page
namespace
{
const char* const kFeatOnOff[] = { "Off", "On" };
strItemTypeLabel featDummySetup{ nullptr };
cMenuItem featRows[3 + features::kMaxParams] = {
#define FEAT_ROW cMenuItem(menuFeaturePage, "", nullptr, menuTypeItemLabel, &featDummySetup)
    FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW, FEAT_ROW,
#undef FEAT_ROW
};
static_assert(3 + features::kMaxParams == 11, "featRows initialisers");
const strItemTypeActionButton featRestartSetup{ features::restartNow, nullptr };
uint8_t featRowFeature[features::kMax];   // Features list row -> feature (a hidden feature has no row)
strItemTypeListTextWithAction featStateSetup{ nullptr, 2, kFeatOnOff, features::changed };
strItemTypeListTextWithAction featEnumSetup[features::kMaxParams];
strItemTypeListValues featIntSetup[features::kMaxParams];
const char* featStatusText = "";
strItemTypeLabelPtr featStatusSetup{ &featStatusText };
int featCurrent = -1;

void featurePageFill()
{
    const int i = featCurrent;
    const feat_desc* d = features::desc(i);
    if (!d) return;
    featStateSetup.value = features::onRef(i);
    featRows[0].rebind("State", d->help ? d->help : d->name, menuTypeItemListTextWithAction, &featStateSetup);
    uint8_t row = 1;
    if (features::on(i))
    {
        for (uint8_t p = 0; p < d->paramCount; p++, row++)
        {
            const feat_param& q = d->params[p];
            if (q.restart & FEAT_PR_ELSEWHERE) { row--; continue; }   // another page edits it (Config > USB)
            if (q.type == FEAT_P_ENUM)
            {
                featEnumSetup[p] = { features::valueRef(i, p), q.enumCount, q.enumText, features::changed };
                featRows[row].rebind(q.name, q.help ? q.help : (d->help ? d->help : d->name), menuTypeItemListTextWithAction, &featEnumSetup[p]);
            }
            else
            {
                featIntSetup[p] = { features::valueRef(i, p), q.min, q.max, 1, uint8_t(q.shown) };
                featRows[row].rebind(q.name, q.help ? q.help : (d->help ? d->help : d->name), menuTypeItemListValues, &featIntSetup[p]);
            }
        }
    }
    featStatusText = features::rowValue(i);
    featRows[row].rebind("Status", d->help ? d->help : d->name, menuTypeItemLabelPtr, &featStatusSetup);
    if (features::restartPending(i))
    {
        // decision D9: the power off of the power button, ending in a restart (the Workspace and the files are saved first)
        row++;
        featRows[row].rebind("Restart now", "Saves the project and restarts the unit, so the change takes effect.",
                             menuTypeItemActionButton, &featRestartSetup);
    }
    for (uint8_t r = 0; r <= row; r++)
    {
        menuFeaturePage.setChild(r, &featRows[r], featRows[r].getName()); // the group lists its own name table
    }
    menuFeaturePage.setCount(row + 1);
}

void featurePageEnter(uint8_t row)
{
    featCurrent = featRowFeature[row < features::kMax ? row : 0];
    featurePageFill();
}

const char* featureRowValue(uint8_t row)
{
    return features::rowValue(featRowFeature[row < features::kMax ? row : 0]);
}
} // namespace

void features::menuBuild()
{
    const int n = features::count();
    uint8_t rows = 0;
    for (int i = 0; i < n; i++)
    {
        if (!features::listed(i)) continue;
        featRowFeature[rows] = uint8_t(i);
        menuFeatures.setChild(rows++, &menuFeaturePage, features::desc(i)->name);
    }
    menuFeatures.setCount(rows);
    menuFeatures.childValue = featureRowValue;
    menuFeatures.onEnter = featurePageEnter;
}

// Config > USB (the original). The entry is listed while USB audio or USB drive is on in Features, a change shows at once (no restart).
// Rows: Storage (drive on), the rows of USB audio that its page leaves out (FEAT_PR_ELSEWHERE: Audio mode, FX in 8+3), Restart now
// (a mode change takes effect at the next start). The values are the feature's own bytes, so the .cfg files and EEPROM byte 7 stay as they were
namespace
{
constexpr uint8_t kUsbParamRows = 2;
#define USB_ROW cMenuItem(menuUsb, "", nullptr, menuTypeItemLabel, &featDummySetup)
cMenuItem usbRows[kUsbParamRows + 1] = { USB_ROW, USB_ROW, USB_ROW };   // + Restart now
#undef USB_ROW
strItemTypeListTextWithAction usbEnumSetup[kUsbParamRows];
bool usbHidden;
uint8_t usbHome = 0xFF;   // the slot of the entry in the base list
}

static uint8_t usbMenuSync()
{
    const int a = features::find("usbaudio");
    const int dr = features::find("usbdisk");
    uint8_t n = 0;
#ifdef FEATURE_USB_MSC
    if (features::on(dr)) menuUsb.setChild(n++, &melUsbStorage, melUsbStorage.getName());
#endif
    if (features::on(a))
    {
        const feat_desc* d = features::desc(a);
        uint8_t r = 0;
        for (uint8_t p = 0; p < d->paramCount && r < kUsbParamRows; p++)
        {
            const feat_param& q = d->params[p];
            if (!(q.restart & FEAT_PR_ELSEWHERE) || q.type != FEAT_P_ENUM) continue;
            usbEnumSetup[r] = { features::valueRef(a, p), q.enumCount, q.enumText, features::changed };
            usbRows[r].rebind(q.name, q.help ? q.help : d->help, menuTypeItemListTextWithAction, &usbEnumSetup[r]);
            menuUsb.setChild(n++, &usbRows[r], usbRows[r].getName());
            r++;
        }
        if (features::restartPending(a))
        {
            usbRows[kUsbParamRows].rebind("Restart now", "Saves the project and restarts the unit, so the change takes effect.",
                                          menuTypeItemActionButton, &featRestartSetup);
            menuUsb.setChild(n++, &usbRows[kUsbParamRows], usbRows[kUsbParamRows].getName());
        }
    }
    menuUsb.setCount(n);
    if (usbHome == 0xFF) usbHome = uint8_t(menuUsb.getSlot());
    const bool hide = !features::on(a) && !features::on(dr);
    if (hide != usbHidden)
    {
        constexpr uint8_t last = baseMenuGroupCount - 1 - snapGroupCount;   // the page group of Project Settings stays the last
        if (hide) menuBase.moveChild(usbHome, last);
        else menuBase.moveChild(last, usbHome);
        usbHidden = hide;
    }
    return hide ? 1 : 0;
}

// the State row changed the row count, the page shows the new set at the refresh that follows the confirm
void featurePageRefresh()
{
    if (featCurrent >= 0) featurePageFill();
    usbMenuRefresh();
}

// the USB entry and its rows follow USB audio and USB drive (a change in Features, Reset config)
void usbMenuRefresh()
{
    menuBase.setCount(baseMenuGroupCount - snapGroupCount - usbMenuSync());
}
#endif

hMenuItem activeSubmenu;

void cConfigEditor::createConfigMenu()
{
#ifdef FEATURE_PLUG_HOST
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
#ifdef FEATURE_PLUG_HOST
    if (projectPage)   // only the page itself
    {
        basemenuList.start = 0;
        basemenuList.length = 1;
        basemenuList.data += menuBase.getSelectedItem();
    }
#endif

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
#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(TRACKER_MINI)
            if (activeSubmenu == &menuFeaturePage || activeSubmenu == &menuUsb)
            {
                featurePageRefresh(); // the State row shows or hides the params, the Status row follows, the USB entry and its rows
                createMenuBaseList();
            }
#endif
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
