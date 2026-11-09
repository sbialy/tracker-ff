#include "PowerControl.h"
#include "PlatformImpl.h"
#include "config.h"
#include "core_pins.h"
#include "pinout.h"

#include <stdint.h>

bool powerButtonLastState;

void checkPowerState(PlatformImpl* platformImpl)
{
    TactSwitchInit();

    if (START_STATE_SAVE)
    {
        if (platformImpl->readStartState() == 0)
        {
#ifndef TRACKER_MINI
            if (RCM_SRS0 & RCM_SRS0_WDOG)
            {
                // the power-off stall guard reset the MCU (PlatformImpl::setPowerOffGuard): the display controller kept
                // running through the reset and may still show the last frame. Hold the FT812 in power down (PD#, PTD11, as
                // FT812_Init drives it). The next start cycles PD# in FT812_Init.
                GPIOD_PCOR = 1u << 11;
                GPIOD_PDDR |= 1u << 11;
                PORTD_PCR11 = PORT_PCR_MUX(1);
            }
#endif
            platformImpl->setPowerMode(false);
            while (1)
            {
                TactSwitchRead(platformImpl);
                asm("nop");
            }
        }
        else
        {
            platformImpl->saveStartState(0);
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
