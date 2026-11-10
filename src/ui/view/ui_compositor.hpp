#ifndef __UI_VIEW_UI_COMPOSITOR_HPP__
#define __UI_VIEW_UI_COMPOSITOR_HPP__

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

#include <SDL2/SDL.h>

#include "ui/view/ui_view.hpp"

/**
 * Keeps a bitmap for each view that draws, and composes the parts of the screen that changed
 * since the last frame from those bitmaps. Each target gets the alpha form that the platform
 * blends it with.
 */
class UiCompositor : public UiContext {
public:
    UiCompositor() = default;
    ~UiCompositor();

    UiCompositor(const UiCompositor&)            = delete;
    UiCompositor& operator=(const UiCompositor&) = delete;

    /** Sets the regular surface. Drops the bitmaps, and composes everything on the next frame. */
    void setScreen(SDL_Surface* screen);

    SDL_PixelFormat* getPixelFormat() const override;
    UiSize getScreenSize() const override;

    /** Returns the root context of a new frame. It stays valid until the next call. */
    UiLayoutContext& beginLayout();

    /** Draws the views that changed, and composes the damage of the frame. */
    void drawFrame();

    /**
     * Composes the whole screen on the next frame, and clears each layer that the compositor
     * wrote. Call it when the screen no longer holds the last frame.
     */
    void invalidateAll();

    /** Clears each layer that the compositor wrote since the last clear. */
    void clearUsedLayers();

    /**
     * Blends `color`, an ARGB value with straight alpha, over the whole regular surface, and
     * composes everything on the next frame.
     */
    void dimSurface(uint32_t color);

    /** Pass true in surfaceOnly to put each entry on the regular surface. */
    void setSurfaceOnly(bool surfaceOnly);
    bool isSurfaceOnly() const { return surfaceOnly; }

    /** Returns true when the last drawFrame() wrote the regular surface. */
    bool hasSurfaceChanged() const { return surfaceChanged; }

    /** Returns true when the last drawFrame() wrote a layer. */
    bool hasLayersChanged() const { return layersChanged; }

private:
    struct Entry {
        UiView* view;
        int viewId;
        UiTargetLayer target;
        SDL_Rect screenRect;
        SDL_Rect clipRect;
        // The content of the bitmap on the screen, cut by the clip. Known after the draw.
        SDL_Rect visibleRect;
        bool shown;
        bool needsDraw;
    };

    struct ShownEntry {
        UiTargetLayer target;
        SDL_Rect screenRect;
        SDL_Rect visibleRect;
    };

    /** The result of the last draw of a view. A view with no content has no surface. */
    struct Bitmap {
        SDL_Surface* surface = nullptr;
        SDL_Rect contentRect = {0, 0, 0, 0};
        UiSize drawnSize     = {0, 0};
        bool drawn           = false;
    };

    void collectEntries(const UiLayoutContext& context, bool parentShown);
    void addDamage(UiTargetLayer target, const SDL_Rect& rect);
    void drawViews();
    void addEntryDamage();
    void compose(UiTargetLayer target, const SDL_Rect& region);
    void dropBitmaps();

    SDL_Surface* screen = nullptr;
    std::optional<UiLayoutContext> root;
    std::vector<Entry> entries;
    std::unordered_map<int, ShownEntry> lastShown;
    std::unordered_map<int, Bitmap> bitmaps;
    std::array<std::vector<SDL_Rect>, kUiTargetLayerCount> damageByTarget;
    // The layers that hold a composition since the last clear
    std::array<bool, kUiTargetLayerCount> usedTargets = {};
    bool composeAll         = true;
    bool clearLayersPending = false;
    bool surfaceOnly        = false;
    bool surfaceChanged     = false;
    bool layersChanged      = false;
};

#endif
