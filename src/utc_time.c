#include "utc_time.h"

// Howard Hinnant's civil_from_days — an exact integer inverse of days_from_civil.
// Shifts the epoch to 0000-03-01 so leap days land at the end of the 400-year
// era, which removes every special case from the month/day arithmetic.
void UtcTime_fromUnix(int64_t secs, UtcTime* out) {
    int64_t days = secs / 86400;
    int64_t rem  = secs % 86400;
    // C truncates toward zero, so negative inputs need the remainder normalized
    // back into [0, 86400) with a matching borrow from the day count.
    if (rem < 0) { rem += 86400; days -= 1; }

    out->hour = (int)(rem / 3600);
    out->min  = (int)((rem % 3600) / 60);
    out->sec  = (int)(rem % 60);

    days += 719468;  // 1970-01-01 -> 0000-03-01
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;                              // [0, 146096]
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
    int64_t mp  = (5*doy + 2) / 153;                                // [0, 11]
    int64_t d   = doy - (153*mp + 2)/5 + 1;                         // [1, 31]
    int64_t m   = mp < 10 ? mp + 3 : mp - 9;                        // [1, 12]

    out->year = (int)(yoe + era * 400 + (m <= 2));
    out->mon  = (int)m;
    out->day  = (int)d;
}
