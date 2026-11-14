#ifndef __UI_VIEW_DIALOG_VIEW_HPP__
#define __UI_VIEW_DIALOG_VIEW_HPP__

#include <string>

#include "ui/view/text_view.hpp"
#include "ui/view/ui_view.hpp"

/** The ground and the border of a dialog. */
class DialogFrameView : public UiView {
public:
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;
    /** Draws the frame again with the colors of the theme. */
    void invalidate() { dirty = true; }

private:
    bool dirty = true;
};

/**
 * A dialog in the middle of the space that the parent gives: a surface with a title, an optional
 * content view, and a hint line at the bottom. It draws nothing of its own.
 */
class DialogView : public UiView {
public:
    enum class Alignment : uint8_t { Center, Left };

    /** Pass the width of the surface in width, in pixels. */
    DialogView(int width, Alignment alignment, TTF_Font* titleFont, TTF_Font* hintFont);

    void setTitle(const std::string& text) { title.setText(text); }
    void setHint(const std::string& text) { hint.setText(text); }

    /** Shows content between the title and the hint. Pass the height of content in height. */
    void setContent(UiView* content, int height);

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;

private:
    /** Lays out text in the row at y, at the left edge or in the middle of the surface. */
    void layoutText(UiLayoutContext& context, UiLayoutContext& measuring, TextView& text, const SDL_Rect& surface,
                    int y);

    int width;
    Alignment alignment;
    DialogFrameView frame;
    TextView title;
    TextView hint;
    UiView* content   = nullptr;
    int contentHeight = 0;
};

#endif
