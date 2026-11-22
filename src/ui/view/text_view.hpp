#ifndef __UI_VIEW_TEXT_VIEW_HPP__
#define __UI_VIEW_TEXT_VIEW_HPP__

#include <cstdint>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "ui/view/ui_task_scheduler.hpp"
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

    /** Returns: true when the text changed */
    virtual bool setText(const std::string& text);
    void setColor(SDL_Color color);

    /** Returns true when the text is wider than the space of the last layout. */
    bool isClipped() const;

    /**
     * Moves the text at `spPerSecond` from where it is. A value of 0 or less stops the text where it
     * is. A text that fits does not move. A view without a context does not move. A view that
     * leaves the layout stops, and moves again at the next start.
     */
    void startScrolling(int32_t spPerSecond);

    /** Moves the text at the speed of each scrolling text of the app. */
    void startScrolling();

    /** Stops the text where it is. */
    void stopScrolling();

    /** Puts the text at its start. Keeps the speed. */
    void resetScrolling();

    /** Returns how far the text moved from its start, in pixels. */
    int getScrollOffset() const { return offset; }


    void layout(UiLayoutContext& context) override;
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

protected:
    void onAttach(UiContext& context) override;

    /** Changes the pixels of the drawn text in the bitmap. The base does nothing. */
    virtual void finishDraw(SDL_Surface*) {}

    bool isScrolling() const { return scrolling; }
    int getFontHeight() const;
    TTF_Font* getFont() const { return font; }
    SDL_Color getColor() const { return color; }
    /** Returns the top of the text in the view, from the last layout. */
    int getTextTop() const { return textTop; }
    /** Marks the view drawn, for a subclass that draws its text in a way of its own. */
    void clearDirty() { dirty = false; }
    /** Returns `text` in the font and the color of the view, premultiplied. The caller frees it. */
    SDL_Surface* renderBitmap(const std::string& text) const;

private:
    void render();
    void freeRender();
    /** Sets the scrolling flag from the speed and the offset. */
    void updateScrolling();
    void stepScroll();
    void setOffset(double newOffset);

    TTF_Font* font;
    bool useEllipsis;
    std::string text;
    // White, until the owner sets the color of a role
    //noinspection HardcodedColor
    SDL_Color color      = {255, 255, 255, 255};
    /** The text in its color, premultiplied */
    SDL_Surface* bitmap  = nullptr;
    bool renderStale     = true;
    int renderedMaxWidth = -1;
    int textWidth        = 0;
    int maxWidth         = 0;
    // The top of the text in the view
    int textTop          = 0;
    int offset           = 0;
    bool scrolling       = false;
    bool dirty           = true;
    int32_t speedSpPerSecond = 0;
    /** A start of the scroll came before the attach */
    bool startPending = false;
    /** The offset with its fraction */
    double exactOffset = 0;
    /** The active time of the last step of the scroll */
    uint32_t stepTime  = 0;
    /** The frame callback of the scroll */
    TaskId scrollTask = 0;
};

#endif
