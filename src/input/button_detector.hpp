#ifndef __INPUT_BUTTON_DETECTOR_HPP__
#define __INPUT_BUTTON_DETECTOR_HPP__

#include <array>
#include <cstdint>
#include <span>

#include "input_events.hpp"

namespace input {

// Turns the raw button events and the time into button events, and gives them to a
// listener. A short press of each button is always on. The requests add a long press, a
// repeat or a combo.
class ButtonDetector : public RawEventListener {
public:
    static constexpr uint32_t kLongPressMs      = 500;
    static constexpr uint32_t kRepeatDelayMs    = 300;
    static constexpr uint32_t kRepeatIntervalMs = 100;
    static constexpr int kMaxCombos             = 8;

    // Copies the requests. A button cannot have a repeat and a long press, and a member of
    // a combo cannot have a repeat. At most kMaxCombos combos. When two combos share the
    // member that goes down, the first one in the requests wins.
    ButtonDetector(ButtonListener& listener, std::span<const ButtonRequest> requests);

    void onRawEvent(const RawEvent& event) override;

    // Gives the long presses and the repeats that are due at the time now.
    void onTick(uint32_t now);

private:
    enum class Mode : uint8_t {
        // Not held, or held from before the detector saw it go down
        Idle,
        // The short press went out at the down
        Immediate,
        // The short press goes out at the up, if no long press or combo went out
        Deferred,
        // The short press went out at the down, and the repeats follow
        Repeat,
        // A long press or a combo went out. No more events until the up
        Done,
    };

    struct ButtonState {
        Mode mode           = Mode::Idle;
        uint32_t downTime   = 0;
        uint32_t nextRepeat = 0;
    };

    void onButtonDown(Button button, uint32_t time);
    void onButtonUp(Button button, uint32_t time);
    // Gives the long press or the repeat of the button that is due at the time now
    void sendDueEvent(Button button, uint32_t now);
    void reset();
    bool startCombo(Button button, uint32_t time);
    // Returns the members of each combo that went out and has a member still held
    uint32_t getBlockedMask() const;
    void send(uint32_t time, decltype(ButtonEvent::kind) kind);
    ButtonState& getState(Button button);

    ButtonListener& listener;
    uint32_t longPressMask = 0;
    uint32_t repeatMask    = 0;
    uint32_t comboMask     = 0;
    std::array<request::Combo, kMaxCombos> combos{};
    int comboCount = 0;
    // A combo that went out stays latched until each of its members is up. Its members give
    // no event while it is latched.
    std::array<bool, kMaxCombos> comboLatched{};
    uint32_t heldMask = 0;
    std::array<ButtonState, kButtonCount> states{};
};

}  // namespace input

#endif
