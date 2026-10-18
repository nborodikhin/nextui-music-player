#include "input_driver.hpp"

namespace input {

// Gives the raw events of one frame to the listener: the reset first, then the buttons in the
// order of their bit index, and for each button a down before an up.
static void sendPadEvents(const PadState& padState, bool reset, uint32_t now,
                          RawEventListener& listener) {
    if (reset) {
        listener.onRawEvent({now, event::ButtonReset{}});
    }
    for (int index = 0; index < kButtonCount; index++) {
        const Button button = static_cast<Button>(index);
        const uint32_t bit = getButtonBit(button);

        if (padState.justPressed & bit) {
            listener.onRawEvent({now, event::ButtonDown{button}});
        }
        if (padState.justReleased & bit) {
            listener.onRawEvent({now, event::ButtonUp{button}});
        }
    }
}

void InputDriver::update(const PadState& padState, uint32_t now, RawEventListener& listener) {
    // NextUI gives no signal on PAD_reset. Detect that but comparing our
    // record with reported "just released"
    const bool reset = (pressedPads & ~padState.pressed & ~padState.justReleased) != 0;
    if (reset) {
        pressedPads = 0;
    }

    // The dpad sets a release of each direction that it does not hold, also of a direction
    // that was not down
    const uint32_t justReleased = padState.justReleased & (pressedPads | padState.justPressed);
    pressedPads = (pressedPads | padState.justPressed) & ~justReleased;

    // The pressed mask comes from the pad, thus a button that was held before the first frame
    // reads as pressed. Only its events are not sent.
    const PadState validatedState = {
        .pressed      = padState.pressed,
        .justPressed  = padState.justPressed,
        .justReleased = justReleased,
    };
    InputState::getInstance().setState(validatedState, now);
    sendPadEvents(validatedState, reset, now, listener);
}

}  // namespace input
