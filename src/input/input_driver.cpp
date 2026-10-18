#include "input_driver.hpp"

namespace input {

// Gives an event of type Event to the listener for each button in the mask, in the order of
// the bit index.
template <typename Event>
static void sendButtonEvents(uint32_t mask, uint32_t now, RawEventListener& listener) {
    for (int index = 0; index < kButtonCount; index++) {
        const Button button = static_cast<Button>(index);
        if (mask & getButtonBit(button)) {
            listener.onRawEvent({now, Event{button}});
        }
    }
}

void InputDriver::update(const PadState& padState, bool padReset, uint32_t now,
                         RawEventListener& listener) {
    // A reset that the app did not count makes a held button leave the pad with no release
    const bool reset = padReset || (pressedPads & ~padState.pressed & ~padState.justReleased) != 0;
    if (reset) {
        pressedPads = 0;
    }

    // The dpad sets a release of each direction that it does not hold, also of a direction
    // that was not down
    const uint32_t justReleased = padState.justReleased & (pressedPads | padState.justPressed);

    // A held button that goes up goes up before the downs of the frame. A button that goes down
    // and up in the frame goes up after them, thus it is held at each down of the frame, and a
    // combo of the frame is found in each bit order.
    const uint32_t earlyUps = justReleased & pressedPads;
    const uint32_t lateUps  = justReleased & padState.justPressed & ~padState.pressed;
    pressedPads = ((pressedPads & ~earlyUps) | padState.justPressed) & ~lateUps;

    // The pressed mask comes from the pad, thus a button that was held before the first frame
    // reads as pressed. Only its events are not sent.
    const PadState validatedState = {
        .pressed      = padState.pressed,
        .justPressed  = padState.justPressed,
        .justReleased = earlyUps | lateUps,
    };
    InputState::getInstance().setState(validatedState, now);

    if (reset) {
        listener.onRawEvent({now, event::ButtonReset{}});
    }
    sendButtonEvents<event::ButtonUp>(earlyUps, now, listener);
    sendButtonEvents<event::ButtonDown>(padState.justPressed, now, listener);
    sendButtonEvents<event::ButtonUp>(lateUps, now, listener);
}

}  // namespace input
