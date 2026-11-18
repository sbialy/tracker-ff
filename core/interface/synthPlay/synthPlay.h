#ifndef INTERFACE_SYNTHPLAY_H_
#define INTERFACE_SYNTHPLAY_H_

#ifdef FEATURE_SYNTH_OPEN

#include "core/modulesBase.h"

// The Synth play page ("tracker as a synth", ref/synth-page-design.md): MIDI in over USB or DIN plays one synth part, or in A/B mode two
// (ref/synth-ab-design.md, an equal power morph between them), the sequencer is stopped while the page is open. Entered with Shift + Perform from any page but Performance (Interface::trySynthPlayEntry) or with Perform on Performance page 2 (the third page of the Perform key, Interface::openSynthPlayFrom), left with any page key (Perform comes back to Performance page 1).
// The voice router is core/source/synthPlay.cpp, the page settings are /Features/synthplay.cfg.
constexpr int kSynthPlayColumns = 8;

class cSynthPlay : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cSynthPlay() {}
    ~cSynthPlay() {}

    // Pads, buttons and the encoder (fSynthPlay.cpp)
    void selectColumn(int column);
    void pressColumn(int column);
    void changeValue(int delta);
    void panic();
    void padNote(uint8_t pad, bool down);
    void confirmStop();
    void recPress();                 // REC: start a take, or stop it (the take lands as an instrument)
    bool leaveKey(uint8_t button);   // a page key: false when it must wait for the take to land (it is repeated once the take is saved)
    uint8_t entryInstrument = 0;   // the live instrument at the Shift + Perform press, before the page it leaves can change it
    bool isOpen() const { return entered; }
    bool isConfirming() const { return confirming; }
    int column() const { return selected; }
    int subItem(int column) const { return sub[column]; }   // which item of the column shows (0 the first), the host sim reads it

  private:
    void enter();
    void leave();
    void setFunctions();
    void showAll();
    void showColumn(int column);
    void showVoices();
    void showTitle();
    void showConfirm();
    int voiceCap(int part) const;
    int columnItem(int column) const;
    bool stepBank(int item, int delta);
    void changeSend(int item, int delta);
    int effectiveVoices() const;
    void applyVoices();
    void restorePart(uint8_t p);
    void loadSettings();
    void saveSettings();
    void touchSettings();
    void startTake();
    void endTake();
    bool landTake();
    void recUpdate();
    void recShowPopup();
    void recFinish(const char* line1, const char* line2);
    void showRecTitle();

    hControl label[8];
    hControl bar[8];
    hControl voiceLine;
    hControl frame;
    hControl bgLabel;
    strFrameData frameData;

    uint8_t selected = 0;
    bool entered = false;
    bool confirming = false;
    bool settingsDirty = false;
    uint32_t settingsDirtyMs = 0;
    uint8_t shownChange = 0xFF;
    uint16_t shownCounts = 0xFFFF;   // the sounding voices of the two sides as last drawn
    uint8_t shownMorph = 0xFF;
    uint8_t sub[kSynthPlayColumns] = {};   // the item each column shows (its row of kItems), kept when the page is left
    bool pageUsed = false;           // the page has been entered since the boot: selected, sub and the session values mean something
    uint8_t lastEntry = 0;           // the entry instrument of that entry, the page values are kept for the same one
    bool settingsLoaded = false;     // the defaults went into the values once
    int8_t morphAtEntry = 50;
    uint8_t sideVoices[2] = {};          // the voices the two sides have
    uint8_t savedInstrument = 0;

    // REC (ref/synth-ab-rec.md): the take is the pool tail from recStart, it lands as instrument recSlot
    enum RecState : uint8_t
    {
        kRecIdle,
        kRecTake,       // the interrupt writes the take
        kRecLanding,    // the take is waiting for the file manager, or it is being written to the workspace
    };
    uint8_t rec = kRecIdle;
    uint8_t recSlot = 0;
    uint8_t recPendingKey = 0xFF;    // a page key pressed while the take ran, repeated when it has landed
    bool recFull = false;            // the pool end stopped the take
    bool recQueued = false;          // the write to the workspace has started
    const char* recPopup1 = nullptr; // the popup of a take that landed, shown once the file manager's own progress popup is gone (it hides every popup)
    const char* recPopup2 = nullptr;
    uint16_t recShownSec = 0xFFFF;
    uint32_t recFrames = 0;
    uint32_t recStartBytes = 0;      // the take's offset in the sample memory
    uint32_t recWaitMs = 0;

};

extern cSynthPlay synthPlay;

#endif // FEATURE_SYNTH_OPEN

#endif /* INTERFACE_SYNTHPLAY_H_ */
