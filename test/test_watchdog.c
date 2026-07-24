// Characterization tests for src/watchdog.c — the pause-mask composition, the
// heartbeat accessors, and the main-thread scoping. These exercise only the
// exported state machine (file-static atomics); they do NOT call Watchdog_init/
// quit, so the dog thread is never spawned and abort() is never reached. The
// thread-scoping tests use Watchdog_bindMainThread(), which binds without
// starting the thread. Host-compiled (see test/Makefile).

#include "watchdog.h"
#include "test.h"

#include <pthread.h>
#include <stdarg.h>
#include <time.h>

// watchdog.c pulls in the real src/log_trace.h (its own dir wins for quoted
// includes), so LOG_trace expands to LogTrace_trace. We never assert on log
// output here, so a no-op stub satisfies the linker.
void LogTrace_trace(const char* fmt, ...) { (void)fmt; }

// No public reset, so clear the mask by resuming every reason. A correct
// resume(unset) is a no-op, so this is a safe per-test precondition.
static void reset_mask(void) {
    for (int r = 0; r < WATCHDOG_REASON_COUNT; r++) {
        Watchdog_resume((WatchdogPauseReason)r, false);
    }
}

TEST(starts_unpaused) {
    reset_mask();
    CHECK(!Watchdog_isPaused());
}

TEST(single_reason_pause_and_resume) {
    reset_mask();
    Watchdog_pause(WATCHDOG_REASON_KEYBOARD, false);
    CHECK(Watchdog_isPaused());
    Watchdog_resume(WATCHDOG_REASON_KEYBOARD, false);
    CHECK(!Watchdog_isPaused());
}

TEST(distinct_reasons_compose) {
    reset_mask();
    Watchdog_pause(WATCHDOG_REASON_KEYBOARD, false);
    Watchdog_pause(WATCHDOG_REASON_HTTP_FETCH, false);
    CHECK(Watchdog_isPaused());
    // Releasing one reason leaves the dog muzzled while the other holds.
    Watchdog_resume(WATCHDOG_REASON_KEYBOARD, false);
    CHECK(Watchdog_isPaused());
    Watchdog_resume(WATCHDOG_REASON_HTTP_FETCH, false);
    CHECK(!Watchdog_isPaused());
}

TEST(same_reason_is_idempotent) {
    reset_mask();
    Watchdog_pause(WATCHDOG_REASON_DEEP_SLEEP, false);
    Watchdog_pause(WATCHDOG_REASON_DEEP_SLEEP, false);
    CHECK(Watchdog_isPaused());
    // A single resume clears it — pausing twice does not require two resumes.
    Watchdog_resume(WATCHDOG_REASON_DEEP_SLEEP, false);
    CHECK(!Watchdog_isPaused());
}

TEST(resume_of_unset_reason_is_noop) {
    reset_mask();
    Watchdog_resume(WATCHDOG_REASON_SUBPROCESS, false);
    CHECK(!Watchdog_isPaused());
}

TEST(out_of_range_reason_is_ignored) {
    reset_mask();
    Watchdog_pause(WATCHDOG_REASON_COUNT, false);          // == COUNT, guarded out
    Watchdog_pause((WatchdogPauseReason)999, false);       // far out of range
    CHECK(!Watchdog_isPaused());
    Watchdog_resume(WATCHDOG_REASON_COUNT, false);         // must not crash
    CHECK(!Watchdog_isPaused());
}

TEST(all_reasons_set_then_cleared) {
    reset_mask();
    for (int r = 0; r < WATCHDOG_REASON_COUNT; r++) {
        Watchdog_pause((WatchdogPauseReason)r, false);
    }
    CHECK(Watchdog_isPaused());
    // Clear all but the last; still paused. Then clear the last.
    for (int r = 0; r < WATCHDOG_REASON_COUNT - 1; r++) {
        Watchdog_resume((WatchdogPauseReason)r, false);
    }
    CHECK(Watchdog_isPaused());
    Watchdog_resume((WatchdogPauseReason)(WATCHDOG_REASON_COUNT - 1), false);
    CHECK(!Watchdog_isPaused());
}

TEST(heartbeat_updates_last_heartbeat_monotonically) {
    Watchdog_heartbeat();
    unsigned int a = Watchdog_lastHeartbeatMs();
    CHECK(a != 0);
    Watchdog_heartbeat();
    unsigned int b = Watchdog_lastHeartbeatMs();
    // CLOCK_MONOTONIC ms never goes backwards within a run.
    CHECK(b >= a);
}

// ---- main-thread scoping ----------------------------------------------------
// The dog watches the main loop only. A worker must not be able to forge a
// heartbeat (which would mask a real hang) or clear a pause reason main holds
// (which would abort() a healthy app). See the THREAD SCOPE note in watchdog.h.

// Runs a function on a throwaway thread and waits for it.
static void* run_off_main(void* (*fn)(void*)) {
    pthread_t t;
    void* ret = NULL;
    CHECK(pthread_create(&t, NULL, fn, NULL) == 0);
    pthread_join(t, &ret);
    return ret;
}

static void* worker_heartbeats(void* arg) {
    (void)arg;
    Watchdog_heartbeat();
    return NULL;
}

static void* worker_pauses(void* arg) {
    (void)arg;
    Watchdog_pause(WATCHDOG_REASON_HTTP_FETCH, false);
    return NULL;
}

static void* worker_resumes(void* arg) {
    (void)arg;
    Watchdog_resume(WATCHDOG_REASON_HTTP_FETCH, false);
    return NULL;
}

TEST(worker_heartbeat_is_ignored) {
    Watchdog_bindMainThread();
    Watchdog_heartbeat();
    unsigned int before = Watchdog_lastHeartbeatMs();

    // Let the clock advance so a forged heartbeat would be visibly newer.
    struct timespec nap = { 0, 20 * 1000 * 1000 };  // 20 ms
    nanosleep(&nap, NULL);

    run_off_main(worker_heartbeats);
    CHECK(Watchdog_lastHeartbeatMs() == before);

    // Sanity: the same call from main still moves it.
    Watchdog_heartbeat();
    CHECK(Watchdog_lastHeartbeatMs() != before);
}

TEST(worker_pause_is_ignored) {
    Watchdog_bindMainThread();
    reset_mask();
    run_off_main(worker_pauses);
    CHECK(!Watchdog_isPaused());
}

TEST(worker_cannot_clear_a_pause_held_by_main) {
    Watchdog_bindMainThread();
    reset_mask();
    // Main holds HTTP_FETCH — e.g. blocked in Wifi_ensureConnected().
    Watchdog_pause(WATCHDOG_REASON_HTTP_FETCH, false);
    CHECK(Watchdog_isPaused());
    // The podcast download worker finishes its own wifi check and resumes.
    run_off_main(worker_resumes);
    // Main's hold must survive, or the dog re-arms mid-block and aborts.
    CHECK(Watchdog_isPaused());
    Watchdog_resume(WATCHDOG_REASON_HTTP_FETCH, false);
    CHECK(!Watchdog_isPaused());
}

TEST(worker_resume_does_not_forge_a_heartbeat) {
    Watchdog_bindMainThread();
    reset_mask();
    Watchdog_heartbeat();
    unsigned int before = Watchdog_lastHeartbeatMs();

    struct timespec nap = { 0, 20 * 1000 * 1000 };  // 20 ms
    nanosleep(&nap, NULL);

    // Watchdog_resume() re-arms the heartbeat, so an unscoped resume from a
    // worker would keep feeding the dog while main is wedged.
    run_off_main(worker_resumes);
    CHECK(Watchdog_lastHeartbeatMs() == before);
}

TEST(unbound_watchdog_accepts_any_thread) {
    // Before binding there is no dog, so every caller counts as main. This is
    // what lets the pause-mask tests above run without Watchdog_init().
    reset_mask();
    run_off_main(worker_pauses);
    CHECK(Watchdog_isPaused());
    run_off_main(worker_resumes);
    CHECK(!Watchdog_isPaused());
}

int main(void) {
    printf("watchdog:\n");
    // Must run before anything binds a main thread — it asserts the unbound
    // (permissive) behavior, and binding is deliberately one-way.
    RUN(unbound_watchdog_accepts_any_thread);
    RUN(starts_unpaused);
    RUN(single_reason_pause_and_resume);
    RUN(distinct_reasons_compose);
    RUN(same_reason_is_idempotent);
    RUN(resume_of_unset_reason_is_noop);
    RUN(out_of_range_reason_is_ignored);
    RUN(all_reasons_set_then_cleared);
    RUN(heartbeat_updates_last_heartbeat_monotonically);
    RUN(worker_heartbeat_is_ignored);
    RUN(worker_pause_is_ignored);
    RUN(worker_cannot_clear_a_pause_held_by_main);
    RUN(worker_resume_does_not_forge_a_heartbeat);
    return test_summary();
}
