#include "common_button_handler.hpp"

#include <utility>

namespace input {

namespace {

constexpr ButtonRequest kRequests[] = {
    request::LongPress{Button::Start},
    // The platform changes the volume. The combo ends the press of START, in each order.
    request::Combo{Button::Start, Button::Plus},
    request::Combo{Button::Start, Button::Minus},
    request::Combo{Button::Plus, Button::Start},
    request::Combo{Button::Minus, Button::Start},
};

}  // namespace

CommonButtonHandler::CommonButtonHandler(PlaybackControl& playback) : playback(playback) {}

void CommonButtonHandler::setHelpRequestedCallback(std::function<void()> callback) {
    onHelpRequested = std::move(callback);
}

void CommonButtonHandler::setQuitRequestedCallback(std::function<void()> callback) {
    onQuitRequested = std::move(callback);
}

std::span<const ButtonRequest> CommonButtonHandler::getButtonRequests() {
    return kRequests;
}

// Gives each button of USB earphones its playback command
static void runMediaKey(PlaybackControl& playback, MediaKey key) {
    switch (key) {
        case MediaKey::PlayPause:  playback.togglePlayback(); break;
        case MediaKey::Next:       playback.skipTrack(true); break;
        case MediaKey::Previous:   playback.skipTrack(false); break;
        case MediaKey::VolumeUp:   playback.changeVolume(1); break;
        case MediaKey::VolumeDown: playback.changeVolume(-1); break;
    }
}

bool CommonButtonHandler::onRawEvent(const RawEvent& event) {
    // The earphone buttons also work while the keys are locked
    if (const auto* typedEvent = std::get_if<event::Media>(&event.kind)) {
        runMediaKey(playback, typedEvent->key);
        return true;
    }
    if (const auto* typedEvent = std::get_if<event::ButtonDown>(&event.kind)) {
        return typedEvent->button == Button::Start && !InputState::getInstance().isKeyLocked();
    }
    return false;
}

static bool isStartCombo(const event::Combo& combo) {
    return combo.first == Button::Start || combo.second == Button::Start;
}

bool CommonButtonHandler::onButtonEvent(const ButtonEvent& event) {
    if (InputState::getInstance().isKeyLocked()) return false;

    if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
        if (typedEvent->button != Button::Start) return false;
        if (onHelpRequested) onHelpRequested();
        return true;
    }
    if (const auto* typedEvent = std::get_if<event::LongPress>(&event.kind)) {
        if (typedEvent->button != Button::Start) return false;
        if (onQuitRequested) onQuitRequested();
        return true;
    }
    if (const auto* typedEvent = std::get_if<event::Combo>(&event.kind)) {
        return isStartCombo(*typedEvent);
    }
    return false;
}

}  // namespace input
