#ifndef SOURCE_PLATFORMIMPL_H_
#define SOURCE_PLATFORMIMPL_H_

#include "BatteryImpl.h"
#include "ButtonsImpl.h"
#include "ConfigManagerImpl.h"
#include "CrcCalculatorImpl.h"
#include "DisplayDriverImpl.h"
#include "EncoderImpl.h"
#include "FileSystemImpl.h"
#include "IntervalTimer.h"
#include "LedsImpl.h"
#include "PadsImpl.h"
#include "Platform.h"
#include "RadioImpl.h"
#include "Sgtl5000Impl.h"
#include "Snooze.h"
#include "TimerImpl.h"
#include "UartMidiImpl.h"
#include "UsbAudioImpl.h"
#include "UsbMidiImpl.h"
#include "UsbImpl.h"

class PlatformImpl final : public Platform
{
  public:
    virtual void setPowerMode(bool mode) override;
    virtual void resetMCU() override;
    virtual void setPowerOffGuard(bool armed) override;
    virtual void kickPowerOffGuard() override;
    virtual void runBootloader(char* fileName) override;
    virtual void setPowerButtonCallback(PowerButtonCallback) override;
    virtual bool getPowerButtonState() override;
    virtual bool readForceStartUp() override;

    virtual Sgtl5000* getCodecAudio() override;
    virtual Radio* getRadio() override;
    virtual SwitchGroup* getButtons() override;
    virtual SwitchGroup* getPads() override;
    virtual Leds* getLeds() override;
    virtual Battery* getBattery() override;
    virtual Timer* getTimer() override;
    virtual Encoder* getEncoder() override;
    virtual DisplayDriver* getDisplayDriver() override;
    virtual UartMidi* getUartMidi() override;
    virtual UsbAudio* getUsbAudio() override;
    virtual UsbMidi* getUsbMidi() override;
    virtual Usb* getUsb() override;
    virtual ConfigManager* getConfigManager() override;
    virtual FileSystem* getFileSystem() override;
    virtual CrcCalculator* getCrcCalculator() override;
    virtual SystemInfo* getSystemInfo() override;

    void init();
    void update();

    inline bool getPowerMode()
    {
#ifndef TRACKER_MINI
        return Snooze.getMode();
#endif
        return true;
    }
    uint8_t readStartState();
    void saveStartState(uint8_t state);
    PowerButtonCallback getPowerButtonCallback();

  protected:
    PowerButtonCallback powerButtonCallback;
    Sgtl5000Impl codecAudio;
    RadioImpl radio;
    PadsImpl pads;
    ButtonsImpl buttons;
    LedsImpl leds;
    BatteryImpl battery;
    TimerImpl timer;
    EncoderImpl encoder;
    DisplayDriverImpl displayDriver;
    UsbAudioImpl usbAudio;
    UartMidiImpl uartMidi;
    UsbMidiImpl usbMidi;
    UsbImpl usb;
    ConfigManagerImpl configManager;
    FileSystemImpl fileSystem;
    CrcCalculatorImpl crcCalculator;

    SnoozeClass Snooze;

    static SystemInfo systemInfo;
};

#endif /* SOURCE_PLATFORMIMPL_H_ */
