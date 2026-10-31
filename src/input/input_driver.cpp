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
    if (padReset) {
        pressedPads = 0;
    }

    // The dpad sets a release of each direction that it does not hold, also of a direction
    // that was not pressed
    const uint32_t justReleased = padState.justReleased & (pressedPads | padState.justPressed);

    // A held button that is released is released before the presses of the frame. A button that
    // is pressed and released in the frame is released after them, thus it is held at each press
    // of the frame, and a combo of the frame is found in each bit order.
    const uint32_t earlyReleases = justReleased & pressedPads;
    const uint32_t lateReleases  = justReleased & padState.justPressed & ~padState.pressed;
    pressedPads = ((pressedPads & ~earlyReleases) | padState.justPressed) & ~lateReleases;

    // The pressed mask comes from the pad, thus a button that was held before the first frame
    // reads as pressed. Only its events are not sent.
    const PadState validatedState = {
        .pressed      = padState.pressed,
        .justPressed  = padState.justPressed,
        .justReleased = earlyReleases | lateReleases,
    };
    InputState::getInstance().setState(validatedState, now);

    if (padReset) {
        listener.onRawEvent({now, event::ButtonReset{}});
    }
    sendButtonEvents<event::ButtonReleased>(earlyReleases, now, listener);
    sendButtonEvents<event::ButtonPressed>(padState.justPressed, now, listener);
    sendButtonEvents<event::ButtonReleased>(lateReleases, now, listener);
}

}  // namespace input
