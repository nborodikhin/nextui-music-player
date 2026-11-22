#ifndef __UI_VIEW_TITLE_TEXT_VIEW_HPP__
#define __UI_VIEW_TITLE_TEXT_VIEW_HPP__

#include <array>
#include <string>

#include "screen_title.h"
#include "ui/view/text_view.hpp"

/** The title of a screen. A title that does not fit fades out at the cut in place of the ellipsis. */
class TitleTextView : public TextView {
public:
    explicit TitleTextView(TTF_Font* font) : TextView(font, false) {}
    ~TitleTextView() override;

    /**
     * Pass true in `follow` to let a new text follow the old one. In that mode, a title that does
     * not fit moves by itself after a start delay. A new text while the title moves does not
     * replace what the title shows: the text under the right edge washes out one em past the edge,
     * and the new text scrolls in after it. A title at rest takes a new text at once.
     */
    void setFollowText(bool follow);

    bool setText(const std::string& text) override;

    using TextView::startScrolling;
    /** A title in the follow mode moves by itself, thus a start does nothing. */
    void startScrolling(int32_t spPerSecond) override;

    /** Returns the texts that the title shows and will show, in the follow mode. */
    const ScreenTitle& getFollowTape() const { return tape; }

    void layout(UiLayoutContext& context) override;
    bool isDirty() const override;
    void draw(UiDrawContext& context) override;

protected:
    void onAttach(UiContext& context) override;
    void finishDraw(SDL_Surface* bitmap) override;

private:
    /** The bitmap of a text of the tape, with the wash of a cut text in it */
    struct SegmentBitmap {
        std::string text;
        int len              = 0;
        bool cut             = false;
        SDL_Color color      = {};
        SDL_Surface* surface = nullptr;
    };

    /** Puts the current text on the tape again, at rest, at the active time. */
    void restartTape();
    void stepFollow();
    SDL_Surface* getSegmentBitmap(int slot);
    void freeSegments();
    uint32_t getNow() const;

    bool follow = false;
    ScreenTitle tape{};
    std::array<SegmentBitmap, SCREEN_TITLE_SEGMENTS> segments;
    std::string followText;
    bool followDirty = true;
    bool moving      = false;
    int followOffset = 0;
    int followCount  = 0;
    TaskId followTask = 0;
};

#endif
