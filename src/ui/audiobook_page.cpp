#include "ui/audiobook_page.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "audiobook/library.hpp"
#include "audiobook/session.hpp"
#include "toast.h"
#include "ui/audiobook_playing_page.hpp"
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
using audiobook::Book;
using audiobook::Library;
using audiobook::Session;

namespace {

constexpr const char* kLeftHints[]       = {"START", "CONTROLS", nullptr};
constexpr const char* kRightHints[]      = {"X", "FINISHED", "B", "BACK", "A", "PLAY", nullptr};
constexpr const char* kEmptyRightHints[] = {"B", "BACK", nullptr};

/** The key of the book of the cursor when the list closed last */
std::string lastSelectedKey;

std::string formatTime(int ms) {
    char text[16];
    format_time(text, ms < 0 ? 0 : ms);
    return text;
}

/** The state of a book, at the right of its row. */
std::string describeBook(const Book& book) {
    if (book.progress.finished) return "Finished";
    if (book.progress.lastPlayed > 0) {
        return "Ch. " + std::to_string(book.progress.chapter + 1) + "/" + std::to_string(book.chapterCount) +
               "  " + formatTime(book.progress.positionMs);
    }
    return formatTime(book.totalDurationMs);
}

/** A row of the list: the Continue row, or a book. */
struct BookRow {
    bool isContinue;
    std::string key;
};

/** The rows of the books, with the Continue row first where a book is in progress. */
class BookListAdapter final : public ListAdapter {
public:
    /** Reads the books of the library again. */
    void reload() {
        rows.clear();
        const Library& library = Library::getInstance();
        if (const Book* resume = library.getResumeBook()) rows.push_back({true, resume->key});
        for (const Book& book : library.getBooks()) rows.push_back({false, book.key});
    }

    const BookRow* getRow(int index) const {
        if (index < 0 || index >= getCount()) return nullptr;
        return &rows[static_cast<size_t>(index)];
    }

    int getCount() const override { return static_cast<int>(rows.size()); }

    /** The id comes from the key and the label, thus a row that changes gets a new view. */
    long getItemId(int index) const override {
        const BookRow& row = rows[static_cast<size_t>(index)];
        std::string id = (row.isContinue ? "C\n" : "B\n") + row.key + '\n' + getRightText(row);
        const auto [it, added] = ids.try_emplace(std::move(id), static_cast<long>(ids.size()));
        return it->second;
    }

    std::shared_ptr<ListItemView> getView(int index) override {
        const BookRow& row = rows[static_cast<size_t>(index)];
        const Book* book   = Library::getInstance().findBook(row.key);
        auto view          = std::make_shared<ListRowView>(Fonts_getMedium());
        const std::string title = book ? book->title : row.key;
        view->setText(row.isContinue ? "Continue: " + title : title);
        auto right = std::make_shared<TextView>(Fonts_getTiny(), false);
        right->setText(getRightText(row));
        right->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        view->setRightView(std::move(right));
        return view;
    }

private:
    static std::string getRightText(const BookRow& row) {
        const Book* book = Library::getInstance().findBook(row.key);
        return book ? describeBook(*book) : "";
    }

    std::vector<BookRow> rows;
    mutable std::unordered_map<std::string, long> ids;
};

class AudiobookPageImpl final : public UiPageImpl<AudiobookPage>, private ListScreenListener {
public:
    AudiobookPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screenView.getTitleView().setText("Audiobook");
        screenView.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        emptyState.setMessage("No audiobooks found");
        emptyState.setLine("Copy books into /Audiobook on the SD card");
        screenView.setEmptyState(&emptyState);
    }

    std::span<const ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    HelpId getHelpId() const override { return HELP_AUDIOBOOK_LIBRARY; }

    void onEnter() override {
        Library::getInstance().scan();
        books.reload();
        screenView.setAdapter(books);
        list.start();
        selectBook(lastSelectedKey, false);
        updateHints();
    }

    void onExit() override {
        if (const BookRow* row = books.getRow(list.getSelectedIndex())) lastSelectedKey = row->key;
        Library::getInstance().flush();
    }

    /** The playing screen moves the place of the book, and its end can mark it finished */
    void resume() override { reloadBooks(); }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (list.onButtonEvent(event)) return true;
        if (event.isSingleButton(Button::A)) {
            playSelectedBook();
            return true;
        }
        if (event.isSingleButton(Button::X)) {
            toggleFinished();
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        return false;
    }

private:
    void playSelectedBook() {
        const BookRow* row = books.getRow(list.getSelectedIndex());
        if (!row) return;
        Session& session = Session::getInstance();
        // The book that already plays keeps its place
        if (!(session.isOpen() && session.getKey() == row->key) && !session.open(row->key)) {
            Toast_show("Failed to play", TOAST_DURATION);
            return;
        }
        lastSelectedKey = row->key;
        getNavigator().open(AudiobookPlayingPage{});
    }

    void toggleFinished() {
        const BookRow* row = books.getRow(list.getSelectedIndex());
        if (!row) return;
        const Book* book = Library::getInstance().findBook(row->key);
        if (!book) return;
        const bool finished = !book->progress.finished;
        const std::string key = row->key;
        Library::getInstance().setFinished(key, finished);
        Library::getInstance().flush();
        Toast_show(finished ? "Marked as finished" : "Marked as unfinished", TOAST_DURATION);
        reloadBooks();
        selectBook(key, true);
    }

    /** Reads the books again, in their new order. The cursor stays on its book. */
    void reloadBooks() {
        const BookRow* row      = books.getRow(list.getSelectedIndex());
        const std::string key   = row ? row->key : "";
        const bool wasContinue  = row && row->isContinue;
        Library::getInstance().scan();
        books.reload();
        list.notifyDataChanged();
        selectBook(key, !wasContinue);
        updateHints();
    }

    /** Puts the cursor on the row of `key`: its book row, or else the row that names it first. */
    void selectBook(const std::string& key, bool preferBookRow) {
        if (key.empty()) return;
        int first = -1;
        for (int index = 0; index < books.getCount(); index++) {
            const BookRow* row = books.getRow(index);
            if (row->key != key) continue;
            if (first < 0) first = index;
            if (!preferBookRow || !row->isContinue) {
                list.setSelectedIndex(index);
                return;
            }
        }
        if (first >= 0) list.setSelectedIndex(first);
    }

    void updateHints() {
        screenView.getButtonHintsView().setHints(kLeftHints, books.getCount() > 0 ? kRightHints : kEmptyRightHints);
    }

    BookListAdapter books;
    ListScreenView screenView{Fonts_getLarge()};
    EmptyStateView emptyState;
    ListScreenController list{screenView, getScheduler(), *this};
};

}  // namespace

std::unique_ptr<UiPageImplBase> AudiobookPage::createPageImpl() const {
    return std::make_unique<AudiobookPageImpl>();
}
