#ifndef __UI_RADIO_PLAYING_PAGE_HPP__
#define __UI_RADIO_PLAYING_PAGE_HPP__

#include <memory>
#include <string>

#include "ui/page/ui_page.hpp"

/**
 * Runs the playing screen of the radio. Starts the station of `url`, or shows the station that
 * plays in the background when `url` is empty.
 */
class RadioPlayingPage final : public UiPage {
public:
    /** The URL of the station that the screen showed last, or an empty text */
    using Result = std::string;

    explicit RadioPlayingPage(std::string url = {}) : url(std::move(url)) {}

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

private:
    std::string url;
};

#endif
