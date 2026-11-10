#ifndef __UI_VIEW_BOX_VIEW_HPP__
#define __UI_VIEW_BOX_VIEW_HPP__

#include "ui/view/ui_view.hpp"

/**
 * A rectangle filled with the color.
 */
class BoxView : public UiView {
public:
    explicit BoxView(SDL_Color color = SDL_Color{0, 0, 0, 0}) : color(color) {}

    void setColor(SDL_Color newColor);
    void setTargetLayer(UiTargetLayer newTarget) { target = newTarget; }

    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    SDL_Color color;
    UiTargetLayer target = UiTargetLayer::MainSurface;
    bool dirty           = true;
};

#endif
