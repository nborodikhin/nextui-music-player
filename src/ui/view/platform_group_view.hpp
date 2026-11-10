#ifndef __UI_VIEW_PLATFORM_GROUP_VIEW_HPP__
#define __UI_VIEW_PLATFORM_GROUP_VIEW_HPP__

#include <cstdint>
#include <vector>

#include <SDL2/SDL.h>

#include "ui/view/ui_view.hpp"

/**
 * Helper for NextUI platform group (buttons or status).
 * Handles management of the screen-size scratch buffer.
 */
class PlatformGroupView : public UiView {
public:
    ~PlatformGroupView() override;

    void setContext(UiContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

protected:
    /**
     * Returns the scratch surface, transparent, or null where it cannot exist. Only the part of
     * the last render is cleared, thus a render draws inside the rectangle that it gives to
     * finishRender().
     */
    SDL_Surface* clearScratch();

    /**
     * Sets the part of the scratch surface that the view copies. Makes the view dirty only when
     * the pixels differ from the last render.
     */
    void finishRender(const SDL_Rect& rect);

    /** Returns the part of the scratch surface that the view copies. */
    const SDL_Rect& getScratchRect() const { return scratchRect; }

    bool dirty       = true;
    bool needsRender = true;

private:
    SDL_Surface* scratch = nullptr;
    SDL_Rect scratchRect = {0, 0, 0, 0};
    std::vector<uint32_t> renderedPixels;
};

#endif
