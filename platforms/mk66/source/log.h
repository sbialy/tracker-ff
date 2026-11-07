#ifndef SOURCE_LOG_H_
#define SOURCE_LOG_H_

extern void _debuglog(const char* msg);
extern void _debuglogf(const char* format, ...);

// Two log levels.
//   LOG / LOGF    verbose: dumps, tables, per step traces. On with DEBUG, ENABLE_LOG or LOG_ALL.
//   LOGE / LOGEF  essential: errors, crash and safe mode, plugin and project load, file errors, card and storage mode.
//                 On wherever LOG is, and also with ENABLE_DEV_CONSOLE (USB test console), where it is all the console logs.
// ENABLE_LOG turns logging on without the rest of what DEBUG changes (optimization level, boot behavior).
// LOG_ALL is the opt-in for the verbose level in a console image: -DENABLE_DEV_CONSOLE -DLOG_ALL (about +0.5 KB).
// The text of a console image goes through the console ring (UsbTest.h), never straight to the serial port.
#if defined(DEBUG) || defined(ENABLE_LOG) || defined(LOG_ALL)
#define LOG(msg) (_debuglog(msg))
#define LOGF(format, ...) _debuglogf(format, __VA_ARGS__)
#else
#define LOG(msg)
#define LOGF(format, ...)
#endif

#if defined(DEBUG) || defined(ENABLE_LOG) || defined(LOG_ALL) || defined(ENABLE_DEV_CONSOLE)
#define LOGE(msg) (_debuglog(msg))
#define LOGEF(format, ...) _debuglogf(format, __VA_ARGS__)
#else
#define LOGE(msg)
#define LOGEF(format, ...)
#endif

#endif /* SOURCE_LOG_H_ */
