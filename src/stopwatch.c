#include "stopwatch.h"

void Stopwatch_reset(Stopwatch* stopwatch) {
    stopwatch->accumulated_ms = 0;
    stopwatch->since          = 0;
    stopwatch->running        = false;
}

void Stopwatch_start(Stopwatch* stopwatch, uint32_t now) {
    if (stopwatch->running) return;
    stopwatch->since   = now;
    stopwatch->running = true;
}

void Stopwatch_stop(Stopwatch* stopwatch, uint32_t now) {
    if (!stopwatch->running) return;
    stopwatch->accumulated_ms = Stopwatch_accumulated(stopwatch, now);
    stopwatch->running        = false;
}

uint64_t Stopwatch_accumulated(const Stopwatch* stopwatch, uint32_t now) {
    if (!stopwatch->running) return stopwatch->accumulated_ms;
    // Unsigned arithmetic gives the interval across the wrap of the clock
    return stopwatch->accumulated_ms + (uint32_t)(now - stopwatch->since);
}
