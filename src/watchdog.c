#include "watchdog.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "defines.h"
#include "api.h"
#include "log_trace.h"

#define DEFAULT_THRESHOLD_MS 5000u

static pthread_t watchdog_thread;
static bool watchdog_running = false;
static bool watchdog_started = false;

// Monotonic last-known-good main-loop tick. Read by the dog, written by main.
static _Atomic uint32_t heartbeat_ms = 0;

// Bitmask of WATCHDOG_REASON_* — non-zero means the dog is muzzled.
static _Atomic uint32_t pause_mask = 0;

// Stall budget before abort() is called.
static uint32_t threshold_ms = DEFAULT_THRESHOLD_MS;

static void* watchdog_loop(void* arg) {
    (void)arg;
    while (atomic_load_explicit(&heartbeat_ms, memory_order_relaxed) == 0) {
        // Wait for the first heartbeat before arming. Avoids tripping during the
        // window between Watchdog_init() and the main loop's first iteration.
        sleep(1);
        if (!watchdog_running) return NULL;
    }

    while (watchdog_running) {
        sleep(1);
        if (!watchdog_running) break;

        // Any pause reason set -> hold off.
        if (Watchdog_isPaused()) continue;

        uint32_t hb = atomic_load_explicit(&heartbeat_ms, memory_order_relaxed);
        uint32_t now = SDL_GetTicks();
        if (now - hb > threshold_ms) {
            fprintf(stderr,
                "watchdog: main loop stalled, age=%u ms (threshold=%u ms)\n",
                (unsigned)(now - hb), (unsigned)threshold_ms);
            abort();
        }
    }
    return NULL;
}

void Watchdog_init(unsigned override_threshold_ms) {
    if (watchdog_started) return;
    threshold_ms = override_threshold_ms ? override_threshold_ms : DEFAULT_THRESHOLD_MS;
    atomic_store_explicit(&heartbeat_ms, 0, memory_order_relaxed);
    atomic_store_explicit(&pause_mask, 0, memory_order_relaxed);
    watchdog_running = true;
    if (pthread_create(&watchdog_thread, NULL, watchdog_loop, NULL) == 0) {
        watchdog_started = true;
    } else {
        watchdog_running = false;
    }
}

void Watchdog_quit(void) {
    if (!watchdog_started) return;
    watchdog_running = false;
    pthread_join(watchdog_thread, NULL);
    watchdog_started = false;
}

void Watchdog_heartbeat(void) {
    atomic_store_explicit(&heartbeat_ms, SDL_GetTicks(), memory_order_relaxed);
}

static const char* reason_name(WatchdogPauseReason r) {
    switch (r) {
        case WATCHDOG_REASON_KEYBOARD:         return "KEYBOARD";
        case WATCHDOG_REASON_DISPLAY_RECOVERY: return "DISPLAY_RECOVERY";
        case WATCHDOG_REASON_DEEP_SLEEP:       return "DEEP_SLEEP";
        case WATCHDOG_REASON_HTTP_FETCH:       return "HTTP_FETCH";
        case WATCHDOG_REASON_SUBPROCESS:       return "SUBPROCESS";
        default:                               return "?";
    }
}

void Watchdog_pause(WatchdogPauseReason reason, bool trace) {
    if ((unsigned)reason >= (unsigned)WATCHDOG_REASON_COUNT) return;
    atomic_fetch_or_explicit(&pause_mask, 1u << (unsigned)reason, memory_order_relaxed);
    if (trace) {
        LOG_trace("Watchdog_pause: %s", reason_name(reason));
    }
}

void Watchdog_resume(WatchdogPauseReason reason, bool trace) {
    if ((unsigned)reason >= (unsigned)WATCHDOG_REASON_COUNT) return;
    atomic_fetch_and_explicit(&pause_mask, ~(1u << (unsigned)reason), memory_order_relaxed);
    // Re-arm the heartbeat so we don't immediately trip on a stale value after a
    // long pause (deep sleep, keyboard hold, ...).
    Watchdog_heartbeat();
    if (trace) {
        uint32_t mask_after = atomic_load_explicit(&pause_mask, memory_order_relaxed);
        LOG_trace("Watchdog_resume: %s mask_after=0x%x", reason_name(reason), (unsigned)mask_after);
    }
}

bool Watchdog_isPaused(void) {
    return atomic_load_explicit(&pause_mask, memory_order_relaxed) != 0;
}

unsigned int Watchdog_lastHeartbeatMs(void) {
    return (unsigned int)atomic_load_explicit(&heartbeat_ms, memory_order_relaxed);
}
