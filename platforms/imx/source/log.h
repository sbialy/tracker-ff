#ifndef SOURCE_LOG_H_
#define SOURCE_LOG_H_

extern void _debuglog(const char* msg);
extern void _debuglogf(const char* format, ...);

// Print msg to a debug console/log. ENABLE_LOG turns logging on without the
// rest of what DEBUG changes (optimization level, boot behavior).
#if defined(DEBUG) || defined(ENABLE_LOG)
#define LOG(msg)          (_debuglog(msg))
#define LOGF(format, ...) _debuglogf(format, ##__VA_ARGS__)
#else
#define LOG(msg)
#define LOGF(format, ...)
#endif

// Essential level (errors, crash and safe mode, plugin and project load, file errors). core/ uses it, the Tracker+ and Mini
// consoles log through LOG, so here it follows LOG.
#if defined(DEBUG) || defined(ENABLE_LOG)
#define LOGE(msg)          (_debuglog(msg))
#define LOGEF(format, ...) _debuglogf(format, ##__VA_ARGS__)
#else
#define LOGE(msg)
#define LOGEF(format, ...)
#endif

#endif /* SOURCE_LOG_H_ */
