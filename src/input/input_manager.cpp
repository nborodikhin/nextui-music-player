#include "input_manager.hpp"

#include <utility>

namespace input {

InputManager::InputManager(std::vector<InputListener*> chain)
    : listeners(std::move(chain)), seenMasks(listeners.size(), 0) {}

bool InputManager::onRawEvent(const RawEvent& event) {
    // The last release ended the gesture: the next raw event starts with no owner
    if (heldMask == 0) {
        owner             = kNoOwner;
        discardingGesture = false;
    }
    if (discardingGesture || discardingPoll) {
        // Only the held buttons change. A reset ends the gesture as usual.
        if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
            heldMask |= getButtonBit(typedEvent->button);
        }
        if (const auto* typedEvent = std::get_if<event::ButtonReleased>(&event.kind)) {
            heldMask &= ~getButtonBit(typedEvent->button);
        }
        if (std::get_if<event::ButtonReset>(&event.kind)) {
            discardingGesture = false;
            onReset(event);
        }
        return false;
    }

    if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
        onButtonPressed(event, typedEvent->button);
    }
    if (const auto* typedEvent = std::get_if<event::ButtonReleased>(&event.kind)) {
        onButtonReleased(event, typedEvent->button);
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
    if (discardingGesture || discardingPoll) return false;
    if (owner != kNoOwner) return listeners[owner]->onButtonEvent(event);
    for (size_t index = 0; index < listeners.size(); index++) {
        if (listeners[index]->onButtonEvent(event)) {
            setOwner(index, event.time);
            return true;
        }
    }
    return false;
}

void InputManager::onButtonPressed(const RawEvent& event, Button button) {
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

void InputManager::onButtonReleased(const RawEvent& event, Button button) {
    const uint32_t bit = getButtonBit(button);
    heldMask &= ~bit;
    for (size_t index = 0; index < listeners.size(); index++) {
        if (!(seenMasks[index] & bit)) continue;
        seenMasks[index] &= ~bit;
        if (listeners[index]->onRawEvent(event) && owner == kNoOwner) {
            // The later listeners get a cancel in place of the released event
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

void InputManager::discardGesture(uint32_t time) {
    discardingPoll    = true;
    discardingGesture = heldMask != 0;
    owner             = kNoOwner;
    for (size_t index = 0; index < listeners.size(); index++) cancelSeen(index, time);
}

void InputManager::beginPoll() {
    discardingPoll = false;
}

void InputManager::setChain(std::vector<InputListener*> chain) {
    listeners = std::move(chain);
    seenMasks.assign(listeners.size(), 0);
    owner = kNoOwner;
    if (heldMask != 0) discardingGesture = true;
}

void InputManager::cancelSeen(size_t index, uint32_t time) {
    const uint32_t seen = seenMasks[index];
    seenMasks[index]    = 0;
    for (int buttonIndex = 0; buttonIndex < kButtonCount; buttonIndex++) {
        const Button button = static_cast<Button>(buttonIndex);
        if (seen & getButtonBit(button)) {
            listeners[index]->onRawEvent({time, event::ButtonCancel{button}});
        }
    }
}

void InputManager::setOwner(size_t index, uint32_t time) {
    owner = static_cast<int>(index);
    for (size_t other = 0; other < listeners.size(); other++) {
        if (other != index) cancelSeen(other, time);
    }
}

}  // namespace input
