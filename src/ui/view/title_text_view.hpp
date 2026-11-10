#ifndef __UI_VIEW_TITLE_TEXT_VIEW_HPP__
#define __UI_VIEW_TITLE_TEXT_VIEW_HPP__

#include "ui/view/text_view.hpp"

/** The title of a screen. A title that does not fit fades out at the cut in place of the ellipsis. */
class TitleTextView : public TextView {
public:
    explicit TitleTextView(TTF_Font* font) : TextView(font, false) {}

protected:
    void finishDraw(SDL_Surface* bitmap) override;
};

#endif
