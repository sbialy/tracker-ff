
#ifndef INTERFACE_PATTERNEDITOR_H_
#define INTERFACE_PATTERNEDITOR_H_

#include "core/modulesBase.h"

#include "mtKeyboardManager.h"
#include "mtSequencer.h"
#include "fx34.h"

uint8_t isMultiSelection();
uint8_t isMultiSelectionOnOneLine();
int16_t getActualStep();
int16_t getActualTrack();

uint8_t isEditMode();
void sendSelection();
void sendCopySelection();
void sendPasteSelection();
void changeSelectedStepValue(int16_t value);

class cPatternEditor : public ModuleBase
{

  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    virtual void hideControlsUnderMiniMenu();

    void refreshLastUsedInstrument();

    cPatternEditor();
    virtual ~cPatternEditor() {}

    //----------------------------------

    void showDefaultScreen();
    void setDefaultScreenFunct();
    void setSwitchModuleFunct();

    void setNotePopupFunct();
    void setInstrPopupFunct();
    void setVolPopupFunct();
    void setFxListPopupFunct();

    void setKeyboardExportFunctions();
    void setOverwriteRenderFunct();

    void setKeyboardNamePatternFunctions(void (*confirmFunct)(void), bool autoNameEnabled);

    void focusOnActual();
    void moveCursorByStep();
    void moveCursorByStep(uint8_t);
    void moveCursorByStepParallel();

    void refreshPatternFromOutsideModule();
    void refreshPattern();
    void readPatternState();
    void setPlayheadRangeFromSelection();

    void refreshEditStateFromOutsideModule();
    void refreshEditState();

    void focusOnPattern();
    void unfocusPattern();

    void changeActualTempo(int16_t value);
    void changeActualPattern(int16_t value);
    void setActualPattern(int16_t value);
    void changeActualPatternLength(int16_t value);
    void changeActualPatternLengthByDefinedValues(int16_t value);
    void setActualPatternLength(int16_t value);
    void changeActualTrackLength(int16_t value, bool definedValues);
    void changeActualPatternEditStep(int16_t value);
    void changeActualPatternEditStepByDefinedValues(int16_t value);
    void setActualPatternEditStep(int16_t value);

    void changePlaceTwo(int16_t value, bool byDefinedValues = false);
    void changeTrackGroove(int16_t value);
    void showGrooveLabel();
    char grooveText[8];
    void changeFillData(int16_t value, bool rollListOver = false);
    void changeRandomiseData(int16_t value);
    void setFillData(uint16_t value);
    void setRandomiseData(uint16_t value);

    void changneFillDataByPad(uint8_t pad);
    void changneRandomiseDataByPad(uint8_t pad);

    void setStepButtonState(uint8_t button, uint8_t state);
    void showNotePopup();
    void showInstrumentPopup();
    void showFx1Popup();
    void showFx2Popup();

    void loadPatternsStatus();
    void lightUpPadBoard();
    void clearPadBoard();

    void showMuteButtons(uint8_t state, bool midiTracks);
    void showRecordArmingButtons(uint8_t state, bool midiTracks);
    void setMuteFunct(uint8_t state);
    void setRecArmingFunct(uint8_t state);
    void setProjectSaveFlags();

    void previousTrack();
    void nextTrack();
    void previousStep();
    void nextStep();
    void changeActualStep(uint8_t direction);
    //----------------------------------

    void activateLabelsBorder();
    void activateFillPopupBorder();
    void activateRandomisePopupBorder();
    void changeFillDataByActionButton(uint8_t place);
    bool isFillPlaceValid(uint8_t place);
    void setFillPlace(uint8_t place, int8_t dir = 0);
    void changeFillPlace(int8_t diff);
    void refreshFillPlace();

    void showPattern(uint8_t forceShow = 0, uint8_t shiftPressed = 0);
    void showLength(uint8_t forceShow = 0);
    void showStep();

    void refreshPatternParams();
    void showFxInfo();

    void showEditModeLabels();
    void hideEditModeLabels();

    void fillInstrumentNames();

    void showFillNote();
    void showFillInstr();
    void showFillFx();
    void showFillMicVol();

    void showFillPopup();
    void refreshFillType();
    void refreshFillFrom();
    void refreshFillTo();
    void refreshFillParam();
    void refreshFillStep();
    void refreshFillDensity();
    void hideFillPopup();

    void activateGreenSelection();
    void activateRedSelection();
    void deactivateSelection();
    void playheadRecMode();
    void playheadNormalMode();

    void startExportSelection();
    void endExportSelection();

    void showKeyboardExport();
    void showKeyboardNameEdit(const char* title, bool autoNameEnabled);
    void hideKeyboardExport();
    void showOverwriteExportDialog();
    void hideOverwriteExportDialog();
    void showExportProgress();
    void refreshExportProgress();
    void hideExportProgress();
    void showFullMemoryInBank();
    void showFullInstrumentInBank();

    //----------------------------------
    void exportSelection();
    void setClockWarningScreenFunct();
    void showClockWarning();
    void setPreviousUsedFx(uint8_t newValue);
    uint8_t getPreviousUsedFx();
    void setPreviousUsedFxValue(uint8_t newValue);
    uint8_t getPreviousUsedFxValue();

    uint8_t saveClockConfig;
    //----------------------------------
    // Funct
    uint16_t lastPatternPosition;
    uint8_t refreshPlayheadOnScreen();

    uint16_t lastShownPatternOnTitleBar = 0;
    uint16_t lastShownPattern = 0;
    uint16_t lastShownPattern_blinkState = 0;

    uint16_t lastLength = 0;

    uint8_t isCursorInSelection();
    bool isSelectionOrCursorInMidiTracks();

    Sequencer::strPattern* seq;

    static strTrackerPattern trackerPattern;

    uint8_t editMode = 0;
    enum EditParam
    {
        EditNote = 0,
        EditInstr,
        EditFx1,
        EditFx2
    };

    enum PatternArrangement
    {
        kVerticalRevolver = 0,
        kVerticalOriginal = 1,
        kHorizontal = 2,

        kPatternArrangementCount = 3,
    };

    static constexpr uint16_t kStepNotePopupShowDelay = 500;
    static constexpr uint16_t kStepPopupShowDelay = 180;

    uint8_t editParam = 0; // 0 note, 1 instrument, 2 fx1, 3 fx 2
    uint8_t previousEditParam = 0;
    uint8_t previousFxValue = 0;
    uint8_t isSelectingNow = 0;

    uint8_t padsTempData[48];

    uint8_t currentExportState;
    uint8_t lastExportState;
    bool isLoadAfterSave;
    int8_t firstFreeInstrumentSlotFound;

    uint16_t lastPatternNumber =
      0xFFF; // do weryfikowania dezaktywacji zaznaczenia w przypadku zmiany patternu
    uint16_t lastPatternLength = 0xFFF;

    //----------------------------------
    // GUI

    hControl label[8];
    hControl bgLabel;
    hControl patternControl;
    hControl arrangementControl;
    hControl frameControl;
    hControl verticalPatternControl;
    hControl horizontalPatternControl;

    // popups
    hControl patternPopupLabel;
    hControl patternPopupTitleLabel;
    hControl param1PopupListControl;
    hControl val1PopupBar;
    hControl val2PopupBar;
    hControl val3PopupBar;
    hControl valCancelEmptyPopupBar;
    hControl valFillEmptyPopupBar;
    hControl intstrumentsFromPopupListControl;
    hControl intstrumentsToPopupListControl;
    hControl param3PopupListControl;
    hControl param2PopupListControl;

    strLabelData labelDataRed;
    strLabelData labelDataGreen;
    strLabelData labelDataMore;

    strFrameData frameData;

    int8_t selectedPlace = -1; // przytrzymany przycisk pod ekranem
    bool undoRedoOpen = 0;     // true jesli operujemy jogiem na undoRedo

    // parametry
    char cTempo[7];
    char cPattern[5];
    char cLength[8];
    char cStep[5];
    char cTitlePattern[PATTERN_NAME_LENGTH + 1];
    char lastStepVmText[16] = { 0 };   // the "V100 m12" the title bar shows now, to redraw it only when it changes
    void showStepVmOnBar();

    bool refreshAfterReleaseFx;

    uint64_t patternOccupyMap;

    //------------------------------------------------------
    // fill
    uint8_t fillState = 0;
    uint8_t undoRedoButtonReleaseBlocker =
      0; // block release after return to patternEditor default view

    int8_t fillPlace = 0;
    //
    strList fillTypeList;
    //
    strList fillScaleFilterList;
    //
    strList fillFxTypeList;
    //
    strList fillEmptyList;
    //
    strList fillStepList;
    //
    strList fillInstrumentsFromList;
    strList fillInstrumentsToList;
    //
    static constexpr int fillTextLen = 16; // 10 before: a MIDI Chord value shows its name ("M7sus4 inv3")
    char fillText1[fillTextLen];
    char fillText2[fillTextLen];
    char fillTextDensity[10];
    char fxTypeText[2];
    //
    struct strFill
    {
        int16_t fillStep;
        int16_t type;
        int16_t from;
        int16_t to;
        int16_t param;
        int16_t density[Sequencer::fillStepEnd__];
        int16_t role;     // Fill Type Role: patternGen::Role, 0 = Kick
        int16_t quantize; // strength percent of the Quantize fill type (Micro-move column only), 100 after init
        // "Constant" and "Quantize" take one value (From), the other types two. Type 3 of the Note column is Role (idea-gen),
        // it takes no From/To value either (isFillRole is checked first where a role differs)
        bool oneValue() const { return type == 0 || type == 3; }
    };
    static strFill fillData[6];
    // Fill Type Role (Note column only): a generated drum, bass or lead idea instead of a value fill, see mtPatternGen.h
    static constexpr int16_t fillTypeRole = 3;
    bool isFillRole() const
    {
        return editParam == EditNote && fillData[EditNote].type == fillTypeRole;
    }
    // the density of the Fill: a role uses the one of the Random mode whatever the Where? list says (the list is hidden then)
    uint8_t fillDensityStep(uint8_t fillIdx) const
    {
        return isFillRole() ? uint8_t(Sequencer::fillStepRandom) : uint8_t(fillData[fillIdx].fillStep);
    }
    // the Quantize fill type: the Micro-move column of the mic/vol view, type 3 of its list
    bool isQuantizeFill() const { return isMicVolView && editParam == EditFx1 && fillData[getFillDataIdx()].type == 3; }
    uint8_t getFillDataIdx() const
    {
        return (isMicVolView && editParam >= EditFx1) ? (editParam + 2) : editParam;
    }
    void initExternalRamState();

    bool isMicVolView = false;
    bool toggledMicVolOnPress = false;

    // The "4 FX" project switch (fx34.h): the two FX columns show the slots 1/2 (bank 0) or 3/4 (bank 1). A second press of the
    // FX1 or FX2 button flips the bank, the same gesture the Note button has for the Mic/Vol view. The Mic/Vol view has no bank.
    uint8_t fxBank = 0;
    bool fxBankTogglePending = false;   // the FX button went down on its own column: a clean tap flips the bank when it comes up
    bool fxBankActive() const { return fxBank != 0 && fx34::enabled() && !isMicVolView; }
    // The slot rank (fx34.h: 0 FX2, 1 FX1, 2 FX4, 3 FX3) a column shows. Ranks are what the sequencer indexes by.
    uint8_t fxRankOf(uint8_t param) const { return uint8_t((param == EditFx1 ? 1 : 0) + (fxBankActive() ? 2 : 0)); }
    uint8_t fxRank() const { return fxRankOf(editParam); }
    // the 3 track layout with V and m columns (Config > Pattern V/m columns) is on screen
    bool vmColumnsOn() const;
    // the cursor step's volume and micro-move as the label of the title bar (empty when the step has neither)
    void makeStepVmText(char* out, size_t outSize) const;

    // int16_t fillStep = 1;

    //-------------------------------------------------------
    // popups
    void cancelPopups();

    uint8_t getStepFx();
    int8_t getStepVol();

    uint8_t dontShowPopupsUntilButtonRelease = 0;
    uint8_t insertOnPopupHideDisabled = 0;

    uint8_t wasNotesEditBefore = 0;

    uint8_t stepButtonsState[4] = { 0, 0, 0, 0 };
    elapsedMillis stepButtonsTimer[4];

    //-------------------------------------------------------
    // pojedyncze parametry / 8 trackow
    uint8_t patternViewMode = 0; // przechowuje bitowo pokazywane parametry dla 8 trackow
    uint8_t patternViewChanged = 0;
    uint8_t selectedParams[2];
    uint8_t patternButtonReleaseActive = 0;

    void setPatternViewMode(uint8_t param);
    void changePatternViewMode(uint8_t param);

    //--------------------------------------------------------
    // powiekszona funkcjolanosc przyciskow pod ekranem - 2 strony
    const uint8_t actionButtonsPagesCount = 3;
    uint8_t actualActionButtonsPage = 0;

    void setActionButtonPage1Funct();
    void setActionButtonPage2Funct();
    void setActionButtonPage3Funct();
    void refreshActualActionButtonPage();

    void showActionButtonPage1Labels();
    void showActionButtonPage2Labels();
    void showActionButtonPage3Labels();
    // Follow chord (tracker-ff, mtFollowChord.h): F3 mode and F4 chord track of the cursor track on the third action page
    void showFollowChordLabels();
    int8_t followShownTrack = -1;
    char followModeText[10];
    char followSourceText[10];

    //--------------------------------------------------------
    // title bar
    void showTitleBar();
    void showTitleBarPattern();
    void showActualParamOnBar(bool forceShow = false);
    void hideActualParamOnBar();

    uint8_t lastTitleBarParamValue = 0xFF;
    uint8_t lastTitleBarFx = 0xFF;
    uint8_t lastTitleBarParam = 0xFF;
    char rightTitleBarLabel[40];
};

extern cPatternEditor patternEditor;

#endif /* INTERFACE_PATTERNEDITOR_H_ */
