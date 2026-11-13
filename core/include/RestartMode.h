#pragma once

// Restart mode of the original Tracker (MK66), shared by the firmware (mtSleep.cpp, interface.cpp) and the host build
// (synth-open/tools/ui_shot/host_platform.cpp), so the host tests run the same decisions as the unit.
//
// The mode is on while the file kFilePath exists at the card root (checked once at start, like /.develop). In the mode a completed
// power off restarts the unit instead of leaving it off, also when the power-off sequence stalls (PowerOffGuard.h resets into a start
// then). An aborted power off (power released before the bar ends) does nothing, as without the mode.
//
//   enter: hold Record while the unit starts (Record down at any time in the first kKeyWindowMs of the main loop). Creates the file.
//   leave: hold Record during a power off (Record down at any time while the bar runs). Deletes the file, the unit goes off normally.
//
// A restart needs a power button press each time: nothing in a start arms it, so the mode cannot loop by itself.

#include <stdint.h>

namespace restart_mode
{
constexpr const char* kFilePath = "/.autorestart";
constexpr uint32_t kKeyWindowMs = 4000; // same window as the plugin host's Shift at power on

enum class PowerOffEnd : uint8_t
{
    kOff,     // no mode: the unit goes off (unchanged behaviour)
    kRestart, // mode on: the unit restarts
    kLeave    // mode on and Record held during the bar: delete the file, the unit goes off
};

inline PowerOffEnd powerOffEnd(bool active, bool recordDuringBar)
{
    if (!active)
    {
        return PowerOffEnd::kOff;
    }
    return recordDuringBar ? PowerOffEnd::kLeave : PowerOffEnd::kRestart;
}

// Record seen while the start window is open: enter the mode (create the file) unless it is on already
inline bool shouldEnter(bool active, bool windowOpen, bool recordHeld)
{
    return !active && windowOpen && recordHeld;
}
} // namespace restart_mode
