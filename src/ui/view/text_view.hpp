#ifndef __UI_VIEW_TEXT_VIEW_HPP__
#define __UI_VIEW_TEXT_VIEW_HPP__

#include <cstdint>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "ui/view/ui_view.hpp"

/**
 * One line of text. A text that does not fit ends in "..." when the view uses the ellipsis.
 * Without it, the text is cut at the edge at rest, and scrolls on the animation layer.
 * The view is as high as the line spacing of the font, and less where its parent gives less
 * height, but not less than the height of the font. The text is in the middle of that height.
 */
class TextView : public UiView {
public:
    /** Pass true in useEllipsis to end a text that does not fit in "...". Such a view keeps one width. */
    TextView(TTF_Font* font, bool useEllipsis);
    ~TextView() override;

    void setText(const char* text);
    void setColor(SDL_Color color);

    /** Returns true when the text is wider than the space of the last layout. */
    bool isClipped() const;

    /**
     * Moves the text to its place at elapsedMs after it started to scroll. Pass 0 to put the
     * text at rest at its start. A text that fits ignores the call.
     */
    void updateScrolling(uint32_t elapsedMs);

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

protected:
    /** Changes the pixels of the drawn text in the bitmap. The base does nothing. */
    virtual void finishDraw(SDL_Surface*) {}

    bool isScrolling() const { return scrolling; }
    int getFontHeight() const;

private:
    void render();
    void freeRender();

    TTF_Font* font;
    bool useEllipsis;
    std::string text;
    // A color mod that changes nothing, until the owner sets the color of a role
    //noinspection HardcodedColor
    SDL_Color color      = {255, 255, 255, 255};
    SDL_Surface* bitmap  = nullptr;  // the text in white
    bool renderStale     = true;
    int renderedMaxWidth = -1;
    int textWidth        = 0;
    int maxWidth         = 0;
    // The top of the text in the view
    int textTop          = 0;
    int offset           = 0;
    bool scrolling       = false;
    bool dirty           = true;
};

#endif
