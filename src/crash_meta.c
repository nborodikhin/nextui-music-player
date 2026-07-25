#include <signal.h>
#include <stdio.h>

#include "crash_meta.h"

// Render a borrowed string field, tolerating NULL. Keeps the snprintf argument
// list readable and guarantees we never hand "%s" a null pointer.
static const char* or_empty(const char* s) {
    return s ? s : "";
}

const char* CrashMeta_signalName(int signo) {
    switch (signo) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGUSR1: return "SIGUSR1";
        default:      return "SIGNAL";
    }
}

size_t CrashMeta_format(const CrashMeta* m, char* out, size_t n) {
    if (!out || n == 0) return 0;
    if (!m) { out[0] = '\0'; return 0; }

    const char* btn = or_empty(m->last_input_button);

    int r = snprintf(out, n,
        "version: %s\n"
        "platform: %s\n"
        "signal: %s (%d)\n"
        "uptime_ms: %u\n"
        "last_input_button: %s\n"
        "last_input_age_ms: %u\n"
        "heartbeat_age_ms: %u\n"
        "ring_total_bytes: %zu\n"
        "screen_width: %d\n"
        "screen_height: %d\n"
        "screen_bpp: %d\n"
        "audio_state: %s\n"
        "audio_background: %s\n"
        "audio_position_ms: %d\n"
        "audio_duration_ms: %d\n"
        "audio_track: %s\n",
        or_empty(m->version),
        or_empty(m->platform),
        or_empty(m->signal_name), m->signal_number,
        m->uptime_ms,
        btn[0] ? btn : "none",
        m->last_input_age_ms,
        m->heartbeat_age_ms,
        m->ring_total_bytes,
        m->screen_width,
        m->screen_height,
        m->screen_bpp,
        or_empty(m->audio_state),
        or_empty(m->audio_background),
        m->audio_position_ms,
        m->audio_duration_ms,
        or_empty(m->audio_track)
    );

    if (r < 0) { out[0] = '\0'; return 0; }
    return (size_t)r < n ? (size_t)r : n - 1;   // clamp on truncation
}
