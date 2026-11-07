// USB test console, Mac side. NOT firmware. The console core and its binding to the UI (core/source/UsbTest*.cpp) are the
// firmware's own; what is here is the platform: the serial port is stdin and stdout, the key and pad queue is the firmware's
// real Keys class (platforms/mk66/3rdparty/Buttons/Keys.cpp, hold repeat and Shift state included), time is the virtual clock.
// ui_shot --serve drives it, see ui_shot_main.cpp.
#include "host_platform.h"

#include "UsbTest.h"

#include "Keys.h"
#include "Tracker.h"

#include <stdarg.h>
#include <deque>
#include <stdio.h>
#include <string.h>
#include <string>

void onButtonPush(uint8_t n);
void onButtonRelease(uint8_t n);
void onButtonHold(uint8_t n);
void onPadPush(uint8_t n);
void onPadRelease(uint8_t n);
void onPadHold(uint8_t n);
void onPotChange(uint8_t n, int16_t value);

// stand-ins for core_pins.h (host_stubs/)
uint32_t millis() { return hostp::g_nowMs; }
void pinMode(uint8_t, uint8_t) {}
void attachInterrupt(uint8_t, void (*)(void), int) {}
void detachInterrupt(uint8_t) {}

namespace hostp
{
namespace
{
Keys g_buttons;
Keys g_pads;
bool g_keysReady;
std::deque<uint8_t> g_rx;
std::string g_tx;
bool g_link = true;
size_t g_txPerStep = 4096;
size_t g_txBudget = 4096;
int32_t g_encoderDelta;
uint32_t g_encoderTimer;

void readyKeys()
{
    if (g_keysReady) return;
    g_keysReady = true;
    g_buttons.setOnPush(onButtonPush);
    g_buttons.setOnRelease(onButtonRelease);
    g_buttons.setOnHold(onButtonHold);
    g_pads.setOnPush(onPadPush);
    g_pads.setOnRelease(onPadRelease);
    g_pads.setOnHold(onPadHold);
}
}   // namespace

bool keyPressed(bool pad, int idx)
{
    readyKeys();
    return idx >= 0 && idx < BUTTON_MAX && (pad ? g_pads : g_buttons).isButtonPressed(uint8_t(idx)) != 0;
}

// One main loop pass of the device (updateHardware, then Tracker::update) after 5 ms of the virtual clock
void usbtestStep()
{
    readyKeys();
    advance(5);
    g_buttons.update();
    g_pads.update();
    if (g_nowMs - g_encoderTimer > 100)   // EncoderImpl::update: the knob is read every 100 ms
    {
        g_encoderTimer = g_nowMs;
        if (g_encoderDelta != 0)
        {
            const int32_t d = g_encoderDelta;
            g_encoderDelta = 0;
            onPotChange(0, int16_t(d));
        }
    }
    g_txBudget = g_txPerStep;
    Tracker::getInstance()->update();
}

void usbtestFeed(const void* data, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    g_rx.insert(g_rx.end(), p, p + n);
}

size_t usbtestRxPending() { return g_rx.size(); }
std::string& usbtestTx() { return g_tx; }
void usbtestSetLink(bool up) { g_link = up; }
void usbtestSetTxRate(size_t perStep) { g_txPerStep = perStep; g_txBudget = perStep; }
}   // namespace hostp

// ------------------------------------------------------------------------------------------------ UsbTest.h platform hooks
int UsbTestPlatform_RxByte()
{
    if (hostp::g_rx.empty()) return -1;
    const int b = hostp::g_rx.front();
    hostp::g_rx.pop_front();
    return b;
}
bool UsbTestPlatform_LinkUp() { return hostp::g_link; }
size_t UsbTestPlatform_TxFree() { return hostp::g_txBudget; }
size_t UsbTestPlatform_TxWrite(const uint8_t* data, size_t length)
{
    if (length > hostp::g_txBudget) length = hostp::g_txBudget;
    hostp::g_tx.append(reinterpret_cast<const char*>(data), length);
    hostp::g_txBudget -= length;
    return length;
}
uint32_t UsbTestPlatform_IrqLock() { return 0; }
void UsbTestPlatform_IrqUnlock(uint32_t) {}
bool UsbTestPlatform_ValidPointer(const void* p) { return p != nullptr; }
bool UsbTestPlatform_InjectButton(uint8_t id, bool down) { return hostp::g_buttons.inject(id, down); }
bool UsbTestPlatform_InjectPad(uint8_t id, bool down) { return hostp::g_pads.inject(id, down); }
void UsbTestPlatform_InjectEncoder(int delta) { hostp::g_encoderDelta += delta; }
bool UsbTestPlatform_InputPending()
{
    return hostp::g_buttons.injectPending() || hostp::g_pads.injectPending() || hostp::g_encoderDelta != 0;
}
uint32_t UsbTestPlatform_ResetStatus() { return 0; }
bool UsbTestPlatform_FaultRecord(uint32_t*, uint32_t*, uint32_t*, uint32_t*) { return false; }

// LOG() of the core (core/source/log.h): into the console ring, like on the device
void _debuglog(const char* msg) { UsbTest_Log(msg); }
void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    UsbTest_LogV(format, args);
    va_end(args);
}
