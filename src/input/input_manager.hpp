#ifndef __INPUT_INPUT_MANAGER_HPP__
#define __INPUT_INPUT_MANAGER_HPP__

#include <cstddef>
#include <cstdint>
#include <vector>

#include "input_listeners.hpp"

namespace input {

/**
 * Gives the events to a chain of listeners, and tracks the owner of the button gesture.
 * With no owner, an event goes along the chain until a listener consumes it. That listener
 * becomes the owner of the gesture.
 * With an owner, an event goes to the owner only.
 * An event that is not part of a gesture, such as ButtonReset, goes to all listeners.
 */
class InputManager : public RawEventListener, public ButtonListener {
public:
    explicit InputManager(std::vector<InputListener*> chain);

    /** Returns: false */
    bool onRawEvent(const RawEvent& event) override;

    /** Returns: true when a listener consumed the event */
    bool onButtonEvent(const ButtonEvent& event) override;

private:
    static constexpr int kNoOwner = -1;

    void onButtonDown(const RawEvent& event, Button button);
    void onButtonUp(const RawEvent& event, Button button);
    void onMedia(const RawEvent& event);
    void onReset(const RawEvent& event);
    /** Gives a ButtonCancel to each other listener for each held button whose down it saw */
    void setOwner(size_t index, uint32_t time);

    std::vector<InputListener*> listeners;
    /** For each listener: the held buttons whose down it saw */
    std::vector<uint32_t> seenMasks;
    uint32_t heldMask = 0;
    /**
     * The gesture ends at the first raw event after the last up, thus the button events of the
     * last up still go to the owner.
     */
    int owner = kNoOwner;
};

}  // namespace input

#endif
