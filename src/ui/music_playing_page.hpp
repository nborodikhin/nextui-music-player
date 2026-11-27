#ifndef __UI_MUSIC_PLAYING_PAGE_HPP__
#define __UI_MUSIC_PLAYING_PAGE_HPP__

#include <memory>

#include "ui/page/ui_page.hpp"

/** Runs the playing screen of the music for the queue that the music session plays. */
class MusicPlayingPage final : public UiPage {
public:
    struct Result {
        /** The screen closed because the queue ended */
        bool queueEnded = false;
    };

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
