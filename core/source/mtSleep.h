#ifndef SOURCE_MTSLEEP_H_
#define SOURCE_MTSLEEP_H_

#include "elapsedMillis.h"

#include <stdint.h>

const uint16_t TURN_OFF_TIME = 1500;
// after the bar has ended, how long the power off waits for the file manager before it goes on anyway: a status that no
// operation will clear, and an operation that keeps running (a save in progress is not cut early, the Workspace stays whole)
const uint16_t POWER_OFF_IDLE_WAIT = 3000;
const uint16_t POWER_OFF_OPERATION_WAIT = 30000;

enum enShutdownState
{
    shutdownStateNone,
    shutdownStateStart,
    shutdownStateSleep,
};

class mtSleep
{
  public:
    void startPowerOffSequence();
    void stopPowerOffSequence();
    uint16_t getTimeLeft();
    uint8_t getShutdownProgress();
    uint32_t getOverrun(); // ms since the bar ended, 0 before
    inline uint8_t getLowPowerState() { return shutdownState; }
    inline void setLowPowerState(uint8_t state) { shutdownState = state; }
    void goLowPower();
    void wakeUp();
    void resetMCU();

    // Restart mode (RestartMode.h): read the file once at start, then every main loop pass with the Record key state
    void initRestartMode();
    void updateRestartMode(bool recordHeld);
    inline bool isRestartMode() const { return restartActive; }
    inline bool isLeavingRestartMode() const { return restartLeave; }

  private:
    void setRestartActive(bool on);

    uint8_t shutdownState = shutdownStateNone;
    elapsedMillis shutdownTimer;
    bool restartActive = false;
    bool restartLeave = false;     // Record seen during the bar of the current power off
    bool restartWindowOpen = false; // the start window for Record (kKeyWindowMs)
    elapsedMillis restartWindowTimer;
};

extern mtSleep lowPower;

#endif /* SOURCE_MTSLEEP_H_ */
