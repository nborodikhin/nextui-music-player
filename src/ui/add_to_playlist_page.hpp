#ifndef __UI_ADD_TO_PLAYLIST_PAGE_HPP__
#define __UI_ADD_TO_PLAYLIST_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/** Runs the Add to Playlist dialog for one indexed file, or for the audio files of a folder. */
class AddToPlaylistPage final : public UiPage {
public:
    using Result = std::monostate;

    /** Pass true in `isFolder` when `id` is the id of a folder, not of a file. */
    AddToPlaylistPage(int id, bool isFolder) : id(id), isFolder(isFolder) {}

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

private:
    int id;
    bool isFolder;
};

#endif
