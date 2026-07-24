// Tests for src/utc_time.c — the integer civil-time conversion that replaced
// localtime_r() in the crash handler (which could deadlock against glibc's
// malloc arena lock; see spec/crash-reporting.md > Signal Handler).
//
// Two things matter here and both are covered below:
//   1. Correctness of the date math, especially leap years and month ends.
//   2. That the formatted directory name stays ISO-sortable — the newest-bundle
//      logic in CrashHandler_findUnsentBundle() picks by strcmp, so a wrong
//      zero-pad or field order would silently select the wrong bundle.
//
// The oracle is the C library's own gmtime_r: unsafe in a signal handler, but
// perfectly good in a test process.

#include "utc_time.h"
#include "test.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void check_fields(int64_t secs, int year, int mon, int day,
                         int hour, int min, int sec) {
    UtcTime t;
    UtcTime_fromUnix(secs, &t);
    CHECK(t.year == year);
    CHECK(t.mon  == mon);
    CHECK(t.day  == day);
    CHECK(t.hour == hour);
    CHECK(t.min  == min);
    CHECK(t.sec  == sec);
    if (t.year != year || t.mon != mon || t.day != day ||
        t.hour != hour || t.min != min || t.sec != sec) {
        printf("      secs=%lld -> %04d-%02d-%02d %02d:%02d:%02d, want %04d-%02d-%02d %02d:%02d:%02d\n",
               (long long)secs, t.year, t.mon, t.day, t.hour, t.min, t.sec,
               year, mon, day, hour, min, sec);
    }
}

TEST(epoch) {
    check_fields(0, 1970, 1, 1, 0, 0, 0);
}

TEST(one_second_before_epoch) {
    // Negative input: C division truncates toward zero, so the remainder has to
    // be normalized back into [0, 86400) with a borrow from the day count.
    check_fields(-1, 1969, 12, 31, 23, 59, 59);
}

TEST(known_timestamps) {
    check_fields(1000000000LL, 2001, 9,  9, 1, 46, 40);   // classic billennium
    check_fields(1234567890LL, 2009, 2, 13, 23, 31, 30);
    check_fields(2147483647LL, 2038, 1, 19,  3, 14,  7);  // 32-bit time_t rollover
}

TEST(leap_day_2024) {
    // 2024-02-29T12:00:00Z — divisible by 4, a leap year.
    check_fields(1709208000LL, 2024, 2, 29, 12, 0, 0);
}

TEST(century_non_leap_1900_and_leap_2000) {
    // 2000 IS a leap year (divisible by 400) — the case a naive %4 check gets
    // right and a naive %100 check gets wrong.
    check_fields(951825600LL, 2000, 2, 29, 12, 0, 0);
    // 1900 is NOT a leap year (divisible by 100, not 400) — if it were treated
    // as one, this instant would come back as Feb 29 instead of Mar 1. Pre-epoch,
    // so it also exercises the negative-seconds path.
    check_fields(-2203848000LL, 1900, 3, 1, 12, 0, 0);
}

TEST(year_and_month_boundaries) {
    check_fields(1735689599LL, 2024, 12, 31, 23, 59, 59);  // last second of 2024
    check_fields(1735689600LL, 2025,  1,  1,  0,  0,  0);  // first second of 2025
}

// Cross-check a wide span against gmtime_r, stepping by a non-round interval so
// the samples land on assorted times of day rather than repeatedly at midnight.
TEST(matches_gmtime_r_across_decades) {
    int mismatches = 0;
    for (int64_t s = -2208988800LL; s < 4102444800LL; s += 999983) {
        UtcTime got;
        UtcTime_fromUnix(s, &got);

        time_t tt = (time_t)s;
        struct tm want;
        gmtime_r(&tt, &want);

        if (got.year != want.tm_year + 1900 || got.mon != want.tm_mon + 1 ||
            got.day  != want.tm_mday        || got.hour != want.tm_hour ||
            got.min  != want.tm_min         || got.sec  != want.tm_sec) {
            if (mismatches < 3) {
                printf("      secs=%lld -> %04d-%02d-%02d %02d:%02d:%02d, gmtime_r says %04d-%02d-%02d %02d:%02d:%02d\n",
                       (long long)s, got.year, got.mon, got.day, got.hour, got.min, got.sec,
                       want.tm_year + 1900, want.tm_mon + 1, want.tm_mday,
                       want.tm_hour, want.tm_min, want.tm_sec);
            }
            mismatches++;
        }
    }
    CHECK(mismatches == 0);
}

// The bundle directory name must sort lexicographically in time order, because
// CrashHandler_findUnsentBundle() picks the newest bundle with strcmp.
static void format_name(int64_t secs, char* out, size_t n) {
    UtcTime t;
    UtcTime_fromUnix(secs, &t);
    snprintf(out, n, "%04d-%02d-%02d_%02d-%02d-%02d",
             t.year, t.mon, t.day, t.hour, t.min, t.sec);
}

TEST(bundle_name_is_iso_sortable) {
    char a[32], b[32];
    format_name(1735689599LL, a, sizeof(a));   // 2024-12-31T23:59:59Z
    format_name(1735689600LL, b, sizeof(b));   // 2025-01-01T00:00:00Z
    CHECK(strcmp(a, b) < 0);

    // Single-digit month/day/hour must be zero-padded, or "2025-1-5" would sort
    // after "2025-10-01" and the newest-bundle scan would pick the wrong one.
    format_name(1736035200LL, a, sizeof(a));   // 2025-01-05T00:00:00Z
    format_name(1759276800LL, b, sizeof(b));   // 2025-10-01T00:00:00Z
    CHECK(strcmp(a, b) < 0);
    CHECK(strlen(a) == strlen(b));
    CHECK(strlen(a) == 19);                    // yyyy-mm-dd_HH-MM-SS

    // Monotonic across a long ascending walk.
    char prev[32] = "";
    int out_of_order = 0;
    for (int64_t s = 0; s < 4102444800LL; s += 7776001) {   // ~90 days
        char cur[32];
        format_name(s, cur, sizeof(cur));
        if (prev[0] && strcmp(prev, cur) >= 0) out_of_order++;
        memcpy(prev, cur, sizeof(prev));
    }
    CHECK(out_of_order == 0);
}

int main(void) {
    printf("utc_time:\n");
    RUN(epoch);
    RUN(one_second_before_epoch);
    RUN(known_timestamps);
    RUN(leap_day_2024);
    RUN(century_non_leap_1900_and_leap_2000);
    RUN(year_and_month_boundaries);
    RUN(matches_gmtime_r_across_decades);
    RUN(bundle_name_is_iso_sortable);
    return test_summary();
}
