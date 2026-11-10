#ifndef __UI_VIEW_LIST_ROW_VIEW_HPP__
#define __UI_VIEW_LIST_ROW_VIEW_HPP__

#include <cstdint>

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

    void setText(const char* text);
    void setSelected(bool selected);
    bool isSelected() const { return selected; }

    /** Moves the whole text, where the row shows it. See TextView::updateScrolling(). */
    void updateScrolling(uint32_t elapsedMs);

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    TTF_Font* font;
    TextView shortText;
    TextView longText;
    bool selected  = false;
    bool dirty     = true;
    int pillWidth  = 0;
    int pillHeight = 0;
};

#endif
