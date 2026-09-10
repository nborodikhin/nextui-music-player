#include "spectrum_ceiling.h"

// The whole height rises in this time.
#define CEILING_RISE_MS 150.0f

// A step longer than this comes from a screen that stopped drawing for a while.
// The value that the time gives is correct, thus the only limit is on the
// arithmetic.
#define CEILING_STEP_MAX_MS 60000u

void SpectrumCeiling_clear(SpectrumCeiling* ceiling) {
    ceiling->value = 0.0f;
    ceiling->last_ms = 0;
    ceiling->started = false;
}

void SpectrumCeiling_step(SpectrumCeiling* ceiling, bool rising, uint32_t now_ms) {
    if (!ceiling->started) {
        ceiling->started = true;
        ceiling->last_ms = now_ms;
        return;
    }

    // Unsigned arithmetic gives the elapsed time across the wrap of the clock.
    uint32_t dt = now_ms - ceiling->last_ms;
    ceiling->last_ms = now_ms;
    if (!rising || dt == 0) return;
    if (dt > CEILING_STEP_MAX_MS) dt = CEILING_STEP_MAX_MS;

    ceiling->value += (float)dt / CEILING_RISE_MS;
    if (ceiling->value > 1.0f) ceiling->value = 1.0f;
}

float SpectrumCeiling_value(const SpectrumCeiling* ceiling) {
    return ceiling->value;
}

bool SpectrumCeiling_isOpen(const SpectrumCeiling* ceiling) {
    return ceiling->value > 0.0f;
}
