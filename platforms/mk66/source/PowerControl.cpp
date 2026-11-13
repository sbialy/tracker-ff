#include "PowerControl.h"
#include "FT812.h"
#include "PlatformImpl.h"
#include "PowerOffGuard.h"
#include "config.h"
#include "core_pins.h"
#include "pinout.h"

#include <stdint.h>

bool powerButtonLastState;

uint32_t takePowerOffMarker(); // PlatformImpl.cpp
void noteOffStart(uint32_t srs, uint32_t marker); // PlatformImpl.cpp: the report of the next start that runs

void checkPowerState(PlatformImpl* platformImpl)
{
    TactSwitchInit();

    if (START_STATE_SAVE)
    {
        power_off_guard::StartInputs in;
        in.startStateSave = START_STATE_SAVE;
#ifdef ENABLE_DEV_CONSOLE
        in.consoleBuild = true;
#else
        in.consoleBuild = false;
#endif
        const uint32_t marker = takePowerOffMarker();
        in.offMarker = marker == power_off_guard::kOffMarker;
        in.restartMarker = marker == power_off_guard::kRestartMarker;
#ifndef TRACKER_MINI
        in.watchdogReset = (RCM_SRS0 & RCM_SRS0_WDOG) != 0;
#else
        in.watchdogReset = false;
#endif
        in.configValid = platformImpl->configValid();
        in.startState = platformImpl->readStartState();

        if (power_off_guard::decideStart(in) == power_off_guard::Start::kOff)
        {
#ifndef TRACKER_MINI
            noteOffStart(uint32_t(RCM_SRS0) | uint32_t(RCM_SRS1) << 8, in.offMarker ? 1u : in.restartMarker ? 2u : 0u);
#endif
#ifndef TRACKER_MINI
            // A reset into the off state (the power-off guard, any reset after the power off) can leave the display controller
            // running the last frame. Turn the panel off over SPI as the power off does (backlight 0, DISP low). Not PD# low: the
            // FT812 in power down leaves DISP and the backlight floating and the panel lights up white (MCU_Layer.cpp).
            MCU_panelOffAtOffStart();
#endif
            platformImpl->setPowerMode(false); // writes the start state 0: a flag a console image left (1) is cleared here
            while (1)
            {
                TactSwitchRead(platformImpl);
                asm("nop");
            }
        }
        else
        {
            // also clears a start state 1 a console image or the bootloader hand-off left. Power return Start (restart mode feature,
            // its EEPROM mirror) keeps the 1 instead: a power cut from here on starts the unit again
            platformImpl->saveStartState(platformImpl->readPowerReturnMirror() ? 1 : 0);
        }
    }
}

void TactSwitchInit()
{
    pinMode(TrackerPinout::kPowerButton, INPUT);
#ifndef TRACKER_MINI
    powerButtonLastState = digitalRead(TrackerPinout::kPowerButton);
#else
    pinMode(TrackerPinout::kPowerReset, INPUT);
    pinMode(TrackerPinout::kPowerSet, INPUT);
    powerButtonLastState = !digitalRead(TrackerPinout::kPowerButton);
#endif
}

bool getTactSwitchState()
{
#ifndef TRACKER_MINI
    return digitalRead(TrackerPinout::kPowerButton);
#else
    return !digitalRead(TrackerPinout::kPowerButton);
#endif
}

void TactSwitchRead(PlatformImpl* platformImpl)
{
    bool state = getTactSwitchState();

    if (state == powerButtonLastState)
    {
        return;
    }

    if (!platformImpl->getPowerMode())
    {
        if (state == false)
        {
            platformImpl->setPowerMode(true);
        }
        powerButtonLastState = state;
        return;
    }

    if (state == false)
    {
        platformImpl->getPowerButtonCallback()(1);
        powerButtonLastState = false;
    }
    else if (state == true)
    {
        powerButtonLastState = true;
        platformImpl->getPowerButtonCallback()(0);
    }
}
