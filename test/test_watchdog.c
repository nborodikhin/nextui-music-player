// Characterization tests for src/watchdog.c — the pause-mask composition and
// heartbeat accessors. These exercise only the exported state machine (file-
// static atomics); they do NOT call Watchdog_init/quit, so no thread is spawned
// and abort() is never reached. Host-compiled (see test/Makefile).

#include "watchdog.h"
#include "test.h"

#include <stdarg.h>

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

int main(void) {
    printf("watchdog:\n");
    RUN(starts_unpaused);
    RUN(single_reason_pause_and_resume);
    RUN(distinct_reasons_compose);
    RUN(same_reason_is_idempotent);
    RUN(resume_of_unset_reason_is_noop);
    RUN(out_of_range_reason_is_ignored);
    RUN(all_reasons_set_then_cleared);
    RUN(heartbeat_updates_last_heartbeat_monotonically);
    return test_summary();
}
