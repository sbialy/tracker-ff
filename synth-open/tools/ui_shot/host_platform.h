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

// USB test console (host_usbtest.cpp)
bool keyPressed(bool pad, int idx);   // the firmware's Keys class holds the state of the buttons and pads
void usbtestStep();                   // 5 ms of the virtual clock and one main loop pass
void usbtestFeed(const void* data, size_t n);
size_t usbtestRxPending();
std::string& usbtestTx();
void usbtestSetLink(bool up);
void usbtestSetTxRate(size_t perStep);
extern bool g_resetRequested;

// Simulated bootloader for the firmware update (host_usbtest.cpp). The hand-off writes /Firmware/_fwinfo like
// PlatformImpl::runBootloader, appends the name to $UISHOT_BOOTLOADER_LOG and ends the process with exit code 77 (a reset).
// The next start "installs" the named .ptf: the reported build id becomes the one found in it. UISHOT_BOOTLOADER_STUCK=1 never comes back.
const std::string& sdRoot();
const char* simBuildId();
void simBootloaderOnStart();
void simBootloaderHandoff(const char* name);   // returns only with UISHOT_BOOTLOADER_FAIL=1 (the _fwinfo file could not be written)
long simFreeBytes();             // $UISHOT_FREE_BYTES or the free space of the folder
}   // namespace hostp
