#ifndef __UI_VIEW_MULTILINE_TEXT_VIEW_HPP__
#define __UI_VIEW_MULTILINE_TEXT_VIEW_HPP__

#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "ui/view/spanned_text.hpp"
#include "ui/view/ui_view.hpp"

/**
 * A text on one or more lines, as wide as the space that the parent gives. A line breaks at a
 * space, and a word that is wider than a line breaks at a character. A line break in the text
 * starts a new line. The view is as high as the lines that it shows.
 */
class MultilineTextView : public UiView {
public:
    /** Where a line can break */
    enum class Wrap {
        Words,       // at a space, and inside a word only when the word is wider than a line
        Characters,  // after any character, thus each line is as full as the width allows
    };

    explicit MultilineTextView(TTF_Font* font);

    /** Returns: true when the wrap changed */
    bool setWrap(Wrap newWrap);
    ~MultilineTextView() override;

    /** Shows text in the primary role. Returns: true when the text changed */
    bool setText(const std::string& text);
    /** Returns: true when the text changed */
    bool setText(const SpannedText& text);

    /** Pass the height of a line in pixels, or 0 for the line spacing of the font. */
    void setLineHeight(int height);

    /** Shows the first `count` lines only. Pass 0 to show each line. */
    void setMaxLines(int count);

    /**
     * Pass true in `cutToSpace` to show only the lines that fit in the height that the parent
     * gives. The default is each line, also below that height.
     */
    void setCutToSpace(bool cutToSpace);

    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

protected:
    void onAttach(UiContext& context) override;

private:
    /** A part of one line in one role, rendered in white */
    struct Run {
        SDL_Surface* bitmap;
        ThemeRole role;
        int x;
        int line;
    };

    void wrap(int width);
    void freeRuns();
    int getLineHeight() const;

    TTF_Font* font;
    Wrap wrapMode = Wrap::Words;
    SpannedText text;
    std::vector<Run> runs;
    int lineCount       = 0;
    int shownLineCount  = 0;
    int fixedLineHeight = 0;
    int maxLines        = 0;
    bool cutToSpace     = false;
    int wrappedWidth    = -1;
    bool wrapStale   = true;
    bool dirty       = true;
};

#endif
