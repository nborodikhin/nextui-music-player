#ifndef __INPUT_INPUT_EVENTS_HPP__
#define __INPUT_INPUT_EVENTS_HPP__

#include <cstdint>
#include <variant>

namespace input {

// A button of the pad. Each value is the bit index of the button in the pad masks.
enum class Button : uint8_t {
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    A,
    B,
    X,
    Y,
    Start,
    Select,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    L4,
    R4,
    Menu,
    Plus,
    Minus,
    Power,
    PowerOff,
    AnalogUp,
    AnalogDown,
    AnalogLeft,
    AnalogRight,
    Home,
};

inline constexpr int kButtonCount = static_cast<int>(Button::Home) + 1;

inline constexpr uint32_t getButtonBit(Button button) {
    return 1u << static_cast<int>(button);
}

// A button of USB earphones.
enum class MediaKey : uint8_t {
    PlayPause,
    Next,
    Previous,
    VolumeUp,
    VolumeDown,
};

// The raw events: each change of an input source, with no filter.
namespace event {
struct ButtonDown {
    Button button;
};

struct ButtonUp {
    Button button;
};

// Another listener consumed an event of the gesture. The button is up, with no release.
struct ButtonCancel {
    Button button;
};

// Each held button is up, with no release. The gesture ends.
struct ButtonReset {};

struct Media {
    MediaKey key;
};
}  // namespace event

struct RawEvent {
    uint32_t time;
    std::variant<event::ButtonDown, event::ButtonUp, event::ButtonReset, event::ButtonCancel,
                 event::Media>
        kind;
};

// The button events: what the input means.
namespace event {
struct ShortPress {
    Button button;
};

struct LongPress {
    Button button;
};

struct Repeat {
    Button button;
};

// The members in the order of the request. The first member went down first, or both
// went down in one frame.
struct Combo {
    Button first;
    Button second;
};
}  // namespace event

struct ButtonEvent {
    uint32_t time;
    std::variant<event::ShortPress, event::LongPress, event::Repeat, event::Combo> kind;
};

// What a listener asks the detector for. The short press is always on.
namespace request {
struct LongPress {
    Button button;
};

struct Repeat {
    Button button;
};

// The first member is held, and then the second member goes down.
struct Combo {
    Button first;
    Button second;
};
}  // namespace request

using ButtonRequest = std::variant<request::LongPress, request::Repeat, request::Combo>;

inline constexpr const char* getButtonName(Button button) {
    switch (button) {
        case Button::DpadUp:      return "UP";
        case Button::DpadDown:    return "DOWN";
        case Button::DpadLeft:    return "LEFT";
        case Button::DpadRight:   return "RIGHT";
        case Button::A:           return "A";
        case Button::B:           return "B";
        case Button::X:           return "X";
        case Button::Y:           return "Y";
        case Button::Start:       return "START";
        case Button::Select:      return "SELECT";
        case Button::L1:          return "L1";
        case Button::R1:          return "R1";
        case Button::L2:          return "L2";
        case Button::R2:          return "R2";
        case Button::L3:          return "L3";
        case Button::R3:          return "R3";
        case Button::L4:          return "L4";
        case Button::R4:          return "R4";
        case Button::Menu:        return "MENU";
        case Button::Plus:        return "PLUS";
        case Button::Minus:       return "MINUS";
        case Button::Power:       return "POWER";
        case Button::PowerOff:    return "POWEROFF";
        case Button::AnalogUp:    return "STICK UP";
        case Button::AnalogDown:  return "STICK DOWN";
        case Button::AnalogLeft:  return "STICK LEFT";
        case Button::AnalogRight: return "STICK RIGHT";
        case Button::Home:        return "HOME";
    }
    return "?";
}

inline constexpr const char* getMediaKeyName(MediaKey key) {
    switch (key) {
        case MediaKey::PlayPause:  return "PLAY/PAUSE";
        case MediaKey::Next:       return "NEXT";
        case MediaKey::Previous:   return "PREVIOUS";
        case MediaKey::VolumeUp:   return "VOLUME UP";
        case MediaKey::VolumeDown: return "VOLUME DOWN";
    }
    return "?";
}

}  // namespace input

#endif
