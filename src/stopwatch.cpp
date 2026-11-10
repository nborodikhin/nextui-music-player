#include "stopwatch.h"

void Stopwatch_reset(StopwatchData* stopwatch) {
    stopwatch->accumulated_ms = 0;
    stopwatch->since          = 0;
    stopwatch->running        = false;
}

void Stopwatch_start(StopwatchData* stopwatch, uint32_t now) {
    if (stopwatch->running) return;
    stopwatch->since   = now;
    stopwatch->running = true;
}

void Stopwatch_stop(StopwatchData* stopwatch, uint32_t now) {
    if (!stopwatch->running) return;
    stopwatch->accumulated_ms = Stopwatch_accumulated(stopwatch, now);
    stopwatch->running        = false;
}

uint64_t Stopwatch_accumulated(const StopwatchData* stopwatch, uint32_t now) {
    if (!stopwatch->running) return stopwatch->accumulated_ms;
    // Unsigned arithmetic gives the interval across the wrap of the clock
    return stopwatch->accumulated_ms + static_cast<uint32_t>(now - stopwatch->since);
}

void Stopwatch::reset() {
    accumulatedMs = 0;
    since         = 0;
    running       = false;
}

void Stopwatch::start(uint32_t now) {
    if (running) return;
    since   = now;
    running = true;
}

void Stopwatch::stop(uint32_t now) {
    if (!running) return;
    accumulatedMs = getElapsed(now);
    running       = false;
}

uint64_t Stopwatch::getElapsed(uint32_t now) const {
    if (!running) return accumulatedMs;
    // Unsigned arithmetic gives the interval across the wrap of the clock
    return accumulatedMs + static_cast<uint32_t>(now - since);
}
