#include "mtSleep.h"
#include "FT812.h"
#include "RestartMode.h"
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
    if (restartLeave)
    {
        restartLeave = false; // an aborted power off keeps the restart mode
        setRestartActive(restartActive);
    }
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

    Platform* platform = TrackerImpl::getInstance()->getPlatform();
    if (restart_mode::powerOffEnd(restartActive, restartLeave) == restart_mode::PowerOffEnd::kLeave)
    {
        platform->getFileSystem()->remove(restart_mode::kFilePath);
        restartActive = false;
        restartLeave = false;
    }

    // the platform restarts instead of going off while the restart mode is on (setRestartOnPowerOff)
    platform->setPowerMode(false);
}

void mtSleep::setRestartActive(bool on)
{
    restartActive = on;
    TrackerImpl::getInstance()->getPlatform()->setRestartOnPowerOff(on && !restartLeave);
}

void mtSleep::initRestartMode()
{
    restartLeave = false;
    setRestartActive(TrackerImpl::getInstance()->getPlatform()->getFileSystem()->exists(restart_mode::kFilePath));
    restartWindowOpen = true;
    restartWindowTimer = 0;
}

void mtSleep::updateRestartMode(bool recordHeld)
{
    if (restartWindowOpen && restartWindowTimer >= restart_mode::kKeyWindowMs)
    {
        restartWindowOpen = false;
    }

    if (shutdownState == shutdownStateStart)
    {
        if (restartActive && recordHeld && !restartLeave)
        {
            restartLeave = true; // Record during the bar: this power off leaves the mode and goes off
            setRestartActive(true);
        }
        return;
    }

    if (shutdownState == shutdownStateNone && restart_mode::shouldEnter(restartActive, restartWindowOpen, recordHeld))
    {
        FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
        auto openFileInfo = fs->openFile(restart_mode::kFilePath, FileSystem::FileOpenMode::kWriteCreateNew);
        if (std::get<0>(openFileInfo))
        {
            fs->closeFile(std::get<1>(openFileInfo));
        }
        restartWindowOpen = false;
        setRestartActive(fs->exists(restart_mode::kFilePath)); // no card or a write error: the mode stays off
    }
}

void mtSleep::resetMCU()
{
    TrackerImpl::getInstance()->getPlatform()->resetMCU();
}
