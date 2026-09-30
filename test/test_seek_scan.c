// Tests for src/seek_scan.c - the progressive seek on LEFT and RIGHT.
//
// Pins the short press (one seek at once, none on release), the repeat delay and
// the RockBox step (1 s, growth of 1/32, a limit of 3%), the single seek on
// release, and the ends of the track.

#include "test.h"
#include "seek_scan.h"

#define MIN_MS   (60 * 1000)
#define HOUR_MS  (60 * MIN_MS)

// One frame with the buttons as given. Returns the seek, or -1 for none.
static int frame(SeekScan* s, uint32_t now, bool back, bool fwd, int pos, int dur) {
    int seek_to = -1;
    if (!SeekScan_update(s, now, back, fwd, pos, dur, 10000, 30000, &seek_to)) return -1;
    return seek_to;
}

TEST(no_button_does_nothing) {
    SeekScan s = {0};
    CHECK_EQ_INT(frame(&s, 0, false, false, 5000, HOUR_MS), -1);
    CHECK(!SeekScan_isScanning(&s));
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 5000), 5000);
}

TEST(short_press_seeks_once_by_the_screen_step) {
    SeekScan s = {0};
    CHECK_EQ_INT(frame(&s, 1000, false, true, MIN_MS, HOUR_MS), MIN_MS + 30000);
    CHECK_EQ_INT(frame(&s, 1016, false, true, MIN_MS, HOUR_MS), -1);
    CHECK_EQ_INT(frame(&s, 1100, false, false, MIN_MS, HOUR_MS), -1);  // release: no seek
    CHECK_EQ_INT(frame(&s, 2000, true, false, MIN_MS, HOUR_MS), MIN_MS - 10000);
}

TEST(short_press_stops_at_the_ends) {
    SeekScan s = {0};
    CHECK_EQ_INT(frame(&s, 0, true, false, 4000, HOUR_MS), 0);
    frame(&s, 16, false, false, 0, HOUR_MS);
    CHECK_EQ_INT(frame(&s, 32, false, true, HOUR_MS - 1000, HOUR_MS), HOUR_MS);
}

// The delay counts from the frame after the press, thus a slow seek of the press
// does not start the scan at once
TEST(scan_starts_after_the_repeat_delay) {
    SeekScan s = {0};
    frame(&s, 0, false, true, 0, HOUR_MS);           // press, seek to 30 s
    frame(&s, 5000, false, true, 30000, HOUR_MS);    // the seek took 5 s: arm
    frame(&s, 5000 + SEEK_SCAN_REPEAT_DELAY_MS - 1, false, true, 30000, HOUR_MS);
    CHECK(!SeekScan_isScanning(&s));
    frame(&s, 5000 + SEEK_SCAN_REPEAT_DELAY_MS, false, true, 30000, HOUR_MS);
    CHECK(SeekScan_isScanning(&s));
    // The first repeat only starts the scan
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 30000), 30000);
}

// Hold forward past the delay, then return the time of the first moving repeat
static uint32_t start_scan(SeekScan* s, int pos, int dur, bool back) {
    frame(s, 0, back, !back, pos, dur);
    frame(s, 16, back, !back, pos, dur);
    frame(s, 16 + SEEK_SCAN_REPEAT_DELAY_MS, back, !back, pos, dur);
    return 16 + SEEK_SCAN_REPEAT_DELAY_MS + SEEK_SCAN_REPEAT_START_MS;
}

TEST(each_repeat_moves_the_target_and_grows_the_step) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 0, HOUR_MS, false);  // the press moved to 30 s
    frame(&s, t, false, true, 30000, HOUR_MS);
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 0), 30000 + 1000);
    CHECK_EQ_INT(s.step_ms, 1000 + (1000 >> SEEK_SCAN_ACCEL_SHIFT));
    // The next repeat comes 10 ms sooner
    t += SEEK_SCAN_REPEAT_START_MS - SEEK_SCAN_REPEAT_DECREMENT_MS;
    frame(&s, t - 1, false, true, 30000, HOUR_MS);
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 0), 31000);
    frame(&s, t, false, true, 30000, HOUR_MS);
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 0), 31000 + 1031);
}

TEST(repeat_interval_stops_at_the_finish_value) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 0, HOUR_MS, false);
    for (int i = 0; i < 40; i++) {
        frame(&s, t, false, true, 30000, HOUR_MS);
        t = s.next_repeat_at;
    }
    CHECK_EQ_INT(s.interval_ms, SEEK_SCAN_REPEAT_FINISH_MS);
}

// A late frame does one repeat, not each repeat that it missed
TEST(late_frame_does_one_repeat) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 0, HOUR_MS, false);
    frame(&s, t + 5000, false, true, 30000, HOUR_MS);
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 0), 31000);
    CHECK_EQ_INT((int)(s.next_repeat_at - (t + 5000)), SEEK_SCAN_REPEAT_START_MS - SEEK_SCAN_REPEAT_DECREMENT_MS);
}

TEST(release_after_a_scan_seeks_to_the_target) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 0, HOUR_MS, false);
    frame(&s, t, false, true, 30000, HOUR_MS);
    CHECK_EQ_INT(frame(&s, t + 10, false, false, 30500, HOUR_MS), 31000);
    CHECK(!SeekScan_isScanning(&s));
    CHECK_EQ_INT(frame(&s, t + 20, false, false, 31000, HOUR_MS), -1);
}

// The step is at most 3% of the time that is left, and at least 500 ms
TEST(step_is_limited_by_the_time_left) {
    SeekScan s = {0};
    s.direction = 1; s.armed = true; s.scanning = true;
    s.target_ms = HOUR_MS - 10000;  // 10 s left: 3% is 300 ms, the floor gives 500
    s.step_ms = 20000;
    s.interval_ms = SEEK_SCAN_REPEAT_FINISH_MS;
    s.next_repeat_at = 100;
    frame(&s, 100, false, true, 0, HOUR_MS);
    CHECK_EQ_INT(s.target_ms, HOUR_MS - 9500);

    s.target_ms = HOUR_MS / 2;      // 30 min left: 3% is 54 s
    s.step_ms = 200000;
    s.next_repeat_at = 200;
    frame(&s, 200, false, true, 0, HOUR_MS);
    CHECK_EQ_INT(s.target_ms, HOUR_MS / 2 + 54000);
}

TEST(backward_scan_stops_at_the_start) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 20000, HOUR_MS, true);  // the press moved to 10 s
    for (int i = 0; i < 100; i++) {
        frame(&s, t, true, false, 10000, HOUR_MS);
        t = s.next_repeat_at;
    }
    CHECK_EQ_INT(SeekScan_displayPosition(&s, 10000), 0);
    CHECK_EQ_INT(frame(&s, t, false, false, 10000, HOUR_MS), 0);
}

// Both buttons down, or a change of direction, ends the press as a release does
TEST(second_button_ends_the_scan) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 0, HOUR_MS, false);
    frame(&s, t, false, true, 30000, HOUR_MS);
    CHECK_EQ_INT(frame(&s, t + 10, true, true, 30000, HOUR_MS), 31000);
    CHECK(!SeekScan_isScanning(&s));
}

TEST(unknown_duration_gives_no_scan) {
    SeekScan s = {0};
    frame(&s, 0, false, true, 0, 0);
    frame(&s, 16, false, true, 30000, 0);
    frame(&s, 2000, false, true, 30000, 0);
    CHECK(!SeekScan_isScanning(&s));
}

TEST(cancel_drops_the_scan_without_a_seek) {
    SeekScan s = {0};
    uint32_t t = start_scan(&s, 0, HOUR_MS, false);
    frame(&s, t, false, true, 30000, HOUR_MS);
    SeekScan_cancel(&s);
    CHECK(!SeekScan_isScanning(&s));
    CHECK_EQ_INT(frame(&s, t + 10, false, false, 30000, HOUR_MS), -1);
}

int main(void) {
    RUN(no_button_does_nothing);
    RUN(short_press_seeks_once_by_the_screen_step);
    RUN(short_press_stops_at_the_ends);
    RUN(scan_starts_after_the_repeat_delay);
    RUN(each_repeat_moves_the_target_and_grows_the_step);
    RUN(repeat_interval_stops_at_the_finish_value);
    RUN(late_frame_does_one_repeat);
    RUN(release_after_a_scan_seeks_to_the_target);
    RUN(step_is_limited_by_the_time_left);
    RUN(backward_scan_stops_at_the_start);
    RUN(second_button_ends_the_scan);
    RUN(unknown_duration_gives_no_scan);
    RUN(cancel_drops_the_scan_without_a_seek);
    return test_summary();
}
