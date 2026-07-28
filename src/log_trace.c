// log_trace.c — implements the LOG_* ring tee.
//
// We deliberately do NOT include "log_trace.h" here. That keeps LOG_note as the
// real NextUI function inside this TU, so we can forward to it directly.

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "defines.h"      // NextUI: BTN_ID_COUNT and friends (required by api.h)
#include "api.h"          // NextUI: real LOG_note(int level, const char* fmt, ...)
#include "ring_log.h"

// App-start epoch, monotonic ms. Captured at LogTrace_init(), which runs
// immediately after RingLog_init() at startup. This is the SAME clock the crash
// handler reports uptime against (via LogTrace_uptimeMs), so log.txt line
// timestamps and meta.txt's uptime_ms agree — previously they used two
// different epochs (SDL vs CLOCK_MONOTONIC, captured seconds apart) and drifted.
static uint32_t app_start_ms = 0;

// Whether appends actually reach the ring. Defaults to on so early-startup logs
// are captured before CrashHandler_init() seeds the real value; once seeded it
// tracks "Collect crash reports". When off, the ring can never be dumped, so
// formatting + the spinlock memcpy would be pure waste — skip them entirely.
static atomic_int capture_enabled = 1;

// Async-signal-safe millisecond clock (CLOCK_MONOTONIC via vDSO on ARM64).
// Wraps at 2^32 ms like SDL_GetTicks(); unsigned subtraction stays correct.
static uint32_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

// --- Repeat-collapse state (defined here so LogTrace_init can reset it) --------
// A chatty loop (wget progress, decode-underrun warnings) can otherwise evict
// the entire pre-crash context from the 64 KB ring. When a message is identical
// to the immediately preceding one, suppress it and count it; on the next
// distinct message, emit a single "(previous line repeated N times)" summary
// first. State is global (the ring is one shared stream), guarded by its own
// spinlock so the compare/flush/append sequence is atomic across threads.
//
// Limitation: a run of repeats ending in a crash shows the line once with no
// summary, since the pending count is only flushed when a different line
// arrives and the signal handler cannot safely touch this state.
static atomic_flag dedup_lock = ATOMIC_FLAG_INIT;
static char     last_msg[256] = "";
static bool     last_valid = false;
static uint32_t repeat_count = 0;

static void dedup_spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&dedup_lock, memory_order_acquire)) { }
}
static void dedup_spin_unlock(void) {
    atomic_flag_clear_explicit(&dedup_lock, memory_order_release);
}

void LogTrace_init(void) {
    app_start_ms = monotonic_ms();
    // Reset repeat-collapse state so a re-init starts clean.
    dedup_spin_lock();
    last_valid = false;
    last_msg[0] = '\0';
    repeat_count = 0;
    dedup_spin_unlock();
}

uint32_t LogTrace_uptimeMs(void) {
    return monotonic_ms() - app_start_ms;
}

void LogTrace_setCaptureEnabled(bool enabled) {
    atomic_store_explicit(&capture_enabled, enabled ? 1 : 0, memory_order_relaxed);
}

static bool capture_on(void) {
    return atomic_load_explicit(&capture_enabled, memory_order_relaxed) != 0;
}

// Linux kernel thread id. Main thread's TID == process PID.
// On glibc, syscall(SYS_gettid) is async-signal-safe.
static int get_tid(void) {
    return (int)syscall(SYS_gettid);
}

// Per-thread scratch buffers — no heap allocation, no inter-thread contention.
static __thread char tl_msg[256];     // user message after vsnprintf
static __thread char tl_line[320];    // full ring entry: prefix + msg + '\n'

// Build "[mm:ss.mmm] [tid:NNNN] <msg>\n" and append to the ring. No dedup here.
static void ring_write_line(const char* msg) {
    uint32_t now = monotonic_ms() - app_start_ms;
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

// Dedup wrapper around ring_write_line.
static void ring_format_and_append(const char* msg) {
    dedup_spin_lock();

    if (last_valid && strcmp(msg, last_msg) == 0) {
        repeat_count++;
        dedup_spin_unlock();
        return;
    }

    // Distinct line: flush any pending repeats before recording the new one.
    if (repeat_count > 0) {
        char summary[64];
        snprintf(summary, sizeof(summary),
                 "(previous line repeated %u times)", repeat_count);
        repeat_count = 0;
        ring_write_line(summary);
    }

    size_t mlen = strnlen(msg, sizeof(last_msg) - 1);
    memcpy(last_msg, msg, mlen);
    last_msg[mlen] = '\0';
    last_valid = true;

    ring_write_line(msg);

    dedup_spin_unlock();
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

    // 1) Always forward to NextUI's original LOG_note — this is normal app
    //    logging, independent of crash-report capture. (LOG_note here is the
    //    real function; this TU does not include log_trace.h.)
    LOG_note(level, "%s", tl_msg);

    // 2) Tee to the ring only when capture is enabled.
    if (capture_on()) ring_format_and_append(tl_msg);
}

void LogTrace_trace(const char* fmt, ...) {
    // Ring-only: nothing to do at all when capture is off.
    if (!capture_on()) return;

    va_list ap;
    va_start(ap, fmt);
    bool ok = fill_tl_msg(fmt, ap);
    va_end(ap);
    if (!ok) return;

    ring_format_and_append(tl_msg);
}
