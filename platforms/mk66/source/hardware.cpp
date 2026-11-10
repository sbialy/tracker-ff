#include "AudioHardware.h"
#include "BlinkLed.h"
#include "ConfigManagerImpl.h"
#include "Drivers/ClockCyclesCounter.h"
#include "Drivers/ExternalSdram.h"
#ifdef FW_SDRAM_FILE_POOLS
void operatorNewArenaInit();
#endif
#include "FT812.h"
#include "PlatformImpl.h"
#include "PowerControl.h"
#include "core_pins.h"
#include "i2cBus.h"
#include "i2cBusControllerNXP.h"
#include "pinout.h"
#include "sdram.h"
#ifdef FEATURE_USB_AUDIO
#include "usb_audio_ff.h"
#endif

static PlatformImpl* platformImpl;

void onButtonPush(uint8_t n);
void onButtonRelease(uint8_t n);
void onButtonHold(uint8_t n);
void onPadPush(uint8_t n);
void onPadRelease(uint8_t n);
void onPadHold(uint8_t n);
void muteAudioOutByKey();
void onBatteryCharging();
void onBatteryEmpty();
void onBatteryLow();
void onSDCardSlotChange(uint8_t);

bool hardwareInitialized = false;

void initHardware(PlatformImpl* platform,
                  const char* version,
                  void (*updateAudioBuffersFunc)(void* left, void* right),
                  void (*updateAudioInputFunc)(void* data),
                  void (*startDisplayFunc)(Platform*))
{
    (void)version;
    platformImpl = platform;
    hardwareInitialized = false;

    ((ConfigManagerImpl*)platform->getConfigManager())->init();
    checkPowerState(platformImpl);

    muteAudioOutByKey();

    Serial.begin(9600);

    i2cBusControllerNXP0.init();
    i2cBusControllerNXP0.setTimeout(1000);

#ifndef TRACKER_MINI
    i2cBusControllerNXP2.init();
    i2cBusControllerNXP2.setTimeout(1000);
#endif

    pinMode(TrackerPinout::kExternalRamEnable, OUTPUT);
    digitalWrite(TrackerPinout::kExternalRamEnable, HIGH);
    externalRAM.init();
#ifdef FEATURE_USB_AUDIO
    ffUsbAudioStart(); // the USB audio ring lives in SDRAM: cleared here, before the audio block or a packet uses it
#endif
#ifdef FW_SDRAM_FILE_POOLS
    operatorNewArenaInit(); // new.cpp: the big blocks of operator new go to the SDRAM from here on
#endif

    clockCyclesCounter.init();

    // Disable NXP System MPU to permit code execution from SRAM
    *(volatile uint32_t*)0x4000D000 = 0;

    DisplayDriverImpl* displayDriver = (DisplayDriverImpl*)platform->getDisplayDriver();
    displayDriver->init();

#ifdef TRACKER_MINI
    BatteryImpl* battery = (BatteryImpl*)platform->getBattery();
    bool battery_empty = battery->begin();
    bool battery_charging = battery->getCharging();

    if (battery_empty && !battery_charging)
    {
        platformImpl->setPowerMode(false);
        return;
    }

    battery->setChargingAction(onBatteryCharging);
    battery->setEmptyBatteryAction(onBatteryEmpty);
    battery->setLowBatteryAction(onBatteryLow);
#endif

    // handle quick activation of display, with logo shown
    // requires the display and battery drivers to be activated in advance
    if (startDisplayFunc != nullptr)
    {
        startDisplayFunc(platform);
    }

    audioHardware.init(platform->getCodecAudio(),
                       TrackerPinout::kAudioOutputEnable,
                       updateAudioBuffersFunc,
                       updateAudioInputFunc);

#ifndef TRACKER_MINI
    RadioImpl* radio = (RadioImpl*)platform->getRadio();
    RadioImpl::Pins radioPins;
    radioPins.reset = TrackerPinout::kRadioReset;
    radioPins.i2sData = TrackerPinout::kI2cData0;
    radioPins.i2sClock = TrackerPinout::kI2cClock0;
    radioPins.interrupt = TrackerPinout::kRadioInterrupt;
    radioPins.lineInSourceSelect = TrackerPinout::kRadioInputKey;

    radio->init(&radioPins);
#endif

    ButtonsImpl* buttons = (ButtonsImpl*)platform->getButtons();
    buttons->setActionCallback(SwitchGroup::ActionType::kPress, onButtonPush);
    buttons->setActionCallback(SwitchGroup::ActionType::kRelease, onButtonRelease);
    buttons->setActionCallback(SwitchGroup::ActionType::kHold, onButtonHold);
    buttons->init(TrackerPinout::kButtonsInterrupt, &i2cBusControllerNXP0, restI2C0);

#ifndef TRACKER_MINI
    PadsImpl* pads = (PadsImpl*)platform->getPads();
    pads->setActionCallback(SwitchGroup::ActionType::kPress, onPadPush);
    pads->setActionCallback(SwitchGroup::ActionType::kRelease, onPadRelease);
    pads->setActionCallback(SwitchGroup::ActionType::kHold, onPadHold);
    pads->init(TrackerPinout::kPadsInterrupt, &i2cBusControllerNXP2, restI2C2);

    LedsImpl* leds = (LedsImpl*)platformImpl->getLeds();

    leds->init(restI2C2);
#endif

    EncoderImpl* encoder = (EncoderImpl*)platform->getEncoder();
    encoder->init();

    BlinkLed.begin(TrackerPinout::kBlinkLed);

    UartMidiImpl* uartMidi = (UartMidiImpl*)platform->getUartMidi();
    uartMidi->init();

    // SD card
    ((FileSystemImpl*)platform->getFileSystem())
      ->init(TrackerPinout::kSdCardDetect, platform->getTimer());

    hardwareInitialized = true;
}

void updateHardware()
{
    TactSwitchRead(platformImpl);

    if (!platformImpl->getPowerMode())
    {
        return;
    }

    externalRAM.update();

    updateI2C((Platform*)platformImpl);

#ifdef TRACKER_MINI
    BatteryImpl* battery = (BatteryImpl*)platformImpl->getBattery();
    battery->update();
#endif

    EncoderImpl* encoder = (EncoderImpl*)platformImpl->getEncoder();
    encoder->update();

    BlinkLed.update();
}

void disableHardware()
{
    if (hardwareInitialized)
    {
        // the audio DMA interrupts render from (and record into) the SDRAM, which goes off below, and detachInterrupt (the buttons
        // and the pads further down) turns the interrupts on again: stop both before the SDRAM is unpowered
        AudioOutputI2S::stop();
        AudioInputI2S::stop();
    }

    // turn off USB
    USB0_INTEN = 0;
    USB0_CTL = 0;

    digitalWrite(TrackerPinout::kBlinkLed, HIGH);
    digitalWrite(TrackerPinout::kExternalRamEnable, LOW); // RAM power off
    BOARD_DeinitPins();                                   // RAM pins deinit

    if (!hardwareInitialized)
    {
        return;
    }

    Sgtl5000Impl* codec = (Sgtl5000Impl*)platformImpl->getCodecAudio();
    codec->powerOff();

    RadioImpl* radio = (RadioImpl*)platformImpl->getRadio();
    radio->enable(false);

    ButtonsImpl* buttons = (ButtonsImpl*)platformImpl->getButtons();
    buttons->disableInterrupts();

#ifndef TRACKER_MINI
    PadsImpl* pads = (PadsImpl*)platformImpl->getPads();
    pads->disableInterrupts();

    LedsImpl* leds = (LedsImpl*)platformImpl->getLeds();
    leds->turnOff();
#endif

    platformImpl->getDisplayDriver()->setPowerState(false);
}

// startup muting
void muteAudioOutByKey()
{
    audioHardware.enableOutput(false);
}

void unmuteAudioOutByKey()
{
    audioHardware.enableOutput(true);
}

uint8_t isAudioOutMuted()
{
    return !audioHardware.isOutputEnabled();
}
