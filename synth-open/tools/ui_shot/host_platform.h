// Host stand-ins for the Platform HAL (core/include/Platform.h): everything the UI core asks the hardware for. NOT firmware.
// Time is virtual (advance() moves it), the SD card is a folder on the Mac, the display is the FT812 model.
#pragma once

#include "Platform.h"
#include "eve_model.h"

#include <string>

namespace hostp
{
extern uint32_t g_nowMs;        // virtual clock, milliseconds
void advance(uint32_t ms);
void setHeld(int idx, bool on);   // what SwitchGroup::isButtonPressed reports (Shift is read that way)
Platform* platform();
EveModel& eve();
void setSdRoot(const std::string& dir);

// Development console (make CONSOLE=1, a separate checkout: host/ui_shot/host_usbtest.cpp). host_platform.cpp has weak
// defaults for a plain build: no key is held by the firmware's Keys class, the build id is "uishot", the bootloader hand-off does nothing.
bool keyPressed(bool pad, int idx);   // the firmware's Keys class holds the state of the buttons and pads
extern bool g_resetRequested;        // Platform::resetMCU was called
const std::string& sdRoot();
const char* simBuildId();
void simBootloaderHandoff(const char* name);   // Platform::runBootloader
}   // namespace hostp
