#include "PlatformImpl.h"
#include "EEPROM.h"
#include "PowerControl.h"
#include "config.h"
#include "core_pins.h"
#include "pinout.h"
#include "polyassert.h"
#include "sdram.h"

#ifdef DEBUG
#define DEBUG_TEXT " debug"
#else
#define DEBUG_TEXT
#endif

PlatformImpl::SystemInfo PlatformImpl::systemInfo = { BUILD_ID DEBUG_TEXT };

extern void disableHardware();

void PlatformImpl::init()
{
}

void PlatformImpl::update()
{
    if (!getPowerMode())
    {
        return;
    }
    // if (!sequencer.isRenderMode()) // todo: warunek zaimplementowac ; czy potrzebne to?
    //    {
    fileSystem.update();
    //    }
}

void PlatformImpl::setPowerMode(bool mode)
{
#ifndef TRACKER_MINI
    if (mode == true)
    {
        Snooze.wakeUp();
        resetMCU();
    }
    else
    {
        saveStartState(0);

        noInterrupts();

        disableHardware();

        setPowerOffGuard(false); // the teardown is done (display dark), the sleep below polls the power button

        Snooze.sleep();
    }
#else
    if (mode == false)
    {
        disableHardware();
        noInterrupts();

        while (digitalRead(TrackerPinout::kPowerButton) == HIGH)
            ;

        delay(1000);

        pinMode(TrackerPinout::kPowerReset, OUTPUT);
        digitalWrite(TrackerPinout::kPowerReset, HIGH);

        while (1)
        {
            __asm("nop");
        }
    }
#endif
}

// Power-off stall guard: the K66 watchdog, clocked from the 1 kHz LPO. Armed when the power-off sequence starts and kicked on
// every main loop pass of it (Interface::processPowerOffSequence). When the main loop stops coming round (blocked in a call, the
// fault_isr loop, a teardown step that never returns with interrupts off) it resets the MCU after kPowerOffGuardMs. A release
// build has written start state 0 at boot (checkPowerState), so that reset lands in the off state (PowerControl.cpp), where a
// power button press starts the unit as usual. The startup code leaves the watchdog off with ALLOWUPDATE set (mk20dx128.c).
static constexpr uint16_t kPowerOffGuardMs = 3000;

static inline uint32_t irqSave()
{
    uint32_t primask;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __disable_irq();
    return primask;
}

static inline void irqRestore(uint32_t primask)
{
    if (primask == 0)
    {
        __enable_irq();
    }
}

void PlatformImpl::setPowerOffGuard(bool armed)
{
#ifndef TRACKER_MINI
    if (!START_STATE_SAVE)
    {
        return; // debug build: a reset would start the unit again, not power it off
    }
    const uint32_t primask = irqSave();
    // unlock, then the new configuration within the 256 bus clock window (the core waits at least one bus clock first)
    WDOG_UNLOCK = WDOG_UNLOCK_SEQ1;
    WDOG_UNLOCK = WDOG_UNLOCK_SEQ2;
    __asm volatile("nop");
    __asm volatile("nop");
    __asm volatile("nop");
    __asm volatile("nop");
    if (armed)
    {
        WDOG_TOVALH = 0;
        WDOG_TOVALL = kPowerOffGuardMs; // LPO ticks, prescaler 1
        WDOG_PRESC = 0;
        // LPO clock, no window, no interrupt, counts on in wait and stop (a core parked in WFI does not hold it back)
        WDOG_STCTRLH = WDOG_STCTRLH_ALLOWUPDATE | WDOG_STCTRLH_WAITEN | WDOG_STCTRLH_STOPEN | WDOG_STCTRLH_WDOGEN;
    }
    else
    {
        WDOG_STCTRLH = WDOG_STCTRLH_ALLOWUPDATE;
    }
    irqRestore(primask);
#else
    (void)armed;
#endif
}

void PlatformImpl::kickPowerOffGuard()
{
#ifndef TRACKER_MINI
    if (!START_STATE_SAVE || !(WDOG_STCTRLH & WDOG_STCTRLH_WDOGEN))
    {
        return;
    }
    const uint32_t primask = irqSave();
    WDOG_REFRESH = 0xA602;
    WDOG_REFRESH = 0xB480;
    irqRestore(primask);
#endif
}

PlatformImpl::PowerButtonCallback PlatformImpl::getPowerButtonCallback()
{
    return powerButtonCallback;
}

void PlatformImpl::resetMCU()
{
#ifndef TRACKER_MINI
    saveStartState(1);
#endif

    __asm volatile("dsb");
    (*(uint32_t*)0xE000ED0C) = (uint32_t)((0x5FAUL << 16) | 0x04U);
    __asm volatile("dsb");

    for (;;)
    {
        __asm volatile("nop");
    }
}

void PlatformImpl::runBootloader(char* fileName)
{
    constexpr char kFirmwareInfoFilePath[] = "/Firmware/_fwinfo";

    if (fileSystem.exists(kFirmwareInfoFilePath))
    {
        fileSystem.remove(kFirmwareInfoFilePath);
    }

    auto openFileInfo =
      fileSystem.openFile(kFirmwareInfoFilePath, FileSystem::FileOpenMode::kWriteCreateNew);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (isOpen)
    {
        fileSystem.writeFile(fileName, strlen(fileName), file);
        fileSystem.closeFile(file);
    }
    else
    {
        return;
    }

    saveStartState(1); // provide correct restart after flashing

    delay(10);

    pinMode(TrackerPinout::kBootloaderTrig, OUTPUT);
    digitalWrite(TrackerPinout::kBootloaderTrig, LOW);

    while (1)
    {
        delay(1);
    }
}

uint8_t PlatformImpl::readStartState()
{
    uint8_t startState = 1;
    constexpr int idSize = 7;
    char id[idSize];

    configManager.readData(0, &id, idSize);

    if (strcmp(id, "EEPROM") == 0) // read start state only when valid config in eeprom
    {
        EEPROM.readData(START_STATE_EEPROM_ADRESS, &startState, 1);
    }

    return startState;
}

void PlatformImpl::saveStartState(uint8_t state)
{
    if (START_STATE_SAVE)
    {
        if (readStartState() != state)
        {
            EEPROM.writeData(START_STATE_EEPROM_ADRESS, &state, 1);
        }
    }
}

void PlatformImpl::setPowerButtonCallback(void (*powerButtonChangeFunc)(int))
{
    this->powerButtonCallback = powerButtonChangeFunc;
}

bool PlatformImpl::getPowerButtonState()
{
    return getTactSwitchState();
}

bool PlatformImpl::readForceStartUp()
{
    return false;
}

Sgtl5000* PlatformImpl::getCodecAudio()
{
    return (Sgtl5000*)&codecAudio;
}

Radio* PlatformImpl::getRadio()
{
    return (Radio*)&radio;
}

SwitchGroup* PlatformImpl::getButtons()
{
    return (SwitchGroup*)&buttons;
}

SwitchGroup* PlatformImpl::getPads()
{
    return (SwitchGroup*)&pads;
}

Leds* PlatformImpl::getLeds()
{
    return (Leds*)&leds;
}

Battery* PlatformImpl::getBattery()
{
    return (Battery*)&battery;
}

Encoder* PlatformImpl::getEncoder()
{
    return (Encoder*)&encoder;
}

Timer* PlatformImpl::getTimer()
{
    return (Timer*)&timer;
}

DisplayDriver* PlatformImpl::getDisplayDriver()
{
    return (DisplayDriver*)&displayDriver;
}

UartMidi* PlatformImpl::getUartMidi()
{
    return (UartMidi*)&uartMidi;
}

UsbMidi* PlatformImpl::getUsbMidi()
{
    return (UsbMidi*)&usbMidi;
}

UsbAudio* PlatformImpl::getUsbAudio()
{
    return (UsbAudio*)&usbAudio;
}

Usb* PlatformImpl::getUsb()
{
    return (Usb*)&usb;
}

ConfigManager* PlatformImpl::getConfigManager()
{
    return (ConfigManager*)&configManager;
}

FileSystem* PlatformImpl::getFileSystem()
{
    return (FileSystem*)&fileSystem;
}

CrcCalculator* PlatformImpl::getCrcCalculator()
{
    return (CrcCalculator*)&crcCalculator;
}

PlatformImpl::SystemInfo* PlatformImpl::getSystemInfo()
{
    return &systemInfo;
}
