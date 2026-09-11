// The ceiling of the spectrum. It limits each bar while the spectrum comes back,
// and it has no fall of its own: the bars carry the fall at their own rate.

#include "test.h"
#include "spectrum.h"

#define FRAME_MS 16

static bool SpectrumCeiling_isOpen(const SpectrumCeiling* ceiling) {
    return ceiling->value > 0.0f;
}

static void feed(SpectrumCeiling* c, bool rising, int ms, uint32_t* clock) {
    for (int t = 0; t < ms; t += FRAME_MS) {
        *clock += FRAME_MS;
        SpectrumCeiling_tick(c, rising, *clock);
    }
}

static float after_samples(int ms) {
    SpectrumCeiling c; SpectrumCeiling_clear(&c);
    uint32_t clock = 1000;
    SpectrumCeiling_tick(&c, true, clock);   // the first call gives the start
    feed(&c, true, ms, &clock);
    return SpectrumCeiling_value(&c);
}

TEST(starts_at_none) {
    SpectrumCeiling c; SpectrumCeiling_clear(&c);
    CHECK(SpectrumCeiling_value(&c) == 0.0f);
    CHECK(!SpectrumCeiling_isOpen(&c));
}

TEST(samples_raise_it_to_full) {
    CHECK(after_samples(48) > 0.0f);
    CHECK(after_samples(48) < 1.0f);

    // The whole height in about 0.15 seconds, and never past full
    CHECK(after_samples(200) == 1.0f);
    CHECK(after_samples(9000) == 1.0f);
}

// The bars carry the fall, thus the ceiling holds where nothing feeds it.
TEST(no_samples_hold_the_value) {
    SpectrumCeiling c; SpectrumCeiling_clear(&c);
    uint32_t clock = 1000;
    SpectrumCeiling_tick(&c, true, clock);
    feed(&c, true, 300, &clock);
    CHECK(SpectrumCeiling_value(&c) == 1.0f);

    feed(&c, false, 4000, &clock);
    CHECK(SpectrumCeiling_value(&c) == 1.0f);
    CHECK(SpectrumCeiling_isOpen(&c));
}

// A dialog over the screen stops the calls. The rise then carries the whole of
// that time, in place of one frame of it.
TEST(a_gap_in_the_calls_carries_its_whole_time) {
    SpectrumCeiling c; SpectrumCeiling_clear(&c);
    uint32_t clock = 5000;
    SpectrumCeiling_tick(&c, true, clock);
    clock += 4000;
    SpectrumCeiling_tick(&c, true, clock);
    CHECK(SpectrumCeiling_value(&c) == 1.0f);
}

TEST(the_clock_of_the_platform_can_wrap) {
    SpectrumCeiling c; SpectrumCeiling_clear(&c);
    uint32_t clock = 0xFFFFFF00u;
    SpectrumCeiling_tick(&c, true, clock);
    feed(&c, true, 400, &clock);
    CHECK(SpectrumCeiling_value(&c) == 1.0f);
}

TEST(clear_takes_it_to_none_at_once) {
    SpectrumCeiling c; SpectrumCeiling_clear(&c);
    uint32_t clock = 1000;
    SpectrumCeiling_tick(&c, true, clock);
    feed(&c, true, 300, &clock);
    SpectrumCeiling_clear(&c);
    CHECK(SpectrumCeiling_value(&c) == 0.0f);
    CHECK(!SpectrumCeiling_isOpen(&c));
}

int main(void) {
    RUN(starts_at_none);
    RUN(samples_raise_it_to_full);
    RUN(no_samples_hold_the_value);
    RUN(a_gap_in_the_calls_carries_its_whole_time);
    RUN(the_clock_of_the_platform_can_wrap);
    RUN(clear_takes_it_to_none_at_once);
    return test_summary();
}
