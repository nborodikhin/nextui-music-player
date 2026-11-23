#ifndef __UI_AUDIOBOOK_PLAYING_PAGE_HPP__
#define __UI_AUDIOBOOK_PLAYING_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/**
 * The playing screen of the audiobook session: the cover, the chapter, the progress and the sleep
 * timer. B leaves the book playing in the background, or stops it where it is paused. The page
 * closes when the session ends.
 */
class AudiobookPlayingPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
