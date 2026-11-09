#pragma once

// Power-off stall guard and the start decision of a release image, shared by the MK66 platform (PlatformImpl.cpp, PowerControl.cpp)
// and the Mac simulator (synth-open/tools/ui_shot/host_platform.cpp), so the simulator runs the same logic as the unit.
//
// Why no hardware watchdog: on the original Tracker's K66 a refresh of the WDOG is ignored and an unlock + disable does not stop it
// once an application has armed it (device probes, ref/shot-hang-1.2.0.md R8.2). An armed WDOG is a one-shot reset timer there, so
// arming it at the power button press reset the unit a few seconds later in any state, also after it had gone off.
//
// The guard is a deadline in milliseconds. Armed when the power-off sequence starts, kicked on every main loop pass of it, disarmed when
// the sequence is aborted or right before the sleep. The SysTick interrupt (a main loop blocked in a call) and the fault handler (the
// main loop gone) check it while it is armed. When it runs out the platform resets into the off state through a RAM marker, it never
// writes the EEPROM start state 1 that a power-on writes. A disarmed guard never acts, so it cannot reset a unit that is already off.

#include <stdint.h>

namespace power_off_guard
{
constexpr uint32_t kTimeoutMs = 3000;
// RAM word that survives a reset (not a power cycle): written right before the guard resets the MCU, read and cleared at the next boot
constexpr uint32_t kOffMarker = 0x0FF5A7E1u;

class Guard
{
  public:
    void arm(uint32_t nowMs) { deadline_ = (nowMs + kTimeoutMs) | 1u; }
    void kick(uint32_t nowMs)
    {
        if (deadline_ != 0)
        {
            deadline_ = (nowMs + kTimeoutMs) | 1u;
        }
    }
    void disarm() { deadline_ = 0; }
    bool armed() const { return deadline_ != 0; }
    bool expired(uint32_t nowMs) const
    {
        const uint32_t d = deadline_;
        return d != 0 && int32_t(nowMs - d) >= 0;
    }

  private:
    volatile uint32_t deadline_ = 0; // 0 = disarmed, else the time it runs out (odd, so never 0)
};

enum class Start : uint8_t
{
    kRun, // start the UI
    kOff  // stay off until the power button
};

struct StartInputs
{
    bool startStateSave; // config.h START_STATE_SAVE: a release build of the original (a debug build always starts)
    bool consoleBuild;   // ENABLE_DEV_CONSOLE compiled in (a console image may arm the WDOG on purpose with `wdog on`)
    bool offMarker;      // the guard reset the MCU (kOffMarker found)
    bool watchdogReset;  // RCM_SRS0 WDOG: on a release image without the console nothing arms it, a bite is never a power-on
    bool configValid;    // the EEPROM holds a valid config ("EEPROM" id), else the start state byte is not trusted
    uint8_t startState;  // EEPROM byte 0: 1 = a power-on, a bootloader hand-off or a console image left it, 0 = off
};

inline Start decideStart(const StartInputs& in)
{
    if (!in.startStateSave)
    {
        return Start::kRun;
    }
    if (in.offMarker || (in.watchdogReset && !in.consoleBuild))
    {
        return Start::kOff;
    }
    if (!in.configValid)
    {
        return Start::kRun; // stock: a unit without a valid config always starts
    }
    return in.startState == 0 ? Start::kOff : Start::kRun;
}
} // namespace power_off_guard
