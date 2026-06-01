// log_trace.c — implements the LOG_* ring tee.
//
// We deliberately do NOT include "log_trace.h" here. That keeps LOG_note as the
// real NextUI function inside this TU, so we can forward to it directly.

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "defines.h"      // NextUI: BTN_ID_COUNT and friends (required by api.h)
#include "api.h"          // NextUI: real LOG_note(int level, const char* fmt, ...)
#include "ring_log.h"

static uint32_t app_start_ticks = 0;

void LogTrace_init(void) {
    app_start_ticks = SDL_GetTicks();
}

// Linux kernel thread id. Main thread's TID == process PID.
// On glibc, syscall(SYS_gettid) is async-signal-safe.
static int get_tid(void) {
    return (int)syscall(SYS_gettid);
}

// Per-thread scratch buffers — no heap allocation, no inter-thread contention.
static __thread char tl_msg[256];     // user message after vsnprintf
static __thread char tl_line[320];    // full ring entry: prefix + msg + '\n'

// Build "[mm:ss.mmm] [tid:NNNN] <msg>\n" and append to the ring.
static void ring_format_and_append(const char* msg) {
    uint32_t now = SDL_GetTicks() - app_start_ticks;
    unsigned mm  = now / 60000u;
    unsigned ss  = (now / 1000u) % 60u;
    unsigned mmm = now % 1000u;
    int tid = get_tid();

    int n = snprintf(tl_line, sizeof(tl_line),
                     "[%02u:%02u.%03u] [tid:%d] %s\n",
                     mm, ss, mmm, tid, msg);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(tl_line)) {
        n = (int)sizeof(tl_line) - 1;
        tl_line[n - 1] = '\n';  // preserve newline boundary on truncation
    }
    RingLog_append(tl_line, (size_t)n);
}

// Format fmt/ap into tl_msg and strip trailing newlines (the ring format owns
// line termination). Returns false if formatting failed — caller emits nothing.
static bool fill_tl_msg(const char* fmt, va_list ap) {
    int n = vsnprintf(tl_msg, sizeof(tl_msg), fmt, ap);
    if (n < 0) return false;

    size_t msg_len = (size_t)n;
    if (msg_len >= sizeof(tl_msg)) msg_len = sizeof(tl_msg) - 1;
    while (msg_len > 0 && tl_msg[msg_len - 1] == '\n') {
        tl_msg[--msg_len] = '\0';
    }
    return true;
}

void LogTrace_emit(int level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    bool ok = fill_tl_msg(fmt, ap);
    va_end(ap);
    if (!ok) return;

    // 1) Forward to NextUI's original LOG_note with already-formatted message.
    //    LOG_note here is the real function (this TU does not include log_trace.h).
    LOG_note(level, "%s", tl_msg);

    // 2) Append to ring.
    ring_format_and_append(tl_msg);
}

void LogTrace_trace(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    bool ok = fill_tl_msg(fmt, ap);
    va_end(ap);
    if (!ok) return;

    ring_format_and_append(tl_msg);
}
