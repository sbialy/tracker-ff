#ifndef SOURCE_DEVCONSOLE_H_
#define SOURCE_DEVCONSOLE_H_

// Development console on the USB serial port: log output plus a few commands
// for scripted firmware deployment. Compiled in with ENABLE_DEV_CONSOLE, and
// even then the port is only enumerated in develop mode (a "/.develop" file on
// the SD card). See docs/developer_guide.md.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ENABLE_DEV_CONSOLE

#ifdef __cplusplus
extern "C"
{
#endif

    // Output is queued and sent from the main loop, so these are safe to call
    // from interrupt context. Formatting uses newlib, whose "%f" allocates:
    // do not format floats from an interrupt.
    void DevConsole_Write(const uint8_t* data, size_t length);
    void DevConsole_Printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
    void DevConsole_Vprintf(const char* format, va_list args);

    // For the USB CDC task, main loop only.
    size_t DevConsole_TxRead(uint8_t* destination, size_t maxLength);
    void DevConsole_RxBytes(const uint8_t* data, size_t length);

#ifdef __cplusplus
}

class PlatformImpl;

void DevConsole_Init(PlatformImpl* platform);
void DevConsole_Update();
#endif

#else /* ENABLE_DEV_CONSOLE */

static inline void DevConsole_Write(const uint8_t* data, size_t length)
{
    (void)data;
    (void)length;
}

static inline void DevConsole_Printf(const char* format, ...)
{
    (void)format;
}

static inline void DevConsole_Vprintf(const char* format, va_list args)
{
    (void)format;
    (void)args;
}

static inline size_t DevConsole_TxRead(uint8_t* destination, size_t maxLength)
{
    (void)destination;
    (void)maxLength;
    return 0;
}

static inline void DevConsole_RxBytes(const uint8_t* data, size_t length)
{
    (void)data;
    (void)length;
}

#ifdef __cplusplus
class PlatformImpl;

inline void DevConsole_Init(PlatformImpl*)
{
}
inline void DevConsole_Update()
{
}
#endif

#endif /* ENABLE_DEV_CONSOLE */

#endif /* SOURCE_DEVCONSOLE_H_ */
