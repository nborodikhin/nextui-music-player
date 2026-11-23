#include "ui/audiobook_chapters_page.hpp"

#include <memory>
#include <string>

#include "audiobook/session.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_utils.h"

using namespace input;
using audiobook::Chapter;
using audiobook::Session;

namespace {

constexpr const char* kLeftHints[]  = {"START", "CONTROLS", nullptr};
constexpr const char* kRightHints[] = {"B", "BACK", "A", "PLAY", nullptr};

/** The rows of the chapters. The session does not change its chapters while the page shows. */
class ChapterListAdapter final : public ListAdapter {
public:
    int getCount() const override { return static_cast<int>(Session::getInstance().getChapters().size()); }

    long getItemId(int index) const override { return index; }

    std::shared_ptr<ListItemView> getView(int index) override {
        const Session& session = Session::getInstance();
        const Chapter& chapter = session.getChapters()[static_cast<size_t>(index)];
        auto row = std::make_shared<ListRowView>(Fonts_getMedium());
        // The chapter that plays has a mark, as the old list had
        row->setText((index == session.getChapterIndex() ? "> " : "") + chapter.title);
        char duration[16];
        format_time(duration, chapter.durationMs);
        auto right = std::make_shared<TextView>(Fonts_getTiny(), false);
        right->setText(duration);
        right->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        row->setRightView(std::move(right));
        return row;
    }
};

class AudiobookChaptersPageImpl final : public UiPageImpl<AudiobookChaptersPage>, private ListScreenListener {
public:
    AudiobookChaptersPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screenView.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screenView.getButtonHintsView().setHints(kLeftHints, kRightHints);
        emptyState.setMessage("No chapters");
        emptyState.setLine("This book has no playable files");
        screenView.setEmptyState(&emptyState);
    }

    std::span<const ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    HelpId getHelpId() const override { return HELP_AUDIOBOOK_CHAPTERS; }

    void onEnter() override {
        const Session& session = Session::getInstance();
        const audiobook::Book* book = session.getBook();
        screenView.getTitleView().setText(book ? book->title : "Chapters");
        screenView.setAdapter(chapters);
        list.start();
        list.setSelectedIndex(session.getChapterIndex());
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    /** The end of the book closes the session, and with it the chapters */
    void onTick() override {
        if (!Session::getInstance().isOpen()) close();
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (list.onButtonEvent(event)) return true;
        if (event.isSingleButton(Button::A)) {
            const int index = list.getSelectedIndex();
            if (index >= 0) finish(index);
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        return false;
    }

private:
    ChapterListAdapter chapters;
    ListScreenView screenView{Fonts_getLarge()};
    EmptyStateView emptyState;
    ListScreenController list{screenView, getScheduler(), *this};
};

}  // namespace

std::unique_ptr<UiPageImplBase> AudiobookChaptersPage::createPageImpl() const {
    return std::make_unique<AudiobookChaptersPageImpl>();
}
