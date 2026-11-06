#ifndef SOURCE_PLATFORMIMPL_H_
#define SOURCE_PLATFORMIMPL_H_

#include "BatteryImpl.h"
#include "ButtonsImpl.h"
#include "ConfigManagerImpl.h"
#include "CrcCalculatorImpl.h"
#include "DisplayDriverImpl.h"
#include "EncoderImpl.h"
#include "FileSystemImpl.h"
#include "LedsImpl.h"
#include "PadsImpl.h"
#include "Platform.h"
#include "PowerControl.h"
#include "RadioImpl.h"
#include "Sgtl5000Impl.h"
#include "TimerImpl.h"
#include "UartMidiImpl.h"
#include "UsbAudioImpl.h"
#include "UsbMidiImpl.h"
#include <UsbImpl.h>

class PlatformImpl final : public Platform
{
  public:
    virtual void setPowerMode(bool mode) override;
    virtual void resetMCU() override;
    virtual void runBootloader(char* fileName) override;
    virtual void setPowerButtonCallback(PowerButtonCallback) override;
    virtual bool getPowerButtonState() override;
    virtual bool readForceStartUp();

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
    virtual UsbMidi* getUsbMidi() override;
    virtual UsbAudio* getUsbAudio() override;
    virtual ConfigManager* getConfigManager() override;
    virtual FileSystem* getFileSystem() override;
    virtual CrcCalculator* getCrcCalculator() override;
    virtual SystemInfo* getSystemInfo() override;
    virtual Usb* getUsb() override;

    void init();
    void update();

    bool getPowerMode();
    uint8_t readStartState();
    void saveStartState(uint8_t state);
    PowerButtonCallback getPowerButtonCallback();
    PowerControl* getPowerControl() { return &powerControl; };
    FileSystemImpl* getFileSystemImpl() { return &fileSystem; }

  protected:
    PowerButtonCallback powerButtonCallback;
    DisplayDriverImpl displayDriver;
    TimerImpl timer;
    Sgtl5000Impl codec;
    ButtonsImpl buttons;
    PadsImpl pads;
    CrcCalculatorImpl crcCalculator;
    BatteryImpl battery;
    ConfigManagerImpl configManager;
    UsbAudioImpl usbAudio;
    UsbMidiImpl usbMidi;
    UartMidiImpl uartMidi;
    FileSystemImpl fileSystem;
    LedsImpl leds;
    EncoderImpl encoder;
    RadioImpl radio;
    PowerControl powerControl;
    UsbImpl usb;

    static SystemInfo systemInfo;
};

#endif /* SOURCE_PLATFORMIMPL_H_ */
