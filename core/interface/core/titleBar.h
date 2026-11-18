#ifndef INTERFACE_CORE_TITLEBAR_H_
#define INTERFACE_CORE_TITLEBAR_H_

#include "commonControls.h"
#include "displayStructs.h"

class Interface;
class Battery;
class Synth;

class TitleBar
{
  public:
    void init(Synth* synth, Battery* battery);
    void initControls();
    void refreshColors();

    void show(bool showRightLabel = false);
    void hide();

    void setTitle(const char* text);
    void setRightLabel(const char* text);
    void showCurrentInstrumentOnRightLabel();
    void setBatteryValue(int valuePercent, bool charging);

    void showRightLabel(bool show);
    void showBattery(bool show);

    // The Capture indicator (Config > Features > Capture), a small square after the title that every screen shows: the state is a
    // CaptureIndicator (mtCapture.h), 0 hides it. Redrawn only when the state changes.
    void setCaptureState(uint8_t state);
    uint8_t captureStateDrawn() const { return captureState; }
    uint16_t captureDraws() const { return captureDrawCount; } // how often the indicator was redrawn, for the host test

    void refresh();

    friend Interface;

  private:
    static constexpr int kPerformance1Treshold = 85;
    static constexpr int kPerformance2Treshold = 93;
    static constexpr int kPerformance3Treshold = 99;
    static constexpr int kBoxHideDelayLoopCount = 3;

    static constexpr int kBatteryNotPresent = -1;
    bool visible = false;

    hControl titleLabel = -1;
    hControl rightLabel = -1;
    hControl bar = -1;
    hControl battery = -1;
    hControl performBox = -1;
    hControl captureBox = -1;

    strLabelData barLabelData;

    char timerText[50];

    Battery* batteryDriver;
    Synth* synth;

    bool lastBatteryCharging = false;
    int lastBatteryLevel = -1;

    uint8_t captureState = 0;
    uint16_t captureDrawCount = 0;
    uint32_t captureColors[4] = {};
    void applyCaptureState();

    uint32_t boxLastColor = 0;
    uint8_t boxShowLoopCount = 0;
};

extern TitleBar titleBar;

#endif /* INTERFACE_CORE_TITLEBAR_H_ */
