// Tests for src/input/input_driver.cpp and src/input/button_detector.cpp - the raw events of the pad
// and the button events that the detector makes from them.
//
// Pins the order of the raw events in one frame, the reset when the app cleared the
// pad, each mode of the detector (immediate, deferred, repeat), the combo and its
// block, and the time of each event.

#include <array>
#include <vector>

#include "test.h"
#include "input/button_detector.hpp"
#include "input/input_driver.hpp"

using namespace input;

namespace {

// A raw or a button event as one comparable record.
enum class Kind { Down, Up, Reset, Media, Short, Long, Repeat, Combo };

struct Record {
    Kind kind;
    Button button;
    Button second;
    uint32_t time;
};

class RawLog : public RawEventListener {
public:
    std::vector<Record> records;

    void onRawEvent(const RawEvent& event) override {
        std::visit(Overloaded{
                       [&](const event::ButtonDown& e) { add(Kind::Down, e.button, event.time); },
                       [&](const event::ButtonUp& e) { add(Kind::Up, e.button, event.time); },
                       [&](const event::ButtonReset&) { add(Kind::Reset, Button::A, event.time); },
                       [&](const event::Media&) { add(Kind::Media, Button::A, event.time); },
                   },
                   event.kind);
    }

private:
    void add(Kind kind, Button button, uint32_t time) {
        records.push_back({kind, button, button, time});
    }
};

class ButtonLog : public ButtonListener {
public:
    std::vector<Record> records;

    bool onButtonEvent(const ButtonEvent& event) override {
        std::visit(Overloaded{
                       [&](const event::ShortPress& e) { add(Kind::Short, e.button, e.button, event.time); },
                       [&](const event::LongPress& e) { add(Kind::Long, e.button, e.button, event.time); },
                       [&](const event::Repeat& e) { add(Kind::Repeat, e.button, e.button, event.time); },
                       [&](const event::Combo& e) { add(Kind::Combo, e.first, e.second, event.time); },
                   },
                   event.kind);
        return true;
    }

    void clear() { records.clear(); }

private:
    void add(Kind kind, Button button, Button second, uint32_t time) {
        records.push_back({kind, button, second, time});
    }
};

bool isRecord(const Record& record, Kind kind, Button button, uint32_t time) {
    return record.kind == kind && record.button == button && record.time == time;
}

constexpr uint32_t bit(Button button) {
    return getButtonBit(button);
}

// The requests of the detector tests: a long press on A, a repeat on DOWN, and the combo
// SELECT + A.
constexpr std::array<ButtonRequest, 3> kRequests = {
    request::LongPress{Button::A},
    request::Repeat{Button::DpadDown},
    request::Combo{Button::Select, Button::A},
};

// A detector with the requests above and a log of its events.
struct Fixture {
    ButtonLog log;
    ButtonDetector detector{log, kRequests};

    void down(Button button, uint32_t time) { detector.onRawEvent({time, event::ButtonDown{button}}); }
    void up(Button button, uint32_t time) { detector.onRawEvent({time, event::ButtonUp{button}}); }
    void reset(uint32_t time) { detector.onRawEvent({time, event::ButtonReset{}}); }

    // Ticks each 16 ms from the time from up to the time to
    void tick(uint32_t from, uint32_t to) {
        for (uint32_t now = from; now <= to; now += 16) detector.onTick(now);
    }
};

}  // namespace

namespace input {

// Reaches the private part of the driver
struct InputDriverTest {
    static void update(InputDriver& driver, const PadState& padState, uint32_t now,
                       RawEventListener& listener) {
        driver.update(padState, now, listener);
    }

    static void setHeld(InputDriver& driver, uint32_t mask) { driver.pressedPads = mask; }
};

}  // namespace input

// ---------------------------------------------------------------------------
// InputDriver

TEST(pad_gives_buttons_in_bit_order_and_down_before_up) {
    InputDriver pad;
    RawLog log;
    // B goes down and up in one frame. A goes down in the same frame.
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A) | bit(Button::B), bit(Button::B)}, 100, log);
    CHECK_EQ_SZ(log.records.size(), 3);
    CHECK(isRecord(log.records[0], Kind::Down, Button::A, 100));
    CHECK(isRecord(log.records[1], Kind::Down, Button::B, 100));
    CHECK(isRecord(log.records[2], Kind::Up, Button::B, 100));
}

TEST(pad_gives_nothing_while_a_button_is_held) {
    InputDriver pad;
    RawLog log;
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A), 0}, 0, log);
    InputDriverTest::update(pad, {bit(Button::A), 0, 0}, 16, log);
    InputDriverTest::update(pad, {0, 0, bit(Button::A)}, 32, log);
    CHECK_EQ_SZ(log.records.size(), 2);
    CHECK(isRecord(log.records[0], Kind::Down, Button::A, 0));
    CHECK(isRecord(log.records[1], Kind::Up, Button::A, 32));
}

TEST(pad_resets_when_a_button_leaves_with_no_release) {
    InputDriver pad;
    RawLog log;
    InputDriverTest::update(pad, {bit(Button::X) | bit(Button::Y), bit(Button::X) | bit(Button::Y), 0}, 0, log);
    InputDriverTest::update(pad, {0, 0, 0}, 16, log);  // the app cleared the pad
    InputDriverTest::update(pad, {0, 0, 0}, 32, log);
    CHECK_EQ_SZ(log.records.size(), 3);
    CHECK(log.records[2].kind == Kind::Reset && log.records[2].time == 16);
}

// The pressed mask of the state is the set of buttons that the pad holds
TEST(pad_state_gives_the_held_buttons) {
    InputDriver pad;
    RawLog log;
    const InputState& state = InputState::getInstance();
    InputDriverTest::update(pad, {bit(Button::A) | bit(Button::B), bit(Button::A) | bit(Button::B), 0}, 0, log);
    CHECK(state.isPressed(Button::A) && state.isPressed(Button::B));
    CHECK(state.isJustPressed(Button::A));
    InputDriverTest::update(pad, {bit(Button::B), 0, bit(Button::A)}, 16, log);
    CHECK(!state.isPressed(Button::A) && state.isPressed(Button::B));
    CHECK(state.isJustReleased(Button::A) && !state.isJustPressed(Button::A));
    CHECK(state.getTime() == 16);
    InputDriverTest::update(pad, {0, 0, 0}, 32, log);  // the app cleared the pad
    CHECK(!state.isPressed(Button::B));
}

// A button that was held before the first update gives no
// down. Its up goes out.
TEST(pad_gives_only_the_up_of_a_held_button) {
    InputDriver pad;
    InputDriverTest::setHeld(pad, bit(Button::A));  // held before the first update
    RawLog log;
    InputDriverTest::update(pad, {bit(Button::A), 0, 0}, 0, log);
    CHECK_EQ_SZ(log.records.size(), 0);
    InputDriverTest::update(pad, {0, 0, bit(Button::A)}, 16, log);
    CHECK_EQ_SZ(log.records.size(), 1);
    CHECK(isRecord(log.records[0], Kind::Up, Button::A, 16));
}

// A button that was held before the first update gives no event, but the state shows it
TEST(pad_state_gives_a_button_held_before_the_first_update) {
    InputDriver pad;
    RawLog log;
    const InputState& state = InputState::getInstance();
    InputDriverTest::update(pad, {bit(Button::A), 0, 0}, 0, log);
    CHECK(state.isPressed(Button::A));
    InputDriverTest::update(pad, {0, 0, bit(Button::A)}, 16, log);
    CHECK(!state.isPressed(Button::A) && !state.isJustReleased(Button::A));
    CHECK_EQ_SZ(log.records.size(), 0);
}

// The dpad releases each direction that it does not hold: LEFT goes down, and UP, DOWN and
// RIGHT, which were not down, give no up
TEST(pad_gives_no_up_of_a_button_that_was_not_down) {
    InputDriver pad;
    RawLog log;
    const uint32_t others = bit(Button::DpadUp) | bit(Button::DpadDown) | bit(Button::DpadRight);
    InputDriverTest::update(pad, {bit(Button::DpadLeft), bit(Button::DpadLeft), others}, 0, log);
    InputDriverTest::update(pad, {0, 0, others | bit(Button::DpadLeft)}, 16, log);
    CHECK_EQ_SZ(log.records.size(), 2);
    CHECK(isRecord(log.records[0], Kind::Down, Button::DpadLeft, 0));
    CHECK(isRecord(log.records[1], Kind::Up, Button::DpadLeft, 16));
}

// ---------------------------------------------------------------------------
// ButtonDetector

TEST(immediate_short_press_at_the_down) {
    Fixture f;
    f.down(Button::B, 10);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::B, 10));
    f.tick(10, 2000);
    f.up(Button::B, 2000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
}

TEST(deferred_short_press_at_the_up) {
    Fixture f;
    f.down(Button::A, 10);
    f.tick(10, 400);
    CHECK_EQ_SZ(f.log.records.size(), 0);
    f.up(Button::A, 420);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::A, 420));
}

TEST(long_press_one_time_and_no_short_press_after_it) {
    Fixture f;
    f.down(Button::A, 0);
    f.detector.onTick(ButtonDetector::kLongPressMs - 1);
    CHECK_EQ_SZ(f.log.records.size(), 0);
    f.detector.onTick(ButtonDetector::kLongPressMs);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Long, Button::A, ButtonDetector::kLongPressMs));
    f.tick(ButtonDetector::kLongPressMs, 3000);
    f.up(Button::A, 3000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
}

TEST(repeat_after_the_delay_then_at_each_interval) {
    Fixture f;
    f.down(Button::DpadDown, 0);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::DpadDown, 0));
    f.detector.onTick(ButtonDetector::kRepeatDelayMs - 1);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    f.detector.onTick(ButtonDetector::kRepeatDelayMs);
    f.detector.onTick(ButtonDetector::kRepeatDelayMs + ButtonDetector::kRepeatIntervalMs - 1);
    f.detector.onTick(ButtonDetector::kRepeatDelayMs + ButtonDetector::kRepeatIntervalMs);
    CHECK_EQ_SZ(f.log.records.size(), 3);
    CHECK(isRecord(f.log.records[1], Kind::Repeat, Button::DpadDown, ButtonDetector::kRepeatDelayMs));
    CHECK(isRecord(f.log.records[2], Kind::Repeat, Button::DpadDown,
                   ButtonDetector::kRepeatDelayMs + ButtonDetector::kRepeatIntervalMs));
    f.up(Button::DpadDown, 450);
    f.tick(450, 1000);
    CHECK_EQ_SZ(f.log.records.size(), 3);
}

// A hold of 450 ms with ticks of 16 ms gives the repeats at 300 and 400 ms
TEST(repeat_count_of_a_hold) {
    Fixture f;
    f.down(Button::DpadDown, 0);
    f.tick(0, 449);
    f.up(Button::DpadDown, 450);
    CHECK_EQ_SZ(f.log.records.size(), 3);
}

// A late tick gives one repeat, and each next tick one more until the schedule of the pad
// is reached again
TEST(late_tick_catches_up_one_repeat_for_each_tick) {
    Fixture f;
    f.down(Button::DpadDown, 0);
    f.detector.onTick(1000);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    for (uint32_t now = 1016; now <= 1160; now += 16) f.detector.onTick(now);
    // The schedule is 300, 400, ..., 1100: nine repeats in all, one for each tick
    CHECK_EQ_SZ(f.log.records.size(), 1 + 9);
    f.detector.onTick(1176);
    f.detector.onTick(1199);
    CHECK_EQ_SZ(f.log.records.size(), 1 + 9);
    f.detector.onTick(1200);
    CHECK_EQ_SZ(f.log.records.size(), 1 + 10);
}

// The combo gives its members in the order of the request
static bool isSelectACombo(const Record& record, uint32_t time) {
    return record.kind == Kind::Combo && record.button == Button::Select &&
           record.second == Button::A && record.time == time;
}

TEST(combo_in_the_order_of_the_request) {
    Fixture f;
    f.down(Button::Select, 0);
    f.down(Button::A, 40);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isSelectACombo(f.log.records[0], 40));
}

// A then SELECT is not the combo: each member gives its own event
TEST(no_combo_in_the_other_order) {
    Fixture f;
    f.down(Button::A, 0);
    f.down(Button::Select, 40);
    f.up(Button::Select, 100);
    f.up(Button::A, 200);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::Select, 100));
    CHECK(isRecord(f.log.records[1], Kind::Short, Button::A, 200));
}

// Two downs at one time start the combo in each order
TEST(combo_at_one_time_in_each_order) {
    const Button orders[2][2] = {
        {Button::Select, Button::A},
        {Button::A, Button::Select},
    };
    for (const auto& order : orders) {
        Fixture f;
        f.down(order[0], 40);
        f.down(order[1], 40);
        CHECK_EQ_SZ(f.log.records.size(), 1);
        CHECK(isSelectACombo(f.log.records[0], 40));
    }
}

TEST(combo_in_one_frame) {
    InputDriver pad;
    Fixture f;
    const uint32_t both = bit(Button::Select) | bit(Button::A);
    InputDriverTest::update(pad, {both, both, 0}, 0, f.detector);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isSelectACombo(f.log.records[0], 0));
}

TEST(combo_members_give_no_event_until_both_are_up) {
    Fixture f;
    f.down(Button::Select, 0);
    f.down(Button::A, 10);
    f.tick(10, 2000);
    f.up(Button::A, 2000);
    f.down(Button::A, 2100);
    f.tick(2100, 3000);
    f.up(Button::A, 3000);
    f.up(Button::Select, 3100);
    CHECK_EQ_SZ(f.log.records.size(), 1);

    f.down(Button::A, 4000);
    f.up(Button::A, 4100);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[1], Kind::Short, Button::A, 4100));
}

// A combo member that is held alone gives its own short press at the up
TEST(combo_member_alone_gives_a_short_press_at_the_up) {
    Fixture f;
    f.down(Button::Select, 0);
    CHECK_EQ_SZ(f.log.records.size(), 0);
    f.up(Button::Select, 100);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::Select, 100));
}

// The release comes before the tick of its frame: a long press that is due at the release
// goes out, and no short press follows
TEST(release_after_the_long_press_deadline_gives_the_long_press) {
    Fixture f;
    f.down(Button::A, 0);
    f.tick(0, 496);  // the last tick is before the deadline
    f.up(Button::A, 512);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Long, Button::A, 512));
}

TEST(release_before_the_long_press_deadline_gives_a_short_press) {
    Fixture f;
    f.down(Button::A, 0);
    f.up(Button::A, ButtonDetector::kLongPressMs - 1);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::A, ButtonDetector::kLongPressMs - 1));
}

// A repeat that is due at the release goes out before the release, as the pad gives it
TEST(release_at_the_repeat_deadline_gives_the_repeat) {
    Fixture f;
    f.down(Button::DpadDown, 0);
    f.tick(0, 288);
    f.up(Button::DpadDown, ButtonDetector::kRepeatDelayMs);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[1], Kind::Repeat, Button::DpadDown, ButtonDetector::kRepeatDelayMs));
}

// With two combos on one member, the first request that matches wins
TEST(first_matching_combo_wins) {
    constexpr std::array<ButtonRequest, 2> requests = {
        request::Combo{Button::Select, Button::A},
        request::Combo{Button::L1, Button::A},
    };
    ButtonLog log;
    ButtonDetector detector(log, requests);
    detector.onRawEvent({0, event::ButtonDown{Button::Select}});
    detector.onRawEvent({10, event::ButtonDown{Button::L1}});
    detector.onRawEvent({20, event::ButtonDown{Button::A}});
    CHECK_EQ_SZ(log.records.size(), 1);
    CHECK(isSelectACombo(log.records[0], 20));
}

// After a long press of A, SELECT does not start the combo
TEST(no_combo_after_a_long_press) {
    Fixture f;
    f.down(Button::A, 0);
    f.detector.onTick(ButtonDetector::kLongPressMs);
    f.down(Button::Select, 600);
    f.up(Button::Select, 700);
    f.up(Button::A, 800);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[0], Kind::Long, Button::A, ButtonDetector::kLongPressMs));
    CHECK(isRecord(f.log.records[1], Kind::Short, Button::Select, 700));
}

// A combo blocks only its own members
TEST(each_combo_blocks_only_its_members) {
    constexpr std::array<ButtonRequest, 2> requests = {
        request::Combo{Button::Select, Button::A},
        request::Combo{Button::L1, Button::R1},
    };
    ButtonLog log;
    ButtonDetector detector(log, requests);
    detector.onRawEvent({0, event::ButtonDown{Button::Select}});
    detector.onRawEvent({10, event::ButtonDown{Button::A}});      // combo, SELECT stays held
    detector.onRawEvent({20, event::ButtonDown{Button::L1}});
    detector.onRawEvent({30, event::ButtonDown{Button::R1}});     // combo
    detector.onRawEvent({40, event::ButtonUp{Button::L1}});
    detector.onRawEvent({50, event::ButtonUp{Button::R1}});
    detector.onRawEvent({60, event::ButtonDown{Button::L1}});
    detector.onRawEvent({70, event::ButtonUp{Button::L1}});
    CHECK_EQ_SZ(log.records.size(), 3);
    CHECK(log.records[2].kind == Kind::Short && log.records[2].button == Button::L1);
}

TEST(up_with_no_down_gives_nothing) {
    Fixture f;
    f.up(Button::A, 10);
    f.up(Button::B, 20);
    CHECK_EQ_SZ(f.log.records.size(), 0);
}

// A reset ends each held button with no event: no short press, long press or repeat
TEST(reset_ends_each_held_button_with_no_event) {
    Fixture f;
    f.down(Button::A, 0);
    f.down(Button::DpadDown, 0);
    f.reset(100);
    f.tick(100, 2000);
    f.up(Button::A, 2000);
    f.up(Button::DpadDown, 2000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::DpadDown, 0));
}

// A reset clears the latch of a combo, thus its members give events again
TEST(reset_clears_a_combo) {
    Fixture f;
    f.down(Button::Select, 0);
    f.down(Button::A, 10);
    f.reset(100);
    f.down(Button::A, 200);
    f.up(Button::A, 300);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[1], Kind::Short, Button::A, 300));
}

TEST(media_gives_nothing) {
    Fixture f;
    f.detector.onRawEvent({0, event::Media{MediaKey::PlayPause}});
    CHECK_EQ_SZ(f.log.records.size(), 0);
}

// The pad and the detector in a chain, as the app runs them
TEST(pad_and_detector_together) {
    InputDriver pad;
    Fixture f;
    InputDriverTest::update(pad, {bit(Button::B), bit(Button::B), 0}, 0, f.detector);
    InputDriverTest::update(pad, {0, 0, bit(Button::B)}, 16, f.detector);
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A), 0}, 32, f.detector);
    InputDriverTest::update(pad, {0, 0, 0}, 48, f.detector);  // the app cleared the pad
    f.tick(48, 1000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::B, 0));
}

int main(void) {
    printf("input events\n");
    RUN(pad_gives_buttons_in_bit_order_and_down_before_up);
    RUN(pad_gives_nothing_while_a_button_is_held);
    RUN(pad_resets_when_a_button_leaves_with_no_release);
    RUN(pad_state_gives_the_held_buttons);
    RUN(pad_gives_only_the_up_of_a_held_button);
    RUN(pad_state_gives_a_button_held_before_the_first_update);
    RUN(pad_gives_no_up_of_a_button_that_was_not_down);
    RUN(immediate_short_press_at_the_down);
    RUN(deferred_short_press_at_the_up);
    RUN(long_press_one_time_and_no_short_press_after_it);
    RUN(repeat_after_the_delay_then_at_each_interval);
    RUN(repeat_count_of_a_hold);
    RUN(late_tick_catches_up_one_repeat_for_each_tick);
    RUN(combo_in_the_order_of_the_request);
    RUN(no_combo_in_the_other_order);
    RUN(combo_at_one_time_in_each_order);
    RUN(combo_in_one_frame);
    RUN(combo_members_give_no_event_until_both_are_up);
    RUN(combo_member_alone_gives_a_short_press_at_the_up);
    RUN(release_after_the_long_press_deadline_gives_the_long_press);
    RUN(release_before_the_long_press_deadline_gives_a_short_press);
    RUN(release_at_the_repeat_deadline_gives_the_repeat);
    RUN(first_matching_combo_wins);
    RUN(no_combo_after_a_long_press);
    RUN(each_combo_blocks_only_its_members);
    RUN(up_with_no_down_gives_nothing);
    RUN(reset_ends_each_held_button_with_no_event);
    RUN(reset_clears_a_combo);
    RUN(media_gives_nothing);
    RUN(pad_and_detector_together);
    return test_summary();
}
