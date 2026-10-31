// Tests for src/input/input_driver.cpp, src/input/button_detector.cpp and
// src/input/common_button_handler.cpp - the raw events of the pad, the button events that the
// detector makes from them, and the global buttons.
//
// Pins the order of the raw events in one frame, the reset when the app cleared the
// pad, each mode of the detector (immediate, deferred, repeat), the combo and its
// block, the time of each event, and the dialogs and the earphone buttons of the handler.

#include <array>
#include <string>
#include <vector>

#include "test.h"
#include "input/button_detector.hpp"
#include "input/common_button_handler.hpp"
#include "input/input_driver.hpp"
#include "input/input_manager.hpp"

using namespace input;

namespace {

// A raw or a button event as one comparable record.
enum class Kind { Pressed, Released, Reset, Cancel, Media, Short, Long, Repeat, Combo };

struct Record {
    Kind kind;
    Button button;
    Button second;
    uint32_t time;
};

class RawLog : public RawEventListener {
public:
    std::vector<Record> records;

    bool onRawEvent(const RawEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
            add(Kind::Pressed, typedEvent->button, event.time);
        }
        if (const auto* typedEvent = std::get_if<event::ButtonCancel>(&event.kind)) {
            add(Kind::Cancel, typedEvent->button, event.time);
        }
        if (const auto* typedEvent = std::get_if<event::ButtonReleased>(&event.kind)) {
            add(Kind::Released, typedEvent->button, event.time);
        }
        if (std::get_if<event::ButtonReset>(&event.kind)) {
            add(Kind::Reset, Button::A, event.time);
        }
        if (std::get_if<event::Media>(&event.kind)) {
            add(Kind::Media, Button::A, event.time);
        }
        return false;
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
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            add(Kind::Short, typedEvent->button, typedEvent->button, event.time);
        }
        if (const auto* typedEvent = std::get_if<event::LongPress>(&event.kind)) {
            add(Kind::Long, typedEvent->button, typedEvent->button, event.time);
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            add(Kind::Repeat, typedEvent->button, typedEvent->button, event.time);
        }
        if (const auto* typedEvent = std::get_if<event::Combo>(&event.kind)) {
            add(Kind::Combo, typedEvent->first, typedEvent->second, event.time);
        }
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

    void press(Button button, uint32_t time) {
        detector.onRawEvent({time, event::ButtonPressed{button}});
    }
    void release(Button button, uint32_t time) {
        detector.onRawEvent({time, event::ButtonReleased{button}});
    }
    void reset(uint32_t time) { detector.onRawEvent({time, event::ButtonReset{}}); }

    // Ticks each 16 ms from the time from to the time to
    void tick(uint32_t from, uint32_t to) {
        for (uint32_t now = from; now <= to; now += 16) detector.onTick(now);
    }
};

}  // namespace

namespace input {

// Reaches the private part of the driver
struct InputDriverTest {
    static void update(InputDriver& driver, const PadState& padState, uint32_t now,
                       RawEventListener& listener, bool padReset = false) {
        driver.update(padState, padReset, now, listener);
    }

    static void setHeld(InputDriver& driver, uint32_t mask) { driver.pressedPads = mask; }
};

}  // namespace input

// ---------------------------------------------------------------------------
// InputDriver

TEST(pad_gives_buttons_in_bit_order_and_presses_before_releases) {
    InputDriver pad;
    RawLog log;
    // B is pressed and released in one frame. A is pressed in the same frame.
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A) | bit(Button::B), bit(Button::B)}, 100, log);
    CHECK_EQ_SZ(log.records.size(), 3);
    CHECK(isRecord(log.records[0], Kind::Pressed, Button::A, 100));
    CHECK(isRecord(log.records[1], Kind::Pressed, Button::B, 100));
    CHECK(isRecord(log.records[2], Kind::Released, Button::B, 100));
}

TEST(pad_gives_nothing_while_a_button_is_held) {
    InputDriver pad;
    RawLog log;
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A), 0}, 0, log);
    InputDriverTest::update(pad, {bit(Button::A), 0, 0}, 16, log);
    InputDriverTest::update(pad, {0, 0, bit(Button::A)}, 32, log);
    CHECK_EQ_SZ(log.records.size(), 2);
    CHECK(isRecord(log.records[0], Kind::Pressed, Button::A, 0));
    CHECK(isRecord(log.records[1], Kind::Released, Button::A, 32));
}

// The app cleared the pad, and the hat released LEFT before the next poll: one reset, and no
// release of LEFT
TEST(pad_resets_when_the_app_counts_a_reset) {
    InputDriver pad;
    RawLog log;
    const uint32_t dpad = bit(Button::DpadUp) | bit(Button::DpadDown) | bit(Button::DpadLeft) |
                          bit(Button::DpadRight);
    InputDriverTest::update(pad, {bit(Button::DpadLeft), bit(Button::DpadLeft), 0}, 0, log);
    InputDriverTest::update(pad, {0, 0, dpad}, 16, log, true);
    InputDriverTest::update(pad, {0, 0, 0}, 32, log);
    CHECK_EQ_SZ(log.records.size(), 2);
    CHECK(isRecord(log.records[0], Kind::Pressed, Button::DpadLeft, 0));
    CHECK(log.records[1].kind == Kind::Reset && log.records[1].time == 16);
}

// A button that is pressed and released in one frame is released after each press of the frame
TEST(pad_gives_the_late_release_after_the_presses) {
    InputDriver pad;
    RawLog log;
    // A is pressed and released, and SELECT is pressed. A has the lower bit.
    InputDriverTest::update(pad, {bit(Button::Select), bit(Button::A) | bit(Button::Select), bit(Button::A)}, 0, log);
    CHECK_EQ_SZ(log.records.size(), 3);
    CHECK(isRecord(log.records[0], Kind::Pressed, Button::A, 0));
    CHECK(isRecord(log.records[1], Kind::Pressed, Button::Select, 0));
    CHECK(isRecord(log.records[2], Kind::Released, Button::A, 0));
}

// A held button that is released and pressed again in one frame stays held
TEST(pad_gives_the_early_release_before_the_presses) {
    InputDriver pad;
    RawLog log;
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A), 0}, 0, log);
    InputDriverTest::update(pad, {bit(Button::A), bit(Button::A), bit(Button::A)}, 16, log);
    InputDriverTest::update(pad, {0, 0, bit(Button::A)}, 32, log);
    CHECK_EQ_SZ(log.records.size(), 4);
    CHECK(isRecord(log.records[1], Kind::Released, Button::A, 16));
    CHECK(isRecord(log.records[2], Kind::Pressed, Button::A, 16));
    CHECK(isRecord(log.records[3], Kind::Released, Button::A, 32));
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
    InputDriverTest::update(pad, {0, 0, 0}, 32, log, true);  // the app cleared the pad
    CHECK(!state.isPressed(Button::B));
}

// A button that was held before the first update gives no
// pressed event. Its released event goes out.
TEST(pad_gives_only_the_release_of_a_held_button) {
    InputDriver pad;
    InputDriverTest::setHeld(pad, bit(Button::A));  // held before the first update
    RawLog log;
    InputDriverTest::update(pad, {bit(Button::A), 0, 0}, 0, log);
    CHECK_EQ_SZ(log.records.size(), 0);
    InputDriverTest::update(pad, {0, 0, bit(Button::A)}, 16, log);
    CHECK_EQ_SZ(log.records.size(), 1);
    CHECK(isRecord(log.records[0], Kind::Released, Button::A, 16));
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

// The dpad releases each direction that it does not hold: LEFT is pressed, and UP, DOWN and
// RIGHT, which were not pressed, give no released event
TEST(pad_gives_no_release_of_a_button_that_was_not_pressed) {
    InputDriver pad;
    RawLog log;
    const uint32_t others = bit(Button::DpadUp) | bit(Button::DpadDown) | bit(Button::DpadRight);
    InputDriverTest::update(pad, {bit(Button::DpadLeft), bit(Button::DpadLeft), others}, 0, log);
    InputDriverTest::update(pad, {0, 0, others | bit(Button::DpadLeft)}, 16, log);
    CHECK_EQ_SZ(log.records.size(), 2);
    CHECK(isRecord(log.records[0], Kind::Pressed, Button::DpadLeft, 0));
    CHECK(isRecord(log.records[1], Kind::Released, Button::DpadLeft, 16));
}

// ---------------------------------------------------------------------------
// ButtonDetector

TEST(immediate_short_press_at_the_press) {
    Fixture f;
    f.press(Button::B, 10);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::B, 10));
    f.tick(10, 2000);
    f.release(Button::B, 2000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
}

TEST(deferred_short_press_at_the_release) {
    Fixture f;
    f.press(Button::A, 10);
    f.tick(10, 400);
    CHECK_EQ_SZ(f.log.records.size(), 0);
    f.release(Button::A, 420);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::A, 420));
}

TEST(long_press_one_time_and_no_short_press_after_it) {
    Fixture f;
    f.press(Button::A, 0);
    f.detector.onTick(ButtonDetector::kLongPressMs - 1);
    CHECK_EQ_SZ(f.log.records.size(), 0);
    f.detector.onTick(ButtonDetector::kLongPressMs);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Long, Button::A, ButtonDetector::kLongPressMs));
    f.tick(ButtonDetector::kLongPressMs, 3000);
    f.release(Button::A, 3000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
}

TEST(repeat_after_the_delay_then_at_each_interval) {
    Fixture f;
    f.press(Button::DpadDown, 0);
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
    f.release(Button::DpadDown, 450);
    f.tick(450, 1000);
    CHECK_EQ_SZ(f.log.records.size(), 3);
}

// A hold of 450 ms with ticks of 16 ms gives the repeats at 300 and 400 ms
TEST(repeat_count_of_a_hold) {
    Fixture f;
    f.press(Button::DpadDown, 0);
    f.tick(0, 449);
    f.release(Button::DpadDown, 450);
    CHECK_EQ_SZ(f.log.records.size(), 3);
}

// A late tick gives one repeat, and each next tick one more until the schedule of the pad
// is reached again
TEST(late_tick_catches_up_one_repeat_for_each_tick) {
    Fixture f;
    f.press(Button::DpadDown, 0);
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
    f.press(Button::Select, 0);
    f.press(Button::A, 40);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isSelectACombo(f.log.records[0], 40));
}

// A then SELECT is not the combo: each member gives its own event
TEST(no_combo_in_the_other_order) {
    Fixture f;
    f.press(Button::A, 0);
    f.press(Button::Select, 40);
    f.release(Button::Select, 100);
    f.release(Button::A, 200);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::Select, 100));
    CHECK(isRecord(f.log.records[1], Kind::Short, Button::A, 200));
}

// Two presses at one time start the combo in each order
TEST(combo_at_one_time_in_each_order) {
    const Button orders[2][2] = {
        {Button::Select, Button::A},
        {Button::A, Button::Select},
    };
    for (const auto& order : orders) {
        Fixture f;
        f.press(order[0], 40);
        f.press(order[1], 40);
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

// A is pressed and released in the frame where SELECT is pressed. A has the lower bit.
TEST(combo_in_one_frame_with_a_release_of_a_member) {
    InputDriver pad;
    Fixture f;
    const uint32_t both = bit(Button::Select) | bit(Button::A);
    InputDriverTest::update(pad, {bit(Button::Select), both, bit(Button::A)}, 0, f.detector);
    InputDriverTest::update(pad, {0, 0, bit(Button::Select)}, 16, f.detector);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isSelectACombo(f.log.records[0], 0));
}

TEST(combo_members_give_no_event_until_both_are_released) {
    Fixture f;
    f.press(Button::Select, 0);
    f.press(Button::A, 10);
    f.tick(10, 2000);
    f.release(Button::A, 2000);
    f.press(Button::A, 2100);
    f.tick(2100, 3000);
    f.release(Button::A, 3000);
    f.release(Button::Select, 3100);
    CHECK_EQ_SZ(f.log.records.size(), 1);

    f.press(Button::A, 4000);
    f.release(Button::A, 4100);
    CHECK_EQ_SZ(f.log.records.size(), 2);
    CHECK(isRecord(f.log.records[1], Kind::Short, Button::A, 4100));
}

// A combo member that is held alone gives its own short press at the release
TEST(combo_member_alone_gives_a_short_press_at_the_release) {
    Fixture f;
    f.press(Button::Select, 0);
    CHECK_EQ_SZ(f.log.records.size(), 0);
    f.release(Button::Select, 100);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::Select, 100));
}

// The release comes before the tick of its frame: a long press that is due at the release
// goes out, and no short press follows
TEST(release_after_the_long_press_deadline_gives_the_long_press) {
    Fixture f;
    f.press(Button::A, 0);
    f.tick(0, 496);  // the last tick is before the deadline
    f.release(Button::A, 512);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Long, Button::A, 512));
}

TEST(release_before_the_long_press_deadline_gives_a_short_press) {
    Fixture f;
    f.press(Button::A, 0);
    f.release(Button::A, ButtonDetector::kLongPressMs - 1);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::A, ButtonDetector::kLongPressMs - 1));
}

// A repeat that is due at the release goes out before the release, as the pad gives it
TEST(release_at_the_repeat_deadline_gives_the_repeat) {
    Fixture f;
    f.press(Button::DpadDown, 0);
    f.tick(0, 288);
    f.release(Button::DpadDown, ButtonDetector::kRepeatDelayMs);
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
    detector.onRawEvent({0, event::ButtonPressed{Button::Select}});
    detector.onRawEvent({10, event::ButtonPressed{Button::L1}});
    detector.onRawEvent({20, event::ButtonPressed{Button::A}});
    CHECK_EQ_SZ(log.records.size(), 1);
    CHECK(isSelectACombo(log.records[0], 20));
}

// After a long press of A, SELECT does not start the combo
TEST(no_combo_after_a_long_press) {
    Fixture f;
    f.press(Button::A, 0);
    f.detector.onTick(ButtonDetector::kLongPressMs);
    f.press(Button::Select, 600);
    f.release(Button::Select, 700);
    f.release(Button::A, 800);
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
    detector.onRawEvent({0, event::ButtonPressed{Button::Select}});
    detector.onRawEvent({10, event::ButtonPressed{Button::A}});      // combo, SELECT stays held
    detector.onRawEvent({20, event::ButtonPressed{Button::L1}});
    detector.onRawEvent({30, event::ButtonPressed{Button::R1}});     // combo
    detector.onRawEvent({40, event::ButtonReleased{Button::L1}});
    detector.onRawEvent({50, event::ButtonReleased{Button::R1}});
    detector.onRawEvent({60, event::ButtonPressed{Button::L1}});
    detector.onRawEvent({70, event::ButtonReleased{Button::L1}});
    CHECK_EQ_SZ(log.records.size(), 3);
    CHECK(log.records[2].kind == Kind::Short && log.records[2].button == Button::L1);
}

TEST(release_with_no_press_gives_nothing) {
    Fixture f;
    f.release(Button::A, 10);
    f.release(Button::B, 20);
    CHECK_EQ_SZ(f.log.records.size(), 0);
}

// A reset ends each held button with no event: no short press, long press or repeat
TEST(reset_ends_each_held_button_with_no_event) {
    Fixture f;
    f.press(Button::A, 0);
    f.press(Button::DpadDown, 0);
    f.reset(100);
    f.tick(100, 2000);
    f.release(Button::A, 2000);
    f.release(Button::DpadDown, 2000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::DpadDown, 0));
}

// A reset clears the latch of a combo, thus its members give events again
TEST(reset_clears_a_combo) {
    Fixture f;
    f.press(Button::Select, 0);
    f.press(Button::A, 10);
    f.reset(100);
    f.press(Button::A, 200);
    f.release(Button::A, 300);
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
    InputDriverTest::update(pad, {0, 0, 0}, 48, f.detector, true);  // the app cleared the pad
    f.tick(48, 1000);
    CHECK_EQ_SZ(f.log.records.size(), 1);
    CHECK(isRecord(f.log.records[0], Kind::Short, Button::B, 0));
}

// ---------------------------------------------------------------------------
// CommonButtonHandler

namespace {

// Records each playback command as one string
class FakePlaybackControl : public PlaybackControl {
public:
    std::vector<std::string> commands;

    void togglePlayback() override { commands.push_back("toggle"); }
    void skipTrack(bool next) override { commands.push_back(next ? "next" : "previous"); }
    void changeVolume(int step) override { commands.push_back(step > 0 ? "volume+" : "volume-"); }
};

// Sets the key lock of InputState for one test, and clears it at the end
struct KeyLock {
    KeyLock() { InputState::getInstance().setKeyLocked(true); }
    ~KeyLock() { InputState::getInstance().setKeyLocked(false); }
};

// Counts the calls of the help and quit callbacks of a handler
struct Requests {
    int help = 0;
    int quit = 0;

    explicit Requests(CommonButtonHandler& handler) {
        handler.setHelpRequestedCallback([this] { help++; });
        handler.setQuitRequestedCallback([this] { quit++; });
    }
};

ButtonEvent shortPress(Button button) {
    return {0, event::ShortPress{button}};
}

ButtonEvent longPress(Button button) {
    return {0, event::LongPress{button}};
}

}  // namespace

TEST(start_short_press_asks_for_the_controls_help) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    Requests requests(handler);
    CHECK(handler.onButtonEvent(shortPress(Button::Start)));
    CHECK_EQ_INT(requests.help, 1);
    CHECK_EQ_INT(requests.quit, 0);
}

TEST(start_with_no_callback_does_nothing) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    CHECK(handler.onButtonEvent(shortPress(Button::Start)));
    CHECK(handler.onButtonEvent(longPress(Button::Start)));
}

// The handler consumes the pressed event of START and each Media event, and no other pressed event
TEST(handler_consumes_start_and_media) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    CHECK(handler.onRawEvent({0, event::ButtonPressed{Button::Start}}));
    CHECK(!handler.onRawEvent({0, event::ButtonPressed{Button::A}}));
    CHECK(handler.onRawEvent({0, event::Media{MediaKey::Next}}));
    const KeyLock lock;
    CHECK(!handler.onRawEvent({0, event::ButtonPressed{Button::Start}}));
    CHECK(handler.onRawEvent({0, event::Media{MediaKey::Next}}));
}

// The detector with the requests of the handler: the long press of START, and no short press
TEST(start_long_press_asks_for_the_quit_dialog) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    Requests requests(handler);
    ButtonDetector detector(handler, CommonButtonHandler::getButtonRequests());
    detector.onRawEvent({0, event::ButtonPressed{Button::Start}});
    for (uint32_t now = 0; now <= 600; now += 16) detector.onTick(now);
    CHECK_EQ_INT(requests.quit, 1);
    detector.onRawEvent({700, event::ButtonReleased{Button::Start}});
    CHECK_EQ_INT(requests.quit, 1);
    CHECK_EQ_INT(requests.help, 0);
}

// A volume button while START is held, in each order, ends the press of START with no dialog
TEST(start_with_a_volume_button_asks_for_no_dialog) {
    const Button volumeButtons[] = {Button::Plus, Button::Minus};
    for (const Button volume : volumeButtons) {
        for (const bool startFirst : {true, false}) {
            FakePlaybackControl playback;
            CommonButtonHandler handler(playback);
            Requests requests(handler);
            ButtonDetector detector(handler, CommonButtonHandler::getButtonRequests());
            const Button first  = startFirst ? Button::Start : volume;
            const Button second = startFirst ? volume : Button::Start;
            detector.onRawEvent({0, event::ButtonPressed{first}});
            detector.onRawEvent({100, event::ButtonPressed{second}});
            for (uint32_t now = 100; now <= 800; now += 16) detector.onTick(now);
            detector.onRawEvent({800, event::ButtonReleased{volume}});
            for (uint32_t now = 800; now <= 1500; now += 16) detector.onTick(now);
            detector.onRawEvent({1500, event::ButtonReleased{Button::Start}});
            CHECK_EQ_INT(requests.help + requests.quit, 0);
        }
    }
}

TEST(handler_consumes_only_the_combos_of_start) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    CHECK(handler.onButtonEvent({0, event::Combo{Button::Start, Button::Plus}}));
    CHECK(handler.onButtonEvent({0, event::Combo{Button::Minus, Button::Start}}));
    CHECK(!handler.onButtonEvent({0, event::Combo{Button::Select, Button::A}}));
}

TEST(other_buttons_go_to_the_page) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    Requests requests(handler);
    CHECK(!handler.onButtonEvent(shortPress(Button::A)));
    CHECK(!handler.onButtonEvent(longPress(Button::B)));
    CHECK_EQ_INT(requests.help + requests.quit, 0);
}

TEST(locked_keys_ask_for_no_dialog) {
    FakePlaybackControl playback;
    const KeyLock lock;
    CommonButtonHandler handler(playback);
    Requests requests(handler);
    CHECK(!handler.onButtonEvent(shortPress(Button::Start)));
    CHECK(!handler.onButtonEvent(longPress(Button::Start)));
    CHECK_EQ_INT(requests.help + requests.quit, 0);
}

TEST(media_key_acts_also_while_locked) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    handler.onRawEvent({0, event::Media{MediaKey::PlayPause}});
    {
        const KeyLock lock;
        handler.onRawEvent({16, event::Media{MediaKey::VolumeUp}});
    }
    handler.onRawEvent({32, event::ButtonPressed{Button::A}});
    CHECK_EQ_SZ(playback.commands.size(), 2);
    CHECK(playback.commands[0] == "toggle");
    CHECK(playback.commands[1] == "volume+");
}

TEST(media_keys_give_their_playback_commands) {
    FakePlaybackControl playback;
    CommonButtonHandler handler(playback);
    const MediaKey keys[] = {MediaKey::PlayPause, MediaKey::Next, MediaKey::Previous,
                             MediaKey::VolumeUp, MediaKey::VolumeDown};
    for (const MediaKey key : keys) handler.onRawEvent({0, event::Media{key}});
    const std::vector<std::string> want = {"toggle", "next", "previous", "volume+", "volume-"};
    CHECK(playback.commands == want);
}

// ---------------------------------------------------------------------------
// InputManager

namespace {

// Writes each event that it gets to a shared log, as "<name> <event> <button>"
class ChainListener : public InputListener {
public:
    ChainListener(const char* name, std::vector<std::string>& log) : name(name), log(log) {}

    uint32_t consumePressedMask  = 0;
    uint32_t consumeReleasedMask = 0;
    uint32_t consumeButtonMask   = 0;
    bool consumeMedia            = false;

    bool onRawEvent(const RawEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
            add("pressed", typedEvent->button);
            return (consumePressedMask & bit(typedEvent->button)) != 0;
        }
        if (const auto* typedEvent = std::get_if<event::ButtonReleased>(&event.kind)) {
            add("released", typedEvent->button);
            return (consumeReleasedMask & bit(typedEvent->button)) != 0;
        }
        if (const auto* typedEvent = std::get_if<event::ButtonCancel>(&event.kind)) {
            add("cancel", typedEvent->button);
        }
        if (std::get_if<event::ButtonReset>(&event.kind)) {
            log.push_back(std::string(name) + " reset");
        }
        if (std::get_if<event::Media>(&event.kind)) {
            log.push_back(std::string(name) + " media");
            return consumeMedia;
        }
        return false;
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            add("short", typedEvent->button);
            return (consumeButtonMask & bit(typedEvent->button)) != 0;
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            add("repeat", typedEvent->button);
            return (consumeButtonMask & bit(typedEvent->button)) != 0;
        }
        return false;
    }

private:
    void add(const char* what, Button button) {
        log.push_back(std::string(name) + " " + what + " " + getButtonName(button));
    }

    const char* name;
    std::vector<std::string>& log;
};

// A page and a handler in an input manager, as the page loop has them
struct ManagerFixture {
    std::vector<std::string> log;
    ChainListener page{"page", log};
    ChainListener handler{"handler", log};
    InputManager manager{{&page, &handler}};

    void press(Button button) { manager.onRawEvent({0, event::ButtonPressed{button}}); }
    void release(Button button) { manager.onRawEvent({0, event::ButtonReleased{button}}); }
    void shortPress(Button button) { manager.onButtonEvent({0, event::ShortPress{button}}); }
};

bool isLog(const std::vector<std::string>& log, const std::vector<std::string>& want) {
    if (log == want) return true;
    for (const std::string& line : log) printf("      %s\n", line.c_str());
    return false;
}

}  // namespace

TEST(manager_gives_an_unconsumed_press_to_each_listener) {
    ManagerFixture f;
    f.press(Button::A);
    f.shortPress(Button::A);
    f.release(Button::A);
    CHECK(isLog(f.log, {"page pressed A", "handler pressed A", "page short A", "handler short A",
                        "page released A", "handler released A"}));
}

// The page consumes a button event: the handler that saw the pressed event gets a cancel, and the
// page gets each later event of the gesture
TEST(manager_button_event_sets_the_owner) {
    ManagerFixture f;
    f.page.consumeButtonMask     = bit(Button::A);
    f.handler.consumePressedMask = bit(Button::Start);
    f.press(Button::A);
    f.shortPress(Button::A);
    f.press(Button::Start);
    f.release(Button::Start);
    f.release(Button::A);
    CHECK(isLog(f.log, {"page pressed A", "handler pressed A", "page short A", "handler cancel A",
                        "page pressed START", "page released START", "page released A"}));
}

// The page intercepts a released event: the handler gets a cancel of the button in place of it
TEST(manager_intercepted_release_cancels_the_later_listener) {
    ManagerFixture f;
    f.page.consumeReleasedMask = bit(Button::A);
    f.press(Button::A);
    f.release(Button::A);
    CHECK(isLog(f.log, {"page pressed A", "handler pressed A", "page released A",
                        "handler cancel A"}));
}

// The handler consumes START: the page that saw the pressed event gets a cancel, and the handler
// gets each event of each button until no button is held
TEST(manager_owner_cancels_the_earlier_listener) {
    ManagerFixture f;
    f.handler.consumePressedMask = bit(Button::Start);
    f.page.consumeButtonMask     = bit(Button::A);
    f.press(Button::Start);
    f.press(Button::A);
    f.shortPress(Button::A);
    f.release(Button::A);
    f.release(Button::Start);
    CHECK(isLog(f.log, {"page pressed START", "handler pressed START", "page cancel START",
                        "handler pressed A", "handler short A", "handler released A",
                        "handler released START"}));
}

// The button events of the last release go to the owner. The next gesture has no owner.
TEST(manager_gesture_ends_after_the_last_release) {
    ManagerFixture f;
    f.handler.consumePressedMask = bit(Button::Start);
    f.press(Button::Start);
    f.release(Button::Start);
    f.shortPress(Button::Start);
    f.log.clear();
    f.press(Button::B);
    CHECK(isLog(f.log, {"page pressed B", "handler pressed B"}));
}

// An owner cancels each held button that another listener saw, and that listener gets no released
// event
TEST(manager_owner_cancels_each_held_button) {
    ManagerFixture f;
    f.page.consumePressedMask = bit(Button::B);
    f.press(Button::A);
    f.press(Button::B);
    f.release(Button::A);
    f.release(Button::B);
    CHECK(isLog(f.log, {"page pressed A", "handler pressed A", "page pressed B", "handler cancel A",
                        "page released A", "page released B"}));
}

TEST(manager_reset_ends_the_gesture_and_reaches_each_listener) {
    ManagerFixture f;
    f.handler.consumePressedMask = bit(Button::Start);
    f.press(Button::Start);
    f.manager.onRawEvent({0, event::ButtonReset{}});
    f.log.clear();
    f.press(Button::A);
    CHECK(isLog(f.log, {"page pressed A", "handler pressed A"}));
}

TEST(manager_media_goes_to_the_first_consumer_or_to_the_owner) {
    ManagerFixture f;
    f.handler.consumeMedia = true;
    f.manager.onRawEvent({0, event::Media{MediaKey::PlayPause}});
    f.page.consumePressedMask = bit(Button::A);
    f.press(Button::A);
    f.manager.onRawEvent({0, event::Media{MediaKey::PlayPause}});
    CHECK(isLog(f.log, {"page media", "handler media", "page pressed A", "page media"}));
}

// The handler consumes Media while a button is held: the page gets a cancel of that button
TEST(manager_media_sets_the_owner_of_a_held_button) {
    ManagerFixture f;
    f.handler.consumeMedia = true;
    f.press(Button::DpadDown);
    f.manager.onRawEvent({0, event::Media{MediaKey::PlayPause}});
    f.shortPress(Button::DpadDown);
    f.release(Button::DpadDown);
    CHECK(isLog(f.log, {"page pressed DOWN", "handler pressed DOWN", "page media", "handler media",
                        "page cancel DOWN", "handler short DOWN", "handler released DOWN"}));
}

// The detector gets each raw event after the input manager, as in the page loop
TEST(manager_gives_the_repeats_to_the_owner_of_the_short_press) {
    ManagerFixture f;
    f.page.consumeButtonMask                 = bit(Button::DpadDown);
    static constexpr ButtonRequest kRepeat[] = {request::Repeat{Button::DpadDown}};
    ButtonDetector detector{f.manager, kRepeat};
    const RawEvent pressed{0, event::ButtonPressed{Button::DpadDown}};
    f.manager.onRawEvent(pressed);
    detector.onRawEvent(pressed);
    detector.onTick(ButtonDetector::kRepeatDelayMs);
    detector.onTick(ButtonDetector::kRepeatDelayMs + ButtonDetector::kRepeatIntervalMs);
    CHECK(isLog(f.log, {"page pressed DOWN", "handler pressed DOWN", "page short DOWN",
                        "handler cancel DOWN", "page repeat DOWN", "page repeat DOWN"}));
}

int main(void) {
    printf("input events\n");
    RUN(pad_gives_buttons_in_bit_order_and_presses_before_releases);
    RUN(pad_gives_nothing_while_a_button_is_held);
    RUN(pad_resets_when_the_app_counts_a_reset);
    RUN(pad_gives_the_late_release_after_the_presses);
    RUN(pad_gives_the_early_release_before_the_presses);
    RUN(pad_state_gives_the_held_buttons);
    RUN(pad_gives_only_the_release_of_a_held_button);
    RUN(pad_state_gives_a_button_held_before_the_first_update);
    RUN(pad_gives_no_release_of_a_button_that_was_not_pressed);
    RUN(immediate_short_press_at_the_press);
    RUN(deferred_short_press_at_the_release);
    RUN(long_press_one_time_and_no_short_press_after_it);
    RUN(repeat_after_the_delay_then_at_each_interval);
    RUN(repeat_count_of_a_hold);
    RUN(late_tick_catches_up_one_repeat_for_each_tick);
    RUN(combo_in_the_order_of_the_request);
    RUN(no_combo_in_the_other_order);
    RUN(combo_at_one_time_in_each_order);
    RUN(combo_in_one_frame);
    RUN(combo_in_one_frame_with_a_release_of_a_member);
    RUN(combo_members_give_no_event_until_both_are_released);
    RUN(combo_member_alone_gives_a_short_press_at_the_release);
    RUN(release_after_the_long_press_deadline_gives_the_long_press);
    RUN(release_before_the_long_press_deadline_gives_a_short_press);
    RUN(release_at_the_repeat_deadline_gives_the_repeat);
    RUN(first_matching_combo_wins);
    RUN(no_combo_after_a_long_press);
    RUN(each_combo_blocks_only_its_members);
    RUN(release_with_no_press_gives_nothing);
    RUN(reset_ends_each_held_button_with_no_event);
    RUN(reset_clears_a_combo);
    RUN(media_gives_nothing);
    RUN(pad_and_detector_together);
    RUN(start_short_press_asks_for_the_controls_help);
    RUN(start_with_no_callback_does_nothing);
    RUN(handler_consumes_start_and_media);
    RUN(start_long_press_asks_for_the_quit_dialog);
    RUN(start_with_a_volume_button_asks_for_no_dialog);
    RUN(handler_consumes_only_the_combos_of_start);
    RUN(other_buttons_go_to_the_page);
    RUN(locked_keys_ask_for_no_dialog);
    RUN(media_key_acts_also_while_locked);
    RUN(media_keys_give_their_playback_commands);
    RUN(manager_gives_an_unconsumed_press_to_each_listener);
    RUN(manager_button_event_sets_the_owner);
    RUN(manager_intercepted_release_cancels_the_later_listener);
    RUN(manager_owner_cancels_the_earlier_listener);
    RUN(manager_gesture_ends_after_the_last_release);
    RUN(manager_owner_cancels_each_held_button);
    RUN(manager_reset_ends_the_gesture_and_reaches_each_listener);
    RUN(manager_media_goes_to_the_first_consumer_or_to_the_owner);
    RUN(manager_media_sets_the_owner_of_a_held_button);
    RUN(manager_gives_the_repeats_to_the_owner_of_the_short_press);
    return test_summary();
}
