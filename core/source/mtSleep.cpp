#include "mtSleep.h"
#include "FT812.h"
#include "RestartMode.h"
#include "TrackerImpl.h"
#ifdef FEATURE_EXT_HOOKS
#include "features.h"
#endif
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
        if (startOnPowerReturn)
        {
            TrackerImpl::getInstance()->getPlatform()->setStartOnPowerReturn(true); // the press wrote 0, a cancelled one puts 1 back
        }
    }
    shutdownState = shutdownStateNone;
    if (restartLeave || restartOnce)
    {
        restartLeave = false; // an aborted power off keeps the restart mode
        restartOnce = false;  // and an aborted Restart now is forgotten
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
#ifdef FEATURE_EXT_HOOKS
        features::set(features::find("restart"), false);
#else
        platform->getFileSystem()->remove(restart_mode::kFilePath);
#endif
        restartActive = false;
        restartLeave = false;
    }

#ifdef FEATURE_EXT_HOOKS
    features::flush(); // every changed feature file is written before the power goes (the 1 s debounce would lose it)
#endif
    restartOnce = false;
    // the platform restarts instead of going off while the restart mode is on (setRestartOnPowerOff)
    platform->setPowerMode(false);
}

void mtSleep::setRestartActive(bool on)
{
    restartActive = on;
    TrackerImpl::getInstance()->getPlatform()->setRestartOnPowerOff((on && !restartLeave) || restartOnce);
}

void mtSleep::requestRestartOnce()
{
    restartOnce = true;
    setRestartActive(restartActive);
}

void mtSleep::initRestartMode()
{
    restartLeave = false;
#ifdef FEATURE_EXT_HOOKS
    // the features own the mode: a /.autorestart of an older image switches the feature on once and is removed
    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs->exists(restart_mode::kFilePath))
    {
        features::set(features::find("restart"), true);
        fs->remove(restart_mode::kFilePath);
    }
#else
    setRestartActive(TrackerImpl::getInstance()->getPlatform()->getFileSystem()->exists(restart_mode::kFilePath));
#endif
    restartWindowOpen = true;
    restartWindowTimer = 0;
}

void mtSleep::setRestartMode(bool on, bool recordShortcut)
{
    restartRecordKey = recordShortcut;
    setRestartActive(on);
}

void mtSleep::setStartOnPowerReturn(bool on)
{
    startOnPowerReturn = on;
    TrackerImpl::getInstance()->getPlatform()->setStartOnPowerReturn(on);
}

void mtSleep::updateRestartMode(bool recordHeld)
{
    if (restartWindowOpen && restartWindowTimer >= restart_mode::kKeyWindowMs)
    {
        restartWindowOpen = false;
    }

    if (shutdownState == shutdownStateStart)
    {
        if (restartActive && restartRecordKey && recordHeld && !restartLeave)
        {
            restartLeave = true; // Record during the bar: this power off leaves the mode and goes off
            setRestartActive(true);
        }
        return;
    }

    if (shutdownState == shutdownStateNone && restartRecordKey &&
        restart_mode::shouldEnter(restartActive, restartWindowOpen, recordHeld))
    {
#ifdef FEATURE_EXT_HOOKS
        restartWindowOpen = false;
        features::set(features::find("restart"), true);
        return;
#endif
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
