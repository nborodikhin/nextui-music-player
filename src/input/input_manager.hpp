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

    /**
     * Gives the rest of this poll and the rest of the gesture to no listener, also to the owner.
     * Each listener gets a cancel of each held button whose pressed event it saw. The gesture part
     * ends when no button is held. The poll part ends at beginPoll().
     */
    void discardGesture(uint32_t time);

    /** Ends the poll part of discardGesture(). Call it before each poll. */
    void beginPoll();

    /**
     * Replaces the listeners. When a button is held, the rest of the gesture goes to no listener,
     * thus the new chain gets no event of a gesture that started before it.
     */
    void setChain(std::vector<InputListener*> chain);

private:
    static constexpr int kNoOwner = -1;

    void onButtonPressed(const RawEvent& event, Button button);
    void onButtonReleased(const RawEvent& event, Button button);
    void onMedia(const RawEvent& event);
    void onReset(const RawEvent& event);
    /**
     * Gives a ButtonCancel to each other listener for each held button whose pressed event it saw
     */
    void setOwner(size_t index, uint32_t time);
    /** Gives the listener a ButtonCancel for each held button whose pressed event it saw */
    void cancelSeen(size_t index, uint32_t time);

    std::vector<InputListener*> listeners;
    /** For each listener: the held buttons whose pressed event it saw */
    std::vector<uint32_t> seenMasks;
    uint32_t heldMask = 0;
    /**
     * The gesture ends at the first raw event after the last release, thus the button events of
     * the last release still go to the owner.
     */
    int owner = kNoOwner;
    /** The rest of the gesture goes to no listener */
    bool discardingGesture = false;
    /** The rest of the poll goes to no listener */
    bool discardingPoll = false;
};

}  // namespace input

#endif
