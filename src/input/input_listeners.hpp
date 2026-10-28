#ifndef __INPUT_INPUT_LISTENERS_HPP__
#define __INPUT_INPUT_LISTENERS_HPP__

#include "input_events.hpp"

namespace input {

/**
 * A gesture is the input events from a down while no button is held, until no button is held.
 * A listener that consumes an event of the gesture is its owner: it gets each later event of the
 * gesture, and no other listener gets them. Each other listener gets a ButtonCancel for each
 * held button whose down it saw.
 */

class RawEventListener {
public:
    /**
     * Returns: true when the listener consumes the event. Ignored for ButtonCancel and
     * ButtonReset.
     */
    virtual bool onRawEvent(const RawEvent& event) = 0;

protected:
    ~RawEventListener() = default;
};

class ButtonListener {
public:
    /** Returns: true when the listener consumes the event */
    virtual bool onButtonEvent(const ButtonEvent& event) = 0;

protected:
    ~ButtonListener() = default;
};

/** A listener of the two streams, such as a page. */
class InputListener : public RawEventListener, public ButtonListener {
protected:
    ~InputListener() = default;
};

}  // namespace input

#endif
