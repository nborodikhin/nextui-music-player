// The part of the input driver that reads NextUI, SDL and the player. The rest is in
// input_driver.cpp, which a host test links with no platform.

#include "input_driver.hpp"

#include <optional>

#include "button_detector.hpp"
#include "pad_reset.h"
#include "player.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

namespace input {

static_assert(static_cast<int>(Button::DpadUp) == BTN_ID_DPAD_UP);
static_assert(static_cast<int>(Button::DpadDown) == BTN_ID_DPAD_DOWN);
static_assert(static_cast<int>(Button::DpadLeft) == BTN_ID_DPAD_LEFT);
static_assert(static_cast<int>(Button::DpadRight) == BTN_ID_DPAD_RIGHT);
static_assert(static_cast<int>(Button::A) == BTN_ID_A);
static_assert(static_cast<int>(Button::B) == BTN_ID_B);
static_assert(static_cast<int>(Button::X) == BTN_ID_X);
static_assert(static_cast<int>(Button::Y) == BTN_ID_Y);
static_assert(static_cast<int>(Button::Start) == BTN_ID_START);
static_assert(static_cast<int>(Button::Select) == BTN_ID_SELECT);
static_assert(static_cast<int>(Button::L1) == BTN_ID_L1);
static_assert(static_cast<int>(Button::R1) == BTN_ID_R1);
static_assert(static_cast<int>(Button::L2) == BTN_ID_L2);
static_assert(static_cast<int>(Button::R2) == BTN_ID_R2);
static_assert(static_cast<int>(Button::L3) == BTN_ID_L3);
static_assert(static_cast<int>(Button::R3) == BTN_ID_R3);
static_assert(static_cast<int>(Button::L4) == BTN_ID_L4);
static_assert(static_cast<int>(Button::R4) == BTN_ID_R4);
static_assert(static_cast<int>(Button::Menu) == BTN_ID_MENU);
static_assert(static_cast<int>(Button::Plus) == BTN_ID_PLUS);
static_assert(static_cast<int>(Button::Minus) == BTN_ID_MINUS);
static_assert(static_cast<int>(Button::Power) == BTN_ID_POWER);
static_assert(static_cast<int>(Button::PowerOff) == BTN_ID_POWEROFF);
static_assert(static_cast<int>(Button::AnalogUp) == BTN_ID_ANALOG_UP);
static_assert(static_cast<int>(Button::AnalogDown) == BTN_ID_ANALOG_DOWN);
static_assert(static_cast<int>(Button::AnalogLeft) == BTN_ID_ANALOG_LEFT);
static_assert(static_cast<int>(Button::AnalogRight) == BTN_ID_ANALOG_RIGHT);
static_assert(static_cast<int>(Button::Home) == BTN_ID_HOME);
static_assert(kButtonCount == BTN_ID_COUNT);

// A held button repeats at the same times as a list that reads the pad
static_assert(ButtonDetector::kRepeatDelayMs == PAD_REPEAT_DELAY);
static_assert(ButtonDetector::kRepeatIntervalMs == PAD_REPEAT_INTERVAL);

// Returns the pad state of the last poll of NextUI.
static PadState readPadState() {
    const PAD_Context &padContext = pad;
    return {
        .pressed      = static_cast<uint32_t>(padContext.is_pressed),
        .justPressed  = static_cast<uint32_t>(padContext.just_pressed),
        .justReleased = static_cast<uint32_t>(padContext.just_released),
    };
}

static std::optional<MediaKey> toMediaKey(USBHIDEvent hidEvent) {
    switch (hidEvent) {
        case USB_HID_EVENT_PLAY_PAUSE:  return MediaKey::PlayPause;
        case USB_HID_EVENT_NEXT_TRACK:  return MediaKey::Next;
        case USB_HID_EVENT_PREV_TRACK:  return MediaKey::Previous;
        case USB_HID_EVENT_VOLUME_UP:   return MediaKey::VolumeUp;
        case USB_HID_EVENT_VOLUME_DOWN: return MediaKey::VolumeDown;
        case USB_HID_EVENT_NONE:        return std::nullopt;
    }
    return std::nullopt;
}

void InputDriver::poll(RawEventListener& listener) {
    const uint32_t time = SDL_GetTicks();
    // A driver starts with no held button, thus a reset before its first poll is not a change
    const uint32_t resetCount = PadReset_getCount();
    const bool padReset = padResetCount && *padResetCount != resetCount;
    padResetCount = resetCount;
    update(readPadState(), padReset, time, listener);

    USBHIDEvent hidEvent;
    while ((hidEvent = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (const std::optional<MediaKey> key = toMediaKey(hidEvent)) {
            listener.onRawEvent({time, event::Media{*key}});
        }
    }
}

}  // namespace input
