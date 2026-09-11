#include "spectrum.h"

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

void SpectrumCeiling_tick(SpectrumCeiling* ceiling, bool sound_is_playing, uint32_t now_ms) {
    uint32_t last_ms = ceiling->started ? ceiling->last_ms : now_ms;

    ceiling->started = true;
    ceiling->last_ms = now_ms;

    // Unsigned arithmetic gives the elapsed time across the wrap of the clock.
    uint32_t dt = now_ms - last_ms;
    if (dt > CEILING_STEP_MAX_MS) dt = CEILING_STEP_MAX_MS;

    // Ticks during buffering should not raise the ceiling.
    if (sound_is_playing) {
        ceiling->value += (float)dt / CEILING_RISE_MS;
        if (ceiling->value > 1.0f) ceiling->value = 1.0f;
    }
}

float SpectrumCeiling_value(const SpectrumCeiling* ceiling) {
    return ceiling->value;
}
