#include "mtSleep.h"
#include "FT812.h"
#include "TrackerImpl.h"
#include <Audio.h>

mtSleep lowPower;

extern void interfaceEnvents(uint8_t event, void* param1, void* param2, void* param3);

void mtSleep::startPowerOffSequence()
{
    if (shutdownState == shutdownStateNone)
    {
        shutdownState = shutdownStateStart;
        shutdownTimer = 0;
        TrackerImpl::getInstance()->getPlatform()->setPowerOffGuard(true);
    }
}

void mtSleep::stopPowerOffSequence()
{
    if (shutdownState == shutdownStateStart)
    {
        TrackerImpl::getInstance()->getPlatform()->setPowerOffGuard(false);
    }
    shutdownState = shutdownStateNone;
}

uint32_t mtSleep::getOverrun()
{
    const uint32_t t = shutdownTimer;
    return t > TURN_OFF_TIME ? t - TURN_OFF_TIME : 0;
}

uint16_t mtSleep::getTimeLeft()
{
    return (shutdownTimer < TURN_OFF_TIME) ? TURN_OFF_TIME - shutdownTimer : 0;
}

uint8_t mtSleep::getShutdownProgress()
{
    return (shutdownTimer < TURN_OFF_TIME) ? ((TURN_OFF_TIME - shutdownTimer) * 100) / TURN_OFF_TIME
                                           : 100;
}

void mtSleep::wakeUp()
{
    shutdownState = shutdownStateNone;
    TrackerImpl::getInstance()->getPlatform()->setPowerMode(true);
}

void mtSleep::goLowPower()
{
    shutdownState = shutdownStateSleep;

    TrackerImpl::getInstance()->getPlatform()->setPowerMode(false);
}

void mtSleep::resetMCU()
{
    TrackerImpl::getInstance()->getPlatform()->resetMCU();
}
