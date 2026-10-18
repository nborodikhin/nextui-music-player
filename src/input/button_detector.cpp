#include "button_detector.hpp"

#include <cassert>

namespace input {

ButtonDetector::ButtonDetector(ButtonListener& listener, std::span<const ButtonRequest> requests)
    : listener(listener) {
    for (const ButtonRequest& request : requests) {
        std::visit(Overloaded{
                       [this](const request::LongPress& r) { longPressMask |= getButtonBit(r.button); },
                       [this](const request::Repeat& r) { repeatMask |= getButtonBit(r.button); },
                       [this](const request::Combo& r) {
                           assert(comboCount < kMaxCombos);
                           if (comboCount == kMaxCombos) return;
                           combos[comboCount++] = r;
                           comboMask |= getButtonBit(r.first) | getButtonBit(r.second);
                       },
                   },
                   request);
    }
    assert((repeatMask & longPressMask) == 0);
    assert((repeatMask & comboMask) == 0);
}

void ButtonDetector::onRawEvent(const RawEvent& event) {
    std::visit(Overloaded{
                   [&](const event::ButtonDown& e) { onButtonDown(e.button, event.time); },
                   [&](const event::ButtonUp& e) { onButtonUp(e.button, event.time); },
                   [this](const event::ButtonReset&) { reset(); },
                   [](const event::Media&) {},
               },
               event.kind);
}

void ButtonDetector::onTick(uint32_t now) {
    for (int index = 0; index < kButtonCount; index++) {
        sendDueEvent(static_cast<Button>(index), now);
    }
}

void ButtonDetector::sendDueEvent(Button button, uint32_t now) {
    ButtonState& state = getState(button);
    if (state.mode == Mode::Deferred && (longPressMask & getButtonBit(button)) &&
        now - state.downTime >= kLongPressMs) {
        state.mode = Mode::Done;
        send(now, event::LongPress{button});
    } else if (state.mode == Mode::Repeat && static_cast<int32_t>(now - state.nextRepeat) >= 0) {
        // One repeat for each tick on the schedule of the pad, thus a late tick catches
        // up one interval at a time
        state.nextRepeat += kRepeatIntervalMs;
        send(now, event::Repeat{button});
    }
}

void ButtonDetector::onButtonDown(Button button, uint32_t time) {
    const uint32_t bit = getButtonBit(button);
    ButtonState& state = getState(button);
    heldMask |= bit;
    state.downTime = time;

    if (getBlockedMask() & bit) {
        state.mode = Mode::Done;
        return;
    }
    if (startCombo(button, time)) {
        return;
    }
    if (repeatMask & bit) {
        state.mode       = Mode::Repeat;
        state.nextRepeat = time + kRepeatDelayMs;
        send(time, event::ShortPress{button});
    } else if ((longPressMask | comboMask) & bit) {
        state.mode = Mode::Deferred;
    } else {
        state.mode = Mode::Immediate;
        send(time, event::ShortPress{button});
    }
}

void ButtonDetector::onButtonUp(Button button, uint32_t time) {
    // The release comes before the tick of its frame. A long press or a repeat that is due
    // at the release goes out first, as the pad gives a repeat before a release.
    sendDueEvent(button, time);

    const uint32_t bit = getButtonBit(button);
    ButtonState& state = getState(button);
    const Mode mode = state.mode;
    state.mode = Mode::Idle;
    heldMask &= ~bit;
    for (int index = 0; index < comboCount; index++) {
        const request::Combo& combo = combos[index];
        if (!(heldMask & (getButtonBit(combo.first) | getButtonBit(combo.second)))) {
            comboLatched[index] = false;
        }
    }

    if (mode == Mode::Deferred) {
        send(time, event::ShortPress{button});
    }
}

// Ends each held button with no event, as the user did not release it
void ButtonDetector::reset() {
    heldMask = 0;
    states.fill({});
    comboLatched.fill(false);
}

// Gives the combo when its second member goes down while the first is held and has given no
// event yet. The pad gives no order for two downs in one frame, thus they start the combo in
// each order. Returns true when a combo went out.
bool ButtonDetector::startCombo(Button button, uint32_t time) {
    for (int index = 0; index < comboCount; index++) {
        const request::Combo& combo = combos[index];
        Button partner;
        bool sameFrameOnly;
        if (combo.second == button) {
            partner       = combo.first;
            sameFrameOnly = false;
        } else if (combo.first == button) {
            partner       = combo.second;
            sameFrameOnly = true;
        } else {
            continue;
        }
        ButtonState& partnerState = getState(partner);
        if (partnerState.mode != Mode::Deferred) continue;
        if (sameFrameOnly && partnerState.downTime != time) continue;

        partnerState.mode     = Mode::Done;
        getState(button).mode = Mode::Done;
        comboLatched[index] = true;
        send(time, event::Combo{combo.first, combo.second});
        return true;
    }
    return false;
}

uint32_t ButtonDetector::getBlockedMask() const {
    uint32_t mask = 0;
    for (int index = 0; index < comboCount; index++) {
        if (comboLatched[index]) {
            mask |= getButtonBit(combos[index].first) | getButtonBit(combos[index].second);
        }
    }
    return mask;
}

void ButtonDetector::send(uint32_t time, decltype(ButtonEvent::kind) kind) {
    listener.onButtonEvent(ButtonEvent{time, kind});
}

ButtonDetector::ButtonState& ButtonDetector::getState(Button button) {
    return states[static_cast<int>(button)];
}

}  // namespace input
