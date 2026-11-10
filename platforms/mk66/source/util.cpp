#include "Drivers/ClockCyclesCounter.h"
#include "core_pins.h"
#include "usb_serial.h"
#include "devhooks.h"

#ifdef FEATURE_EXT_HOOKS
#include "SD.h"
// Kept for an extension module (synth-open/ext_module.cpp keepForModule): the image has no other caller, the module links
// against them. Never called.
extern "C" __attribute__((noinline, used)) void ext_keep_platform(void)
{
    static volatile uint32_t sink;
    sink = SD.freeClusterCount() + SD.sectorsPerCluster();
    attachInterruptVector(IRQ_NUMBER_t(0), nullptr);
}
#endif

// A console image (devhooks.h) sends the text to its own ring instead of the serial port
void _debuglog(const char* msg)
{
    DEVHOOK_LOG(msg, Serial.println(msg));
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    DEVHOOK_LOGV(format, args, Serial.vprintf(format, args));

    va_end(args);
}

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
