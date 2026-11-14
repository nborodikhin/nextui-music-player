#ifndef __UI_VIEW_UI_LAYOUT_CONTEXT_HPP__
#define __UI_VIEW_UI_LAYOUT_CONTEXT_HPP__

#include <list>

#include <SDL2/SDL.h>

#include "ui/view/ui_size.hpp"
#include "ui/view/ui_target_layer.hpp"

class UiView;

/**
 * The context used during view layout to build view tree, measure view,
 * report layout-based visibility and clip rects.
 * The order of `layoutChild` defines child z-order.
 */
class UiLayoutContext {
public:
    /** Returns a root context at the origin of the screen, with the screen as the clip. */
    static UiLayoutContext makeRoot(UiSize screenSize, bool surfaceOnly);

    /**
     * Lays out childView in rect, in the coordinates of this view. The child is clipped to
     * the rectangle of this view. The returned context stays valid until the layout() of this
     * view returns.
     */
    UiLayoutContext& layoutChild(UiView& childView, const SDL_Rect& rect);

    /** Same, with the child also clipped to clip, in the coordinates of this view. */
    UiLayoutContext& layoutChild(UiView& childView, const SDL_Rect& rect, const SDL_Rect& clip);

    /** Returns a context in the place of this one whose layout adds no entries. */
    UiLayoutContext createMeasuringContext() const;

    UiSize getMaxSize() const { return maxSize; }
    /** Returns the max size as a rectangle in the coordinates of this view. */
    SDL_Rect getMaxRect() const { return SDL_Rect{0, 0, maxSize.w, maxSize.h}; }

    /** Sets the size of the view and of its bitmap. The default is the max size. */
    void setSize(UiSize newSize) { size = newSize; }
    UiSize getSize() const { return size; }

    /** The default is the target layer of the parent. In surface-only mode it is the surface. */
    void setTargetLayer(UiTargetLayer target);
    UiTargetLayer getTargetLayer() const { return targetLayer; }

    /** Hides the view and its children in this frame. The parent decides it. */
    void setVisible(bool visible) { layoutVisible = visible; }
    bool isVisible() const { return layoutVisible; }

    SDL_Point viewToScreen(SDL_Point point) const;
    SDL_Point screenToView(SDL_Point point) const;

    /** Returns the view of this entry, or null at the root. */
    UiView* getView() const { return view; }

    /** Returns the rectangle of the view on the screen. */
    SDL_Rect getScreenRect() const;

    /** Returns the clip of the view on the screen. */
    SDL_Rect getClipRect() const { return clipRect; }

    const std::list<UiLayoutContext>& getChildren() const { return children; }

private:
    UiLayoutContext& layoutChildIn(UiView& childView, const SDL_Rect& rect, const SDL_Rect* clip);

    UiView* view              = nullptr;
    SDL_Point origin          = {0, 0};
    UiSize maxSize            = {0, 0};
    UiSize size               = {0, 0};
    SDL_Rect clipRect         = {0, 0, 0, 0};
    UiTargetLayer targetLayer = UiTargetLayer::MainSurface;
    bool layoutVisible        = true;
    bool surfaceOnly          = false;
    std::list<UiLayoutContext> children;
};

#endif
