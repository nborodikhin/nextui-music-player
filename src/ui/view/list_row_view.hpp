#ifndef __UI_VIEW_LIST_ROW_VIEW_HPP__
#define __UI_VIEW_LIST_ROW_VIEW_HPP__

#include <cstdint>
#include <string>

#include <SDL2/SDL_ttf.h>

#include "ui/view/text_view.hpp"
#include "ui/view/ui_view.hpp"

/**
 * A row of a list: a selection pill when it is selected, and its text. The row shows its whole
 * text, which can scroll, when it is selected and the text does not fit. Else the text ends in
 * "...".
 */
class ListRowView : public UiView {
public:
    explicit ListRowView(TTF_Font* font);

    /** Returns: true when the text changed */
    bool setText(const std::string& text);
    void setSelected(bool selected);
    bool isSelected() const { return selected; }

    /**
     * Moves the whole text at the scrolling speed of the app, where the row shows it. The text
     * moves again each time the row shows it, until stopScrolling().
     */
    void startScrolling() {
        scrollRequested = true;
        longText.startScrolling();
    }
    void stopScrolling() {
        scrollRequested = false;
        longText.stopScrolling();
    }
    void resetScrolling() { longText.resetScrolling(); }

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    TextView shortText;
    TextView longText;
    bool selected  = false;
    bool dirty     = true;
    /** The owner started the scroll and did not stop it */
    bool scrollRequested = false;
    /** The last layout showed the whole text */
    bool longTextShown = false;
    int pillWidth  = 0;
    int pillHeight = 0;
};

#endif
