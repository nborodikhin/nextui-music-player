#ifndef __INPUT_INPUT_DRIVER_HPP__
#define __INPUT_INPUT_DRIVER_HPP__

#include <cstdint>
#include <optional>

#include "input_listeners.hpp"
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

    // Pass true in padReset when the app cleared the pad of NextUI since the last frame.
    void update(const PadState& padState, bool padReset, uint32_t now, RawEventListener& listener);

    uint32_t pressedPads = 0;
    // The count of pad resets at the last poll. Empty before the first poll.
    std::optional<uint32_t> padResetCount;
};

}  // namespace input

#endif
