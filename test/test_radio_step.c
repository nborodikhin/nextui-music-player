// Tests for src/radio_step.c: the station that the radio plays after a step.

#include <stdio.h>
#include <string.h>

#include "radio.h"
#include "test.h"

static RadioStation stations[3];

static void fill(int count) {
    memset(stations, 0, sizeof(stations));
    for (int i = 0; i < count; i++) snprintf(stations[i].url, sizeof(stations[i].url), "http://s%d", i);
}

TEST(no_station_gives_no_step) {
    fill(0);
    CHECK_EQ_INT(Radio_pickStation(stations, 0, "http://s0", true), -1);
    CHECK_EQ_INT(Radio_pickStation(stations, 0, NULL, false), -1);
    CHECK_EQ_INT(Radio_pickStation(NULL, 3, "http://s0", true), -1);
}

TEST(one_station_that_plays_gives_no_step) {
    fill(1);
    CHECK_EQ_INT(Radio_pickStation(stations, 1, "http://s0", true), -1);
    CHECK_EQ_INT(Radio_pickStation(stations, 1, "http://s0", false), -1);
}

TEST(one_station_that_does_not_play_is_the_step) {
    fill(1);
    CHECK_EQ_INT(Radio_pickStation(stations, 1, "http://other", true), 0);
    CHECK_EQ_INT(Radio_pickStation(stations, 1, "http://other", false), 0);
}

TEST(step_moves_by_one) {
    fill(3);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, "http://s1", true), 2);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, "http://s1", false), 0);
}

TEST(step_wraps_at_each_end) {
    fill(3);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, "http://s2", true), 0);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, "http://s0", false), 2);
}

TEST(url_that_is_not_in_the_list_steps_to_an_end) {
    fill(3);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, "http://removed", true), 0);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, "http://removed", false), 2);
    CHECK_EQ_INT(Radio_pickStation(stations, 3, NULL, true), 0);
}

int main(void) {
    printf("test_radio_step\n");
    RUN(no_station_gives_no_step);
    RUN(one_station_that_plays_gives_no_step);
    RUN(one_station_that_does_not_play_is_the_step);
    RUN(step_moves_by_one);
    RUN(step_wraps_at_each_end);
    RUN(url_that_is_not_in_the_list_steps_to_an_end);
    return test_summary();
}
