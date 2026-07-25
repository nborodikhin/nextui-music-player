#ifndef __CRASH_META_H__
#define __CRASH_META_H__

#include <stddef.h>
#include <stdint.h>

// Pure formatting half of meta.txt. Deliberately dependency-free (no SDL, no
// player/background/fb_capture) so the exact bytes of meta.txt can be tested on
// the host without a device — see test/test_crash_meta.c.
//
// crash_handler.c owns the *gathering* half: it snapshots the live subsystems
// into a CrashMeta and calls CrashMeta_format(). Keeping the two apart is what
// makes the "frozen format" boundary in spec/crash-reporting.md enforceable.

// Plain-old-data snapshot of everything meta.txt reports. All string fields are
// borrowed pointers valid for the duration of the CrashMeta_format() call; NULL
// is tolerated and rendered as an empty value (except last_input_button, which
// renders as "none" — see below).
typedef struct {
    const char* version;             // app version, e.g. "1.4.2"
    const char* platform;            // build PLATFORM string, e.g. "tg5040"
    const char* signal_name;         // "SIGSEGV", ...
    int         signal_number;
    uint32_t    uptime_ms;
    const char* last_input_button;   // NULL/empty renders as "none"
    uint32_t    last_input_age_ms;
    uint32_t    heartbeat_age_ms;
    size_t      ring_total_bytes;
    int         screen_width;
    int         screen_height;
    int         screen_bpp;
    const char* audio_state;         // "playing" | "paused" | "stopped" | "unknown"
    const char* audio_background;    // "music" | "radio" | "podcast" | "none" | "unknown"
    int         audio_position_ms;
    int         audio_duration_ms;
    const char* audio_track;         // raw filesystem path (PII — opt-in gated)
} CrashMeta;

// Render `m` as meta.txt's `key: value\n` lines into `out` (always NUL-terminated
// when n > 0). Returns the byte length written, excluding the NUL, clamped to
// n-1 on truncation. Async-signal-safe in the same sense as the rest of the
// handler: no allocation, no locks, snprintf only.
//
// The key set and their order are a frozen format — changing them requires the
// sign-off described in spec/crash-reporting.md.
size_t CrashMeta_format(const CrashMeta* m, char* out, size_t n);

// Map a signal number to the name recorded in meta.txt. Unknown signals render
// as "SIGNAL" (the number is reported separately).
const char* CrashMeta_signalName(int signo);

#endif
