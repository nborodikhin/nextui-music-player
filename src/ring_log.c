#include "ring_log.h"

#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

// Static buffer; allocated by the loader at app start, never freed.
static char ring[RING_LOG_SIZE];

// Next-write offset, [0, RING_LOG_SIZE). Atomic because RingLog_dump reads it
// without the lock whenever its try-lock fails — exactly the dump-anyway case
// the design is built around. On ARM64 aligned word loads are atomic in
// practice, but a plain size_t is formally a data race the compiler may fuse or
// reload; relaxed atomics compile to the same ldr/str here, so this is free.
static _Atomic size_t head = 0;

// Monotonic total. When >= RING_LOG_SIZE, the buffer has wrapped. Atomic for the
// same lock-free-dump reason as head.
static _Atomic size_t total_appended = 0;

// Spinlock. Held briefly for each append; the signal-handler dump uses
// try-acquire so a deadlocked or crashed writer can't keep us out.
static atomic_flag ring_lock = ATOMIC_FLAG_INIT;

static int spin_trylock(void) {
    // atomic_flag_test_and_set returns the PREVIOUS value:
    // false → we just acquired the lock.
    return !atomic_flag_test_and_set_explicit(&ring_lock, memory_order_acquire);
}

static void spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&ring_lock, memory_order_acquire)) {
        // brief spin — appends are tiny (memcpy of ~100 bytes)
    }
}

static void spin_unlock(void) {
    atomic_flag_clear_explicit(&ring_lock, memory_order_release);
}

void RingLog_init(void) {
    atomic_store_explicit(&head, 0, memory_order_relaxed);
    atomic_store_explicit(&total_appended, 0, memory_order_relaxed);
    atomic_flag_clear(&ring_lock);
}

void RingLog_append(const char* data, size_t len) {
    if (!data || len == 0) return;

    // Caller gave more than the buffer; keep the tail only.
    if (len > RING_LOG_SIZE) {
        data += len - RING_LOG_SIZE;
        len = RING_LOG_SIZE;
    }

    spin_lock();

    // Writers are serialized by the lock, so a plain relaxed load/store pair is
    // enough — the atomicity is only for the lock-free reader in RingLog_dump.
    size_t h = atomic_load_explicit(&head, memory_order_relaxed);
    size_t first_chunk = RING_LOG_SIZE - h;
    if (len <= first_chunk) {
        memcpy(ring + h, data, len);
        h += len;
        if (h == RING_LOG_SIZE) h = 0;
    } else {
        memcpy(ring + h, data, first_chunk);
        memcpy(ring, data + first_chunk, len - first_chunk);
        h = len - first_chunk;
    }
    atomic_store_explicit(&head, h, memory_order_relaxed);

    size_t total = atomic_load_explicit(&total_appended, memory_order_relaxed);
    atomic_store_explicit(&total_appended, total + len, memory_order_relaxed);

    spin_unlock();
}

// Write all `len` bytes of `buf`, looping on short writes. Returns the count
// written (== len) or -1 on a hard error. Regular-file writes to the SD card can
// legitimately return short, and sa_flags=0 (no SA_RESTART) makes an EINTR short
// write possible too — a single write() would silently truncate and, in the
// wrapped case, splice the two segments out of order.
static ssize_t write_all(int fd, const char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) return -1;
        if (n == 0) break;  // no progress; give up rather than spin
        off += (size_t)n;
    }
    return (ssize_t)off;
}

ssize_t RingLog_dump(int fd) {
    // Signal-handler discipline: best-effort lock with one retry, then dump anyway.
    int locked = spin_trylock();
    if (!locked) locked = spin_trylock();

    ssize_t total = 0;

    // Snapshot the atomics once. Without the lock these can race a concurrent
    // writer, but the dump is a best-effort crash snapshot and the values are
    // internally consistent enough (a torn tail beats no log).
    size_t appended = atomic_load_explicit(&total_appended, memory_order_relaxed);
    size_t h = atomic_load_explicit(&head, memory_order_relaxed);

    if (appended < RING_LOG_SIZE) {
        // Not yet wrapped: ring[0 .. appended] is everything ever written.
        ssize_t n = write_all(fd, ring, appended);
        if (n < 0) {
            if (locked) spin_unlock();
            return -1;
        }
        total += n;
    } else {
        // Wrapped: oldest byte is at `h`, newest is just before `h`.
        // Write [h .. end), then [0 .. h).
        ssize_t n1 = write_all(fd, ring + h, RING_LOG_SIZE - h);
        if (n1 < 0) {
            if (locked) spin_unlock();
            return -1;
        }
        total += n1;

        if (h > 0) {
            ssize_t n2 = write_all(fd, ring, h);
            if (n2 < 0) {
                if (locked) spin_unlock();
                return -1;
            }
            total += n2;
        }
    }

    if (locked) spin_unlock();
    return total;
}

size_t RingLog_totalAppended(void) {
    return atomic_load_explicit(&total_appended, memory_order_relaxed);
}
