#ifndef __UI_VIEW_LIST_ROW_VIEW_HPP__
#define __UI_VIEW_LIST_ROW_VIEW_HPP__

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <SDL2/SDL_ttf.h>

#include "ui/view/text_view.hpp"
#include "ui/view/list_item_view.hpp"

/**
 * A row of a list: a selection pill when it is selected, and its text. The row shows its whole
 * text, which can scroll, when it is selected and the text does not fit. Else the text ends in
 * "...".
 *
 * A row can have a right view at its right edge, and the text then takes the space to the left of
 * it. A selected row with a right view shows a band under the whole row.
 *
 * A row can have an icon at its left edge, before the text.
 */
class ListRowView : public ListItemView {
public:
    explicit ListRowView(TTF_Font* font);

    /** Returns: true when the text changed */
    bool setText(const std::string& text);
    void setSelected(bool selected) override;

    /**
     * Shows an icon before the text. The row calls `getIcon` with its selection in each draw, and
     * does not keep the surface. Pass an empty function in `getIcon` to remove the icon.
     */
    void setLeftIcon(std::function<SDL_Surface*(bool selected)> getIcon);

    /** Shows `view` at the right edge of the row. Pass nullptr in `view` to remove it. */
    void setRightView(std::shared_ptr<UiView> view);
    bool isSelected() const { return selected; }

    /** The text moves again each time the row shows it, until stopScrolling(). */
    void startScrolling() override {
        scrollRequested = true;
        longText.startScrolling();
    }
    void stopScrolling() override {
        scrollRequested = false;
        longText.stopScrolling();
    }
    void resetScrolling() override { longText.resetScrolling(); }

    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    /** The icon that the getter of the row returns for its selection */
    class IconView : public UiView {
    public:
        std::function<SDL_Surface*(bool selected)> getIcon;
        bool selected = false;
        bool dirty    = true;

        bool isDirty() const override { return dirty; }
        void draw(UiDrawContext& context) override;
    };

    IconView icon;
    TextView shortText;
    TextView longText;
    std::shared_ptr<UiView> rightView;
    bool selected  = false;
    bool dirty     = true;
    /** The owner started the scroll and did not stop it */
    bool scrollRequested = false;
    /** The last layout showed the whole text */
    bool longTextShown = false;
    int pillWidth  = 0;
    int pillHeight = 0;
    /** The width of the band, which is 0 for a row with no right view */
    int bandWidth  = 0;
};

#endif
