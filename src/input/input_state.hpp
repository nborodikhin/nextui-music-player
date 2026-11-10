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

    // Returns true when the status group must draw again: the volume or the brightness changed,
    // or the power management asked for a draw. The status group clears it when it draws.
    bool isStatusChanged() const { return statusChanged; }

    // Returns the time of the current frame in milliseconds.
    uint32_t getTime() const { return time; }

    // Returns true while the keys are locked, as on HDMI after the screen timeout. Only the
    // earphone buttons act then.
    bool isKeyLocked() const { return keyLocked; }

    // Records the state of a new frame. Only the input driver calls it.
    void setState(const PadState& framePadState, uint32_t frameTime) {
        padState = framePadState;
        time  = frameTime;
    }

    // Records the hardware bar. Only the common button handler calls it.
    void setHardwareBar(HardwareBar bar) { hardwareBar = bar; }

    // Records that the status group must draw again. Only the page loop calls it.
    void setStatusChanged() { statusChanged = true; }

    // Records that the status group drew. Only the status group calls it.
    void clearStatusChanged() { statusChanged = false; }

    // Records the key lock of the frame. Only the coordinator of a page calls it.
    void setKeyLocked(bool locked) { keyLocked = locked; }

private:
    InputState() = default;

    PadState padState{};
    uint32_t time = 0;
    HardwareBar hardwareBar = HardwareBar::None;
    bool statusChanged      = false;
    bool keyLocked          = false;
};

}  // namespace input

#endif
