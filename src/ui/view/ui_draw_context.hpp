#ifndef __UI_VIEW_UI_DRAW_CONTEXT_HPP__
#define __UI_VIEW_UI_DRAW_CONTEXT_HPP__

#include <SDL2/SDL.h>

/**
 * Provides access to the view's output bitmap (view-sized, transparent) during
 * `view.draw()` call. This bitmap is later used to compose the final screen.
 *
 * Content rect is defined as:
 * - empty, if surface was not accessed
 * - intersection of whole view and explicit rect set by setContentRect()
 * - whole view, if getSurface() was used at least once during draw
 */
class UiDrawContext {
public:
    /**
     * Returns the bitmap of the view, in the size of the view and transparent. It holds
     * premultiplied alpha. Blit a source with straight alpha with SDL_BLENDMODE_BLEND, and copy
     * a premultiplied source with SDL_BLENDMODE_NONE. Returns null when the bitmap cannot exist.
     */
    virtual SDL_Surface* getSurface() = 0;

    /**
     * Sets the part of the bitmap that holds content, in the coordinates of the view. The
     * default is the whole bitmap.
     */
    virtual void setContentRect(const SDL_Rect& rect) = 0;

protected:
    ~UiDrawContext() = default;
};

#endif
