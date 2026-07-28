#ifndef __UTC_TIME_H__
#define __UTC_TIME_H__

#include <stdint.h>

// Async-signal-safe civil-time conversion.
//
// Exists because neither localtime_r nor gmtime_r may be called from a signal
// handler: both take tzset_lock, and localtime_r additionally getenv/malloc/
// reads /etc/localtime on first call. glibc's malloc corruption detector calls
// abort() while holding the arena lock, so a SIGABRT handler that allocates
// deadlocks against it — exactly the case where a crash bundle matters most.
// See spec/crash-reporting.md > Signal Handler.

typedef struct {
    int year;   // e.g. 2026
    int mon;    // 1-12
    int day;    // 1-31
    int hour;   // 0-23
    int min;    // 0-59
    int sec;    // 0-60 (leap seconds are not represented by time_t, so 0-59)
} UtcTime;

// Convert seconds since the Unix epoch to broken-down UTC.
// Pure integer arithmetic: no locale, no locks, no allocation, no errno.
// Valid across the whole int64 range, including negative (pre-1970) inputs.
void UtcTime_fromUnix(int64_t secs, UtcTime* out);

#endif
