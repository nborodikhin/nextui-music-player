// Host tests of the activity clock: the time between the frames of a page, and the frames after
// a gap that add no time.

#include "test.h"
#include "ui/page/activity_clock.hpp"

TEST(the_first_frame_adds_no_time) {
    ActivityClock clock;
    clock.onFrameStart(5000);
    CHECK_EQ_INT(clock.getActiveTime(), 0);
    clock.onFrameStart(5016);
    clock.onFrameStart(5040);
    CHECK_EQ_INT(clock.getActiveTime(), 40);
}

TEST(a_gap_makes_the_next_frame_add_no_time) {
    ActivityClock clock;
    clock.onFrameStart(0);
    clock.onFrameStart(16);
    clock.onActivityGap();
    clock.onFrameStart(60000);
    CHECK_EQ_INT(clock.getActiveTime(), 16);
    clock.onFrameStart(60016);
    CHECK_EQ_INT(clock.getActiveTime(), 32);
}

TEST(two_gaps_before_one_frame_skip_one_frame) {
    ActivityClock clock;
    clock.onFrameStart(0);
    clock.onActivityGap();
    clock.onActivityGap();
    clock.onFrameStart(1000);
    clock.onFrameStart(1016);
    CHECK_EQ_INT(clock.getActiveTime(), 16);
}

TEST(the_time_crosses_the_wrap_of_the_frame_time) {
    ActivityClock clock;
    clock.onFrameStart(0xFFFFFFF0u);
    clock.onFrameStart(0x00000010u);
    CHECK_EQ_INT(clock.getActiveTime(), 32);
}

int main(void) {
    printf("activity_clock\n");
    RUN(the_first_frame_adds_no_time);
    RUN(a_gap_makes_the_next_frame_add_no_time);
    RUN(two_gaps_before_one_frame_skip_one_frame);
    RUN(the_time_crosses_the_wrap_of_the_frame_time);
    return test_summary();
}
