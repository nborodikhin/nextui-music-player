#include "ring_log.h"

#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

// Static buffer; allocated by the loader at app start, never freed.
static char ring[RING_LOG_SIZE];

// Next-write offset, [0, RING_LOG_SIZE).
static size_t head = 0;

// Monotonic total. When >= RING_LOG_SIZE, the buffer has wrapped.
static size_t total_appended = 0;

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
    head = 0;
    total_appended = 0;
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

    size_t first_chunk = RING_LOG_SIZE - head;
    if (len <= first_chunk) {
        memcpy(ring + head, data, len);
        head += len;
        if (head == RING_LOG_SIZE) head = 0;
    } else {
        memcpy(ring + head, data, first_chunk);
        memcpy(ring, data + first_chunk, len - first_chunk);
        head = len - first_chunk;
    }
    total_appended += len;

    spin_unlock();
}

ssize_t RingLog_dump(int fd) {
    // Signal-handler discipline: best-effort lock with one retry, then dump anyway.
    int locked = spin_trylock();
    if (!locked) locked = spin_trylock();

    ssize_t total = 0;

    if (total_appended < RING_LOG_SIZE) {
        // Not yet wrapped: ring[0 .. total_appended] is everything ever written.
        ssize_t n = write(fd, ring, total_appended);
        if (n < 0) {
            if (locked) spin_unlock();
            return -1;
        }
        total += n;
    } else {
        // Wrapped: oldest byte is at `head`, newest is just before `head`.
        // Write [head .. end), then [0 .. head).
        ssize_t n1 = write(fd, ring + head, RING_LOG_SIZE - head);
        if (n1 < 0) {
            if (locked) spin_unlock();
            return -1;
        }
        total += n1;

        if (head > 0) {
            ssize_t n2 = write(fd, ring, head);
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
    return total_appended;
}
