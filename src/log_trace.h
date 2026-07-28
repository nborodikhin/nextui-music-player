#ifndef __LOG_TRACE_H__
#define __LOG_TRACE_H__

// Tee-into-ring wrapper around NextUI's LOG_note plus a new ring-only LOG_trace.
// See spec/crash-reporting.md.
//
// USAGE: include this header AFTER NextUI's "api.h" (which declares LOG_note and
// defines LOG_debug/LOG_info/LOG_warn/LOG_error). The macros below override LOG_note
// so every existing call site is captured in the ring buffer without changing NextUI.
//
// Music-player files only — never include from NextUI translation units.

// Enforce the include order. LOG_debug/info/warn/error are defined by api.h; if
// this header is included first, api.h's own LOG_note wins and that file's LOG_*
// silently bypass the ring — a per-file, invisible failure that has bitten this
// codebase before (12 engine/network files once missed the include). A compile
// error is cheaper than another silent gap.
#ifndef LOG_info
#error "include api.h before log_trace.h (LOG_* must be defined first)"
#endif

#include <stdbool.h>
#include <stdint.h>

// Initialize subsystem (records the monotonic app-start epoch used in log
// timestamps). Call once, right after RingLog_init().
void LogTrace_init(void);

// Milliseconds since LogTrace_init(), on CLOCK_MONOTONIC. Async-signal-safe.
// The crash handler reports meta.txt's uptime_ms from this so it agrees with
// the [mm:ss.mmm] stamps in log.txt.
uint32_t LogTrace_uptimeMs(void);

// Enable/disable ring capture. When disabled, LOG_* still reach NextUI's sink
// but nothing is appended to the ring (it could never be dumped anyway). Wired
// to the "Collect crash reports" setting; defaults to enabled so early-startup
// logs are kept until the setting is seeded.
void LogTrace_setCaptureEnabled(bool enabled);

// Internal: emit a wrapped LOG_note. Forwards to NextUI's original LOG_note
// (preserving its destination) AND appends a timestamped line to the ring.
void LogTrace_emit(int level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Internal: emit a ring-only trace line (no stdout / no NextUI sink).
void LogTrace_trace(const char* fmt, ...)
    __attribute__((format(printf, 1, 2)));

// Override NextUI's LOG_note. LOG_debug/info/warn/error in api.h all expand to
// LOG_note(level, ...), so this single override captures everything.
// #undef is harmless when LOG_note is a function symbol (no-op for non-macros).
#undef LOG_note
#define LOG_note(level, fmt, ...) LogTrace_emit((level), (fmt), ##__VA_ARGS__)

// New: fine-grained tracing for hot paths. Ring-only by design — no stdout.
#define LOG_trace(fmt, ...) LogTrace_trace((fmt), ##__VA_ARGS__)

#endif
