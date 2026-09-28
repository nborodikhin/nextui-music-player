#ifndef __DEBUG_H__
#define __DEBUG_H__

// Debug timing and timed logging. A release build compiles each function to
// nothing and keeps the logging of NextUI.

#include "defines.h"
#include "api.h"

typedef long long int DebugTime;

#ifdef DEBUG

// Records the start time of the app. Call it first in main().
void Debug_init(void);

// Returns the milliseconds since Debug_init(), from a monotonic clock.
DebugTime Debug_time(void);

// Returns the milliseconds from a value of Debug_time() until now.
DebugTime Debug_msSince(DebugTime time);

#undef LOG_debug
#undef LOG_info
#undef LOG_warn
#undef LOG_error

// Writes one log line to stderr, with the time since Debug_init() in the form
// +ss.mmm before it.
void Debug_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG_debug(fmt, ...) Debug_log("[DEBUG] " fmt, ##__VA_ARGS__)
#define LOG_info(fmt, ...) Debug_log("[INFO] " fmt, ##__VA_ARGS__)
#define LOG_warn(fmt, ...) Debug_log("[WARN] " fmt, ##__VA_ARGS__)
#define LOG_error(fmt, ...) Debug_log("[ERROR] " fmt, ##__VA_ARGS__)

#else

static inline void Debug_init(void) {}
static inline DebugTime Debug_time(void) { return 0; }
static inline DebugTime Debug_msSince(DebugTime time) {
    (void)time;
    return 0;
}

#endif

#endif
