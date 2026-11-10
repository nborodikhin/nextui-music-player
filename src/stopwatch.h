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
} StopwatchData;

// Sets the count to 0, and stops the stopwatch.
void Stopwatch_reset(StopwatchData* stopwatch);

// Continues the count from `now`. A stopwatch that runs does not change.
void Stopwatch_start(StopwatchData* stopwatch, uint32_t now);

// Stops the count at `now`. A stopwatch that is stopped does not change.
void Stopwatch_stop(StopwatchData* stopwatch, uint32_t now);

// Returns the sum of the runs up to `now`, in milliseconds. A stopped time does
// not count.
uint64_t Stopwatch_accumulated(const StopwatchData* stopwatch, uint32_t now);

#ifdef __cplusplus
}

// A count of time that can stop and continue. A new stopwatch is stopped, with a count of 0.
class Stopwatch {
public:
    // Sets the count to 0, and stops the stopwatch.
    void reset();

    // Continues the count from `now`. A stopwatch that runs does not change.
    void start(uint32_t now);

    // Stops the count at `now`. A stopwatch that is stopped does not change.
    void stop(uint32_t now);

    bool isRunning() const { return running; }

    // Returns the sum of the runs up to `now`, in milliseconds. A stopped time does not count.
    uint64_t getElapsed(uint32_t now) const;

private:
    uint64_t accumulatedMs = 0;  // the count up to `since`
    uint32_t since         = 0;  // the time where the current run started
    bool running           = false;
};
#endif

#endif
