#ifndef __UI_PAGE_HPP__
#define __UI_PAGE_HPP__

#include <cstdint>
#include <span>

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "help_screen.h"
#include "input/input_listeners.hpp"
#include "module_common.h"

// A screen that runPage() operates. It gets the raw events and the button events of each
// frame, and a tick. It sets its dirty flag when its state changed, and runPage() then
// calls draw().
class UiPage : public input::InputListener {
public:
    // Returns the long presses, the repeats and the combos that the page uses.
    virtual std::span<const input::ButtonRequest> getButtonRequests() const = 0;

    virtual HelpId getHelpId() const { return HELP_NONE; }

    // Called one time before the first frame, and one time after the last frame.
    virtual void onEnter() {}
    virtual void onExit() {}

    bool onRawEvent(const input::RawEvent&) override { return false; }
    bool onButtonEvent(const input::ButtonEvent&) override { return false; }

    // Called each frame after the events.
    virtual void onTick(uint32_t) {}

    // Paints the page. A page might track partially changed areas, but must
    // respect invalidateAll() calls. Pass true in useLayers where the page is the top of the
    // screen: it can then draw on the layers. Pass false where a dialog covers the page: it
    // then draws everything, with no motion, into the regular surface only.
    virtual void draw(SDL_Surface* screen, bool useLayers) = 0;

    // Asks for a draw of everything on the next frame: the screen no longer holds
    // the pixels of last draw of the page.
    void invalidateAll() {
        dirty = true;
        onInvalidateAll();
    }

    bool isDirty() const { return dirty; }
    void markClean() { dirty = false; }

    ModuleExitReason getExitReason() const { return exitReason; }

protected:
    ~UiPage() = default;

    // Asks for a draw on the next frame.
    void invalidate() { dirty = true; }

    // Called when the screen no longer holds the last draw of the page. A page that paints
    // only what changed forgets its last drawn state here.
    virtual void onInvalidateAll() {}

    // Requests the exit of the page at the end of this frame. The page still gets the
    // remaining events of the frame.
    void exitTo(ModuleExitReason reason) { exitReason = reason; }

private:
    bool dirty                  = false;
    ModuleExitReason exitReason = MODULE_EXIT_NONE;
};

// Runs the frame loop of the page until it requests an exit, and returns the reason.
ModuleExitReason runPageLoop(UiPage& page, DisplayContext* display);

#endif
