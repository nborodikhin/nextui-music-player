#ifndef __INPUT_INPUT_STATE_HPP__
#define __INPUT_INPUT_STATE_HPP__

#include <cstdint>

#include "input_events.hpp"

namespace input {

// The state of the pad in one frame, one bit for each Button.
struct PadState {
    uint32_t pressed;
    uint32_t justPressed;
    uint32_t justReleased;
};

// The bar that the platform draws in place of the status group while a hardware setting
// changes. Each value is the value of show_setting in NextUI.
enum class HardwareBar : int {
    None       = 0,
    Brightness = 1,
    Volume     = 2,
    ColorTemp  = 3,
};

// The state of the buttons in the current frame. The input driver writes it one time in each
// frame, before the events of the frame, thus a listener of an event reads the state of the
// full frame. There is one state, because there is one pad.
class InputState {
public:
    static InputState& getInstance() {
        static InputState state;
        return state;
    }

    InputState(const InputState&)            = delete;
    InputState& operator=(const InputState&) = delete;

    bool isPressed(Button button) const { return (padState.pressed & getButtonBit(button)) != 0; }
    bool isJustPressed(Button button) const { return (padState.justPressed & getButtonBit(button)) != 0; }
    bool isJustReleased(Button button) const { return (padState.justReleased & getButtonBit(button)) != 0; }

    // Returns the bar that the header shows in place of the status group. A press of a
    // hardware setting shows it, and it stays for a time after the last press.
    HardwareBar getHardwareBar() const { return hardwareBar; }

    // Returns the width of the status group, or of the hardware bar in its place. The title of
    // a page takes the room that is left. It changes with the hardware bar, the clock, Wi-Fi
    // and Bluetooth.
    int getStatusGroupWidth() const { return statusGroupWidth; }

    // Returns the time of the current frame in milliseconds.
    uint32_t getTime() const { return time; }

    // Records the state of a new frame. Only the input driver calls it.
    void setState(const PadState& framePadState, uint32_t frameTime) {
        padState = framePadState;
        time  = frameTime;
    }

    // Records the hardware bar. Only the common button handler calls it.
    void setHardwareBar(HardwareBar bar) { hardwareBar = bar; }

    // Records the width of the status group. Only the coordinator of a page calls it.
    void setStatusGroupWidth(int width) { statusGroupWidth = width; }

private:
    InputState() = default;

    PadState padState{};
    uint32_t time = 0;
    HardwareBar hardwareBar = HardwareBar::None;
    int statusGroupWidth    = 0;
};

}  // namespace input

#endif
