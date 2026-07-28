#ifndef __RING_LOG_H__
#define __RING_LOG_H__

#include <stddef.h>
#include <sys/types.h>

// Fixed-size in-memory ring buffer for crash-report log capture.
// Receives all LOG_* output (via log_trace.h wrappers) and is dumped to
// disk by the crash signal handler. See spec/crash-reporting.md.

#define RING_LOG_SIZE (64 * 1024)  // 64 KB — ~500-1000 log lines

// Initialize the ring (called once at app start before any LOG_*).
// Idempotent: safe to call multiple times.
void RingLog_init(void);

// Append raw bytes (caller pre-formats the line, including trailing '\n').
// Thread-safe; spinlock-guarded. Wraps when full, overwriting the oldest content.
// If len > RING_LOG_SIZE, only the trailing RING_LOG_SIZE bytes are kept.
void RingLog_append(const char* data, size_t len);

// Dump current ring contents (oldest first) to file descriptor fd.
// Signal-handler-safe: try-locks twice; if still contended, dumps anyway
// (best-effort racy snapshot beats no snapshot).
// Returns total bytes written, or -1 on write error.
ssize_t RingLog_dump(int fd);

// Total bytes ever appended (monotonic). For tests and meta.txt.
size_t RingLog_totalAppended(void);

#endif
