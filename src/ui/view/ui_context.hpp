#ifndef __UI_VIEW_UI_CONTEXT_HPP__
#define __UI_VIEW_UI_CONTEXT_HPP__

#include <SDL2/SDL.h>

#include "ui/view/ui_size.hpp"

/** What a view needs from its screen. */
class UiContext {
public:
    virtual SDL_PixelFormat* getPixelFormat() const = 0;
    virtual UiSize getScreenSize() const = 0;

protected:
    ~UiContext() = default;
};

#endif
