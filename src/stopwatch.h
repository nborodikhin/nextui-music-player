#ifndef __STOPWATCH_H__
#define __STOPWATCH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t accumulated_ms;  // the count up to `since`
    uint32_t since;           // the time where the current run started
    bool     running;
} Stopwatch;

// Sets the count to 0, and stops the stopwatch.
void Stopwatch_reset(Stopwatch* stopwatch);

// Continues the count from `now`. A stopwatch that runs does not change.
void Stopwatch_start(Stopwatch* stopwatch, uint32_t now);

// Stops the count at `now`. A stopwatch that is stopped does not change.
void Stopwatch_stop(Stopwatch* stopwatch, uint32_t now);

// Returns the sum of the runs up to `now`, in milliseconds. A stopped time does
// not count.
uint64_t Stopwatch_accumulated(const Stopwatch* stopwatch, uint32_t now);

#ifdef __cplusplus
}
#endif

#endif
