#ifndef __INPUT_INPUT_DRIVER_HPP__
#define __INPUT_INPUT_DRIVER_HPP__

#include <cstdint>

#include "input_events.hpp"
#include "input_state.hpp"

namespace input {

// Translates the input devices (buttons and the USB HID) into events.
class InputDriver {
public:
    // Captures the current state into InputState
    // Emits corresponding set of RawEvents.
    void poll(RawEventListener& listener);

private:
    friend struct InputDriverTest;

    void update(const PadState& padState, uint32_t now, RawEventListener& listener);

    uint32_t pressedPads = 0;
};

}  // namespace input

#endif
