#include "PlatformImpl.h"
#include "PowerOffGuard.h"
#include "EEPROM.h"
#include "PowerControl.h"
#include "config.h"
#include "core_pins.h"
#include "pinout.h"
#include "polyassert.h"
#include "sdram.h"
#include "devhooks.h" // FEATURE_EXT_HOOKS: ext_power_down (ext_hooks.h)
#ifdef FEATURE_USB_AUDIO
#include "usb_audio_ff.h"
#endif

#ifdef DEBUG
#define DEBUG_TEXT " debug"
#else
#define DEBUG_TEXT
#endif

PlatformImpl::SystemInfo PlatformImpl::systemInfo = { BUILD_ID DEBUG_TEXT };

extern void disableHardware();

// restart mode (RestartMode.h, set by the core through setRestartOnPowerOff): read by setPowerMode(false) and the guard
static volatile bool restartOnPowerOff;

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
        // restart mode (RestartMode.h): the same teardown, then the reset of a power-on (start state 1, as resetMCU) instead of
        // the sleep. The next start reads 1 and comes up without a button press, exactly as after a wake from the sleep.
        const bool restart = restartOnPowerOff;

        saveStartState(restart ? 1 : 0);

        noInterrupts();

#ifdef FEATURE_EXT_HOOKS
        // disableHardware unpowers the SDRAM, where a loaded extension module keeps its SysTick and fault handlers, and turns the
        // interrupts on again (detachInterrupt): the image's own handlers first, or the next SysTick locks the core up
        ext_power_down();
#endif

        disableHardware();

        // disableHardware turned the interrupts on again (detachInterrupt) with the SDRAM unpowered. Off from here for good, the same
        // state as the off start (checkPowerState): the sleep below polls the power button and its wake (Snooze.wakeUp, resetMCU) polls
        // registers only. Every handler left (SysTick, MIDI, card, display) would run on the slow BLPI clock with the SDRAM gone.
        noInterrupts();

        setPowerOffGuard(false); // the sleep below polls the power button

        if (restart)
        {
            __asm volatile("dsb");
            (*(volatile uint32_t*)0xE000ED0C) = (uint32_t)((0x5FAUL << 16) | 0x04U);
            __asm volatile("dsb");
            for (;;)
            {
                __asm volatile("nop");
            }
        }

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

// Power-off stall guard (PowerOffGuard.h): a deadline checked from SysTick and from the fault handler, no hardware watchdog (the K66
// WDOG cannot be refreshed or stopped once armed on this unit, it reset the unit after it had gone off). When it runs out the MCU
// resets into the off state through powerOffMarker, without writing the start state 1 that resetMCU (a power-on) writes.
static power_off_guard::Guard powerOffGuard;
extern "C" volatile uint32_t systick_millis_count;
// survives a reset, not a power cycle (.noinit is not cleared by the startup code)
uint32_t powerOffMarker __attribute__((section(".noinit.power_off")));

static void resetWithMarker(uint32_t marker)
{
#ifndef TRACKER_MINI
    __disable_irq();
    powerOffMarker = marker;
    __asm volatile("dsb");
    (*(volatile uint32_t*)0xE000ED0C) = (uint32_t)((0x5FAUL << 16) | 0x04U);
    __asm volatile("dsb");
    for (;;)
    {
        __asm volatile("nop");
    }
#endif
}

extern "C" void resetToOffState()
{
    resetWithMarker(power_off_guard::kOffMarker);
}

// the end of a stalled power-off sequence: the off state, or a start in the restart mode. The restart marker is written only here,
// while the guard of a power-off sequence runs, and taken at the next boot, so a start never re-enters it by itself.
extern "C" void resetAfterPowerOff()
{
    resetWithMarker(restartOnPowerOff ? power_off_guard::kRestartMarker : power_off_guard::kOffMarker);
}

// the off starts a reset caused (checkPowerState), for the report at the next start that runs (takeOffStartRecord). Same RAM as the
// marker: survives a reset, not a power cycle, so the magic tells a record from what the RAM held at power up.
struct OffStartRecord
{
    uint32_t magic, srs, marker, count;
};
constexpr uint32_t kOffStartMagic = 0x0FF57A27u;
OffStartRecord offStartRecord __attribute__((section(".noinit.power_off")));

void noteOffStart(uint32_t srs, uint32_t marker)
{
    if (offStartRecord.magic != kOffStartMagic)
    {
        offStartRecord.count = 0;
    }
    offStartRecord.magic = kOffStartMagic;
    offStartRecord.srs = srs;
    offStartRecord.marker = marker;
    offStartRecord.count++;
}

bool PlatformImpl::takeOffStartRecord(uint32_t& srs, uint32_t& marker, uint32_t& count)
{
    if (offStartRecord.magic != kOffStartMagic)
    {
        return false;
    }
    srs = offStartRecord.srs;
    marker = offStartRecord.marker;
    count = offStartRecord.count;
    offStartRecord.magic = 0;
    return true;
}

uint32_t takePowerOffMarker()
{
    const uint32_t marker = powerOffMarker;
    powerOffMarker = 0;
    return marker;
}

// SysTick (EventResponder.cpp), every millisecond: the main loop of the power-off sequence stopped coming round
extern "C" void powerOffGuardTick()
{
    if (powerOffGuard.expired(systick_millis_count))
    {
        resetAfterPowerOff();
    }
}

// fault_isr (mk20dx128.c) of a release image: a fault during the power-off sequence powers off (or restarts in the restart mode)
// instead of a lit, dead unit
extern "C" void powerOffGuardFault()
{
    if (powerOffGuard.armed())
    {
        resetAfterPowerOff();
    }
}

void PlatformImpl::setRestartOnPowerOff(bool on)
{
#ifndef TRACKER_MINI
    restartOnPowerOff = on && START_STATE_SAVE; // a debug build always starts anyway
#else
    (void)on;
#endif
}

void PlatformImpl::setStartOnPowerReturn(bool on)
{
#ifndef TRACKER_MINI
    // checkPowerState wrote the mirror below at this start. 1 makes the next power-up (power returning after a cut) start the UI,
    // the press of a power off writes 0 again (setPowerOffGuard), so a deliberate power off still ends off. The mirror lets the
    // next start keep the 1 itself (no 3 s window with 0, one write fewer per session). One EEPROM write per change of each.
    if (START_STATE_SAVE)
    {
        uint8_t mirror = 0;
        EEPROM.readData(POWER_RETURN_EEPROM_ADRESS, &mirror, 1);
        uint8_t want = on ? POWER_RETURN_ON : 0;
        if (mirror != want)
        {
            EEPROM.writeData(POWER_RETURN_EEPROM_ADRESS, &want, 1);
        }
    }
    saveStartState(on ? 1 : 0);
#else
    (void)on;
#endif
}

bool PlatformImpl::readPowerReturnMirror()
{
#ifndef TRACKER_MINI
    uint8_t mirror = 0;
    if (configValid())
    {
        EEPROM.readData(POWER_RETURN_EEPROM_ADRESS, &mirror, 1);
    }
    return mirror == POWER_RETURN_ON;
#else
    return false;
#endif
}

#ifdef FEATURE_USB_AUDIO
bool PlatformImpl::usbAudioRunning()
{
    return ffUsbAudioActive() != 0;
}

void PlatformImpl::setUsbAudioBoot(bool on)
{
    ffUsbAudioSetBootFlag(on ? 1 : 0);
}
#endif

bool PlatformImpl::powerOffGuardArmed() const
{
    return powerOffGuard.armed();
}

void PlatformImpl::setPowerOffGuard(bool armed)
{
#ifndef TRACKER_MINI
    if (!START_STATE_SAVE)
    {
        return; // debug build: always starts, a reset would not power it off
    }
    if (armed)
    {
        saveStartState(0); // a running release image holds 0 already (checkPowerState), no write then
        powerOffGuard.arm(systick_millis_count);
    }
    else
    {
        powerOffGuard.disarm();
    }
#else
    (void)armed;
#endif
}

void PlatformImpl::kickPowerOffGuard()
{
    powerOffGuard.kick(systick_millis_count);
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

bool PlatformImpl::configValid()
{
    constexpr int idSize = 7;
    char id[idSize];

    configManager.readData(0, &id, idSize);

    return memcmp(id, "EEPROM", idSize) == 0; // the id and its terminator

}

uint8_t PlatformImpl::readStartState()
{
    uint8_t startState = 1;

    if (configValid()) // read start state only when valid config in eeprom
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
