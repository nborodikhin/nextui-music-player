#ifndef __WATCHDOG_H__
#define __WATCHDOG_H__

#include <stdbool.h>

// Hang-detection watchdog. A separate thread tripping abort() if the main loop's
// heartbeat goes stale, converting hangs into SIGABRT so the crash signal handler
// can capture a diagnostic bundle. See spec/crash-reporting.md.

// Pause reasons. Each occupies one bit in an atomic bitmask; the watchdog only
// fires when the bitmask is zero. Distinct reasons compose; same reason set twice
// is idempotent (the bit stays set).
typedef enum {
    WATCHDOG_REASON_KEYBOARD = 0,
    WATCHDOG_REASON_DISPLAY_RECOVERY,
    WATCHDOG_REASON_DEEP_SLEEP,
    WATCHDOG_REASON_HTTP_FETCH,
    WATCHDOG_REASON_SUBPROCESS,   // popen/system shell calls on main
    WATCHDOG_REASON_COUNT
} WatchdogPauseReason;

// Initialize and start the watchdog thread. Threshold is the stall budget in ms
// before abort() is called. Pass 0 to use the default (5000 ms).
// Safe to call once; subsequent calls are ignored.
void Watchdog_init(unsigned threshold_ms);

// Stop the watchdog thread and join it. Safe to call even if init was not called.
void Watchdog_quit(void);

// Heartbeat — call once per main-loop iteration (top of each module's while(1)).
// Updates the monotonic heartbeat the watchdog reads. Inline-friendly, no syscall.
void Watchdog_heartbeat(void);

// Pause/resume the dog for a specific reason. Pausing multiple times for distinct
// reasons composes; resuming clears that reason's bit. Watchdog fires only when
// no reasons are set. Idempotent per-reason.
//
// `trace`: when true, both pause and resume emit a LOG_trace line (resume
// includes the post-release pause-mask). Callers wrapping inherently slow
// operations (keyboard, display recovery, network, subprocess) should pass true.
// Callers wrapping a fast-or-slow operation that runs every frame should pass
// false to keep the ring log readable.
void Watchdog_pause(WatchdogPauseReason reason, bool trace);
void Watchdog_resume(WatchdogPauseReason reason, bool trace);

// Returns true iff the dog is currently paused (any reason set). For tests.
bool Watchdog_isPaused(void);

// Last heartbeat tick as observed by the watchdog (SDL_GetTicks() at last
// Watchdog_heartbeat() call). 0 if no heartbeat has happened yet.
// Async-signal-safe (atomic load).
unsigned int Watchdog_lastHeartbeatMs(void);

#endif
