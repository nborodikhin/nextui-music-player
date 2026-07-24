// Tests for src/log_trace.c — the repeat-collapse and capture-gate logic added
// for crash reporting. log_trace.c no longer depends on SDL, so it compiles on
// the host against the stub api.h (LOG_note is a no-op there) plus the real
// ring_log.c. We drive LOG_trace / LogTrace_emit and read the ring back to
// assert what was captured.

#include "api.h"        // stub: defines LOG_* so log_trace.h's include-order guard passes
#include "log_trace.h"
#include "ring_log.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Dump the ring into a NUL-terminated buffer for substring assertions.
static void dump_ring(char* out, size_t cap) {
    FILE* tmp = tmpfile();
    if (!tmp) { perror("tmpfile"); exit(2); }
    int fd = fileno(tmp);
    RingLog_dump(fd);
    rewind(tmp);
    size_t n = fread(out, 1, cap - 1, tmp);
    out[n] = '\0';
    fclose(tmp);
}

// Count non-overlapping occurrences of needle in haystack.
static int count_occurrences(const char* hay, const char* needle) {
    int c = 0;
    size_t nlen = strlen(needle);
    for (const char* p = hay; (p = strstr(p, needle)) != NULL; p += nlen) c++;
    return c;
}

// Fresh ring + log state before each test.
static void reset(void) {
    RingLog_init();
    LogTrace_init();                 // also clears repeat-collapse state
    LogTrace_setCaptureEnabled(true);
}

TEST(distinct_lines_are_all_captured) {
    reset();
    LogTrace_trace("alpha");
    LogTrace_trace("bravo");
    LogTrace_trace("charlie");

    char buf[4096];
    dump_ring(buf, sizeof(buf));
    CHECK(count_occurrences(buf, "alpha") == 1);
    CHECK(count_occurrences(buf, "bravo") == 1);
    CHECK(count_occurrences(buf, "charlie") == 1);
    CHECK(count_occurrences(buf, "repeated") == 0);
}

TEST(consecutive_duplicates_are_collapsed) {
    reset();
    LogTrace_trace("spinning");
    LogTrace_trace("spinning");
    LogTrace_trace("spinning");
    LogTrace_trace("spinning");   // 1 emitted + 3 suppressed
    LogTrace_trace("done");       // distinct → flushes the summary, then emits

    char buf[4096];
    dump_ring(buf, sizeof(buf));
    // The line itself appears once (the first occurrence), not four times.
    CHECK(count_occurrences(buf, "spinning") == 1);
    // A single summary names the 3 suppressed repeats.
    CHECK(count_occurrences(buf, "previous line repeated 3 times") == 1);
    CHECK(count_occurrences(buf, "done") == 1);
}

TEST(pending_repeats_without_a_following_line_show_once) {
    reset();
    // A run of repeats with no distinct line after it (e.g. right before a
    // crash): the line is present once, and there is no premature summary.
    LogTrace_trace("stuck");
    LogTrace_trace("stuck");
    LogTrace_trace("stuck");

    char buf[4096];
    dump_ring(buf, sizeof(buf));
    CHECK(count_occurrences(buf, "stuck") == 1);
    CHECK(count_occurrences(buf, "repeated") == 0);
}

TEST(alternating_lines_are_not_collapsed) {
    reset();
    LogTrace_trace("ping");
    LogTrace_trace("pong");
    LogTrace_trace("ping");
    LogTrace_trace("pong");

    char buf[4096];
    dump_ring(buf, sizeof(buf));
    CHECK(count_occurrences(buf, "ping") == 2);
    CHECK(count_occurrences(buf, "pong") == 2);
    CHECK(count_occurrences(buf, "repeated") == 0);
}

TEST(capture_disabled_appends_nothing) {
    reset();
    LogTrace_setCaptureEnabled(false);
    LogTrace_trace("should_not_appear");
    LogTrace_emit(0, "also_should_not_appear");   // LOG_note forward is a no-op stub

    CHECK_EQ_SZ(RingLog_totalAppended(), 0);

    char buf[256];
    dump_ring(buf, sizeof(buf));
    CHECK(count_occurrences(buf, "should_not_appear") == 0);
}

TEST(capture_reenabled_resumes_appending) {
    reset();
    LogTrace_setCaptureEnabled(false);
    LogTrace_trace("dropped");
    LogTrace_setCaptureEnabled(true);
    LogTrace_trace("kept");

    char buf[1024];
    dump_ring(buf, sizeof(buf));
    CHECK(count_occurrences(buf, "dropped") == 0);
    CHECK(count_occurrences(buf, "kept") == 1);
}

TEST(uptime_is_monotonic_nonzero) {
    LogTrace_init();
    uint32_t a = LogTrace_uptimeMs();
    struct timespec nap = { 0, 5 * 1000 * 1000 };  // 5 ms
    nanosleep(&nap, NULL);
    uint32_t b = LogTrace_uptimeMs();
    CHECK(b >= a);
    CHECK(b < 60000u);   // sanity: a test run is not minutes long
}

int main(void) {
    printf("log_trace:\n");
    RUN(distinct_lines_are_all_captured);
    RUN(consecutive_duplicates_are_collapsed);
    RUN(pending_repeats_without_a_following_line_show_once);
    RUN(alternating_lines_are_not_collapsed);
    RUN(capture_disabled_appends_nothing);
    RUN(capture_reenabled_resumes_appending);
    RUN(uptime_is_monotonic_nonzero);
    return test_summary();
}
