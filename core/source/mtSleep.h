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

  private:
    uint8_t shutdownState = shutdownStateNone;
    elapsedMillis shutdownTimer;
};

extern mtSleep lowPower;

#endif /* SOURCE_MTSLEEP_H_ */
