#ifndef __UI_VIEW_ICON_VIEW_HPP__
#define __UI_VIEW_ICON_VIEW_HPP__

#include <SDL2/SDL.h>

#include "ui_icons.h"
#include "ui/view/ui_view.hpp"

/** An icon of the app in one color, scaled to the size that the parent gives. */
class IconView : public UiView {
public:
    explicit IconView(IconId icon) : icon(icon) {}

    void setIcon(IconId newIcon);
    void setColor(SDL_Color newColor);

    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

protected:
    void onAttach(UiContext& context) override;

private:
    IconId icon;
    // White, until the owner sets the color of a role
    //noinspection HardcodedColor
    SDL_Color color = {255, 255, 255, 255};
    bool dirty      = true;
};

#endif
