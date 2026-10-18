#ifndef __INPUT_COMMON_BUTTON_HANDLER_HPP__
#define __INPUT_COMMON_BUTTON_HANDLER_HPP__

#include <cstdint>

#include "input_events.hpp"
#include "input_state.hpp"

namespace input {

// The work of each frame that is the same on each page: power management, and later the
// buttons that act on each page.
class CommonButtonHandler : public ButtonListener {
public:
    // Returns false: no button acts on each page yet.
    bool onButtonEvent(const ButtonEvent& event) override;

    // Runs power management for the frame, and writes the hardware bar to InputState. Returns
    // true when the screen must draw again: a wake from sleep, or a change of the status group
    // or of the hardware bar.
    bool onTick(uint32_t now);

    // Returns true when the device woke from sleep in the last onTick(). The sleep cleared the
    // screen.
    bool hasWoken() const { return woke; }

private:
    bool woke = false;
};

}  // namespace input

#endif
