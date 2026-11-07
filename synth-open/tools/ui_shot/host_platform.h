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
}   // namespace hostp
