#include "Drivers/ClockCyclesCounter.h"
#include "core_pins.h"
#include "usb_serial.h"
#include "UsbTest.h"

#ifdef ENABLE_DEV_CONSOLE
// LOG() runs in any context, the audio interrupt included, and usb_serial_write can wait up to 70 ms for a host that does
// not read. With the test console the text goes into its ring instead and the main loop sends it.
void _debuglog(const char* msg)
{
    UsbTest_Log(msg);
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    UsbTest_LogV(format, args);

    va_end(args);
}
#else
void _debuglog(const char* msg)
{
    Serial.println(msg);
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    Serial.vprintf(format, args);

    va_end(args);
}
#endif

uint32_t cpuCycles()
{
    uint32_t clockCycles = clockCyclesCounter.clockCycles();
    return clockCycles;
}

void assert_func_play(const char*, int)
{
    asm("bkpt #0");
    while (1)
    {
    }
}

void __assert_func(const char* file, int line, const char*, const char*)
{
    assert_func_play(file, line);
    while (1)
    {
    }
}
