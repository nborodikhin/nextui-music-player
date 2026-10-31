// The count of a stopwatch that can stop and continue.

#include "test.h"
#include "stopwatch.h"

// A stopwatch that runs from `now` with a count of 0
static Stopwatch started_at(uint32_t now) {
    Stopwatch s;
    Stopwatch_reset(&s);
    Stopwatch_start(&s, now);
    return s;
}

TEST(a_reset_stopwatch_counts_nothing) {
    Stopwatch s;
    Stopwatch_reset(&s);
    CHECK(Stopwatch_accumulated(&s, 5000) == 0);
    CHECK(Stopwatch_accumulated(&s, 9000) == 0);
}

TEST(starts_at_zero) {
    Stopwatch s = started_at(5000);
    CHECK(Stopwatch_accumulated(&s, 5000) == 0);
    CHECK(Stopwatch_accumulated(&s, 6000) == 1000);
}

TEST(a_stop_keeps_the_count_through_a_long_wait) {
    Stopwatch s = started_at(0);
    Stopwatch_stop(&s, 1000);
    CHECK(Stopwatch_accumulated(&s, 1000) == 1000);
    CHECK(Stopwatch_accumulated(&s, 600000) == 1000);

    // The count continues from the stop
    Stopwatch_start(&s, 600000);
    CHECK(Stopwatch_accumulated(&s, 600000) == 1000);
    CHECK(Stopwatch_accumulated(&s, 601000) == 2000);
}

TEST(a_call_with_no_change_changes_nothing) {
    Stopwatch s = started_at(0);
    Stopwatch_start(&s, 500);
    CHECK(Stopwatch_accumulated(&s, 1000) == 1000);

    Stopwatch_stop(&s, 1000);
    Stopwatch_stop(&s, 3000);
    CHECK(Stopwatch_accumulated(&s, 5000) == 1000);
}

TEST(a_reset_clears_the_count) {
    Stopwatch s = started_at(0);
    Stopwatch_stop(&s, 1000);
    Stopwatch_reset(&s);
    CHECK(Stopwatch_accumulated(&s, 2000) == 0);

    Stopwatch_start(&s, 2000);
    CHECK(Stopwatch_accumulated(&s, 2500) == 500);
}

TEST(the_clock_of_the_platform_can_wrap) {
    Stopwatch s = started_at(UINT32_MAX - 499);
    CHECK(Stopwatch_accumulated(&s, 500) == 1000);

    Stopwatch_stop(&s, 500);
    Stopwatch_start(&s, UINT32_MAX - 999);
    CHECK(Stopwatch_accumulated(&s, 0) == 2000);
}

TEST(the_count_goes_past_the_range_of_the_platform_clock) {
    Stopwatch s = started_at(0);
    // 60 days, in runs that each stay inside the 32-bit clock
    uint32_t now = 0;
    for (int day = 0; day < 60; day++) {
        now += 86400000u;
        Stopwatch_stop(&s, now);
        Stopwatch_start(&s, now);
    }
    CHECK(Stopwatch_accumulated(&s, now) == 60ull * 86400000u);
}

int main(void) {
    RUN(a_reset_stopwatch_counts_nothing);
    RUN(starts_at_zero);
    RUN(a_stop_keeps_the_count_through_a_long_wait);
    RUN(a_call_with_no_change_changes_nothing);
    RUN(a_reset_clears_the_count);
    RUN(the_clock_of_the_platform_can_wrap);
    RUN(the_count_goes_past_the_range_of_the_platform_clock);
    return test_summary();
}
