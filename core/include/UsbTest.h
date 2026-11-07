#ifndef SOURCE_USBTEST_H_
#define SOURCE_USBTEST_H_

// USB test mode for the original Tracker (and the Mac host build): a text console on the USB serial port that drives the
// real input path, reads the state, takes screenshots and moves files. Protocol: docs/usb-test-console.md.
// Two gates: the ENABLE_DEV_CONSOLE compile flag (make original ... CUSTOM_DEFINES=-DENABLE_DEV_CONSOLE, release images
// leave it off and every function here is an empty inline) and the /.develop file on the card (Tracker.cpp), plus a terminal
// that has the port open (DTR). Not for Tracker+ and Mini, they have their own console (platforms/imx).

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

class Platform;

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

#define FW_USB_TEST 1

// main loop only
void UsbTest_Init(Platform* platform, bool develop);
void UsbTest_Update();

// any context, including the audio interrupt: the text is queued in a ring, nothing waits
void UsbTest_Log(const char* text);
void UsbTest_LogV(const char* format, va_list args);

// What the platform provides (platforms/mk66/source/UsbTestPlatform.cpp, synth-open/tools/ui_shot/host_usbtest.cpp).
int UsbTestPlatform_RxByte(); // -1 when nothing was received
bool UsbTestPlatform_LinkUp();
size_t UsbTestPlatform_TxFree(); // bytes that can be written right now without waiting
size_t UsbTestPlatform_TxWrite(const uint8_t* data, size_t length);
uint32_t UsbTestPlatform_IrqLock();
void UsbTestPlatform_IrqUnlock(uint32_t state);
bool UsbTestPlatform_ValidPointer(const void* p);
bool UsbTestPlatform_InjectButton(uint8_t id, bool down);
bool UsbTestPlatform_InjectPad(uint8_t id, bool down);
void UsbTestPlatform_InjectEncoder(int delta);
bool UsbTestPlatform_InputPending();
uint32_t UsbTestPlatform_ResetStatus();
// True if the fault handler of a REBOOT_ON_FAULT image saved a record before the last reset (the record is then cleared)
bool UsbTestPlatform_FaultRecord(uint32_t* pc, uint32_t* lr, uint32_t* psr, uint32_t* cfsr);

#else

static inline void UsbTest_Init(Platform*, bool)
{
}
static inline void UsbTest_Update()
{
}
static inline void UsbTest_Log(const char*)
{
}
static inline void UsbTest_LogV(const char*, va_list)
{
}

#endif

#endif /* SOURCE_USBTEST_H_ */
