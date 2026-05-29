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

// Initialize subsystem (records app-start time used in log timestamps).
// Call once after RingLog_init() and SDL_GetTicks() is usable.
void LogTrace_init(void);

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
