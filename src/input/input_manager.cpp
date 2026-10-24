#include "input_manager.hpp"

#include <utility>

namespace input {

InputManager::InputManager(std::vector<InputListener*> chain)
    : listeners(std::move(chain)), seenMasks(listeners.size(), 0) {}

bool InputManager::onRawEvent(const RawEvent& event) {
    // The last up ended the gesture: the next raw event starts with no owner
    if (heldMask == 0) owner = kNoOwner;

    if (const auto* typedEvent = std::get_if<event::ButtonDown>(&event.kind)) {
        onButtonDown(event, typedEvent->button);
    }
    if (const auto* typedEvent = std::get_if<event::ButtonUp>(&event.kind)) {
        onButtonUp(event, typedEvent->button);
    }
    if (std::get_if<event::ButtonReset>(&event.kind)) {
        onReset(event);
    }
    if (std::get_if<event::Media>(&event.kind)) {
        onMedia(event);
    }
    return false;
}

bool InputManager::onButtonEvent(const ButtonEvent& event) {
    if (owner != kNoOwner) return listeners[owner]->onButtonEvent(event);
    for (size_t index = 0; index < listeners.size(); index++) {
        if (listeners[index]->onButtonEvent(event)) {
            setOwner(index, event.time);
            return true;
        }
    }
    return false;
}

void InputManager::onButtonDown(const RawEvent& event, Button button) {
    const uint32_t bit = getButtonBit(button);
    heldMask |= bit;
    if (owner != kNoOwner) {
        seenMasks[owner] |= bit;
        listeners[owner]->onRawEvent(event);
        return;
    }
    for (size_t index = 0; index < listeners.size(); index++) {
        seenMasks[index] |= bit;
        if (listeners[index]->onRawEvent(event)) {
            setOwner(index, event.time);
            return;
        }
    }
}

void InputManager::onButtonUp(const RawEvent& event, Button button) {
    const uint32_t bit = getButtonBit(button);
    heldMask &= ~bit;
    for (size_t index = 0; index < listeners.size(); index++) {
        if (!(seenMasks[index] & bit)) continue;
        seenMasks[index] &= ~bit;
        if (listeners[index]->onRawEvent(event) && owner == kNoOwner) {
            // The later listeners get a cancel in place of the up
            setOwner(index, event.time);
            return;
        }
    }
}

void InputManager::onMedia(const RawEvent& event) {
    if (owner != kNoOwner) {
        listeners[owner]->onRawEvent(event);
        return;
    }
    for (size_t index = 0; index < listeners.size(); index++) {
        if (listeners[index]->onRawEvent(event)) {
            setOwner(index, event.time);
            return;
        }
    }
}

void InputManager::onReset(const RawEvent& event) {
    heldMask = 0;
    owner    = kNoOwner;
    for (size_t index = 0; index < listeners.size(); index++) {
        seenMasks[index] = 0;
        listeners[index]->onRawEvent(event);
    }
}

void InputManager::setOwner(size_t index, uint32_t time) {
    owner = static_cast<int>(index);
    for (size_t other = 0; other < listeners.size(); other++) {
        if (other == index) continue;
        const uint32_t seen = seenMasks[other];
        seenMasks[other]    = 0;
        for (int buttonIndex = 0; buttonIndex < kButtonCount; buttonIndex++) {
            const Button button = static_cast<Button>(buttonIndex);
            if (seen & getButtonBit(button)) {
                listeners[other]->onRawEvent({time, event::ButtonCancel{button}});
            }
        }
    }
}

}  // namespace input
