#include "ui/audiobook_playing_page.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "audiobook/session.hpp"
#include "input/input_state.hpp"
#include "module_common.h"
#include "player.h"
#include "toast.h"
#include "ui/audiobook_chapters_page.hpp"
#include "ui/view/bitmap_view.hpp"
#include "ui/view/box_view.hpp"
#include "ui/view/progress_bar_view.hpp"
#include "ui/view/screen_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;
using audiobook::Book;
using audiobook::Chapter;
using audiobook::Session;

namespace {

constexpr const char* kLeftHints[]   = {"START", "CONTROLS", nullptr};
constexpr const char* kPlayingHints[] = {"X", "CHAPTERS", "A", "PAUSE", nullptr};
constexpr const char* kPausedHints[]  = {"X", "CHAPTERS", "A", "PLAY", nullptr};

constexpr int kSkipBackMs    = 10000;
constexpr int kSkipForwardMs = 30000;

constexpr ButtonRequest kButtonRequests[] = {
    request::Repeat{Button::DpadLeft},
    request::Repeat{Button::DpadRight},
};

std::string formatTime(int ms) {
    char text[16];
    format_time(text, std::max(ms, 0));
    return text;
}

/** Returns true for a button that the screen-off states keep: the power, the volume, START. */
bool isSystemButton(Button button) {
    return button == Button::Power || button == Button::PowerOff || button == Button::Plus ||
           button == Button::Minus || button == Button::Menu || button == Button::Start;
}

/** Returns the button of a pressed raw event, or nothing. */
std::optional<Button> getPressedButton(const RawEvent& event) {
    if (const auto* pressed = std::get_if<event::ButtonPressed>(&event.kind)) return pressed->button;
    return std::nullopt;
}

/** What the screen shows of the session, read once a frame. */
struct PlayingState {
    std::string author;
    std::string chapterTitle;
    std::string status;
    std::string remaining;
    std::string bookTime;
    int percent = 0;
};

PlayingState readState(const Session& session) {
    PlayingState state;
    const Book* book       = session.getBook();
    const Chapter* chapter = session.getChapter();
    state.author           = book ? book->author : "";
    state.chapterTitle     = chapter ? chapter->title : "";

    state.status = "Chapter " + std::to_string(session.getChapterIndex() + 1) + " of " +
                   std::to_string(session.getChapters().size());
    if (session.isSeeking()) state.status += "  -  Resuming...";
    else if (session.isPaused()) state.status += "  -  Paused";
    const std::string sleep = session.getSleepLabel();
    if (!sleep.empty()) state.status += "  -  Sleep " + sleep;

    const int duration = session.getChapterDurationMs();
    const int position = std::min(session.getChapterPositionMs(), duration > 0 ? duration : INT32_MAX);
    state.percent      = duration > 0 ? static_cast<int>(static_cast<int64_t>(position) * 100 / duration) : 0;
    state.remaining    = "-" + formatTime(duration - position);
    if (book && book->totalDurationMs > 0) {
        state.bookTime = formatTime(std::min(session.getBookPositionMs(), book->totalDurationMs)) + " / " +
                         formatTime(book->totalDurationMs);
    } else {
        state.bookTime = formatTime(position);
    }
    return state;
}

/** The body of the playing screen: the cover at the left, the texts beside it, the progress at the foot. */
class PlayingBody final : public UiLayout {
public:
    PlayingBody() {
        author.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        chapterTitle.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        status.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        remaining.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        bookTime.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
    }

    /** Shows `state`. Returns true when the chapter title changed. */
    bool show(const PlayingState& state) {
        author.setText(state.author);
        const bool titleChanged = chapterTitle.setText(state.chapterTitle);
        status.setText(state.status);
        remaining.setText(state.remaining);
        bookTime.setText(state.bookTime);
        bookTimeText = state.bookTime;
        bar.setPercent(state.percent);
        return titleChanged;
    }

    /** Shows the cover art of the file that plays, which the player owns. */
    void showCover(SDL_Surface* art) {
        if (!art) {
            cover.setBitmap(nullptr);
            return;
        }
        // The view frees its bitmap, thus it gets a copy
        cover.setBitmap(SDL_ConvertSurfaceFormat(art, SDL_PIXELFORMAT_ARGB8888, 0));
    }

    TextView& getChapterTitle() { return chapterTitle; }

    void layout(UiLayoutContext& context) override {
        const UiSize body = context.getMaxSize();
        const int smallH  = TTF_FontHeight(Fonts_getSmall());
        const int barH    = SCALE1(4);
        const int timesY  = body.h - smallH;
        const int barY    = timesY - SCALE1(6) - barH;
        const int top     = SCALE1(PADDING);
        const int upperH  = barY - SCALE1(PADDING) - top;

        int textX = 0;
        if (cover.hasBitmap() && upperH > 0) {
            const int size = std::min(upperH, body.w * 2 / 5);
            context.layoutChild(cover, SDL_Rect{0, top, size, size});
            textX = size + SCALE1(PADDING * 2);
        }
        const int textW = body.w - textX;

        int y             = top;
        const int authorH = TTF_FontHeight(Fonts_getArtist());
        context.layoutChild(author, SDL_Rect{textX, y, textW, authorH});
        y += authorH + SCALE1(4);
        const int titleH = TTF_FontHeight(Fonts_getTitle());
        context.layoutChild(chapterTitle, SDL_Rect{textX, y, textW, titleH});
        y += titleH + SCALE1(6);
        context.layoutChild(status, SDL_Rect{textX, y, textW, smallH});

        context.layoutChild(bar, SDL_Rect{0, barY, body.w, barH});
        int bookTimeW = 0;
        TTF_SizeUTF8(Fonts_getSmall(), bookTimeText.c_str(), &bookTimeW, nullptr);
        context.layoutChild(remaining, SDL_Rect{0, timesY, body.w / 2, smallH});
        context.layoutChild(bookTime, SDL_Rect{body.w - bookTimeW, timesY, bookTimeW, smallH});
    }

private:
    BitmapView cover;
    TextView author{Fonts_getArtist(), true};
    TextView chapterTitle{Fonts_getTitle(), false};
    TextView status{Fonts_getSmall(), true};
    ProgressTrackView bar;
    TextView remaining{Fonts_getSmall(), true};
    TextView bookTime{Fonts_getSmall(), false};
    std::string bookTimeText;
};

// The screen goes dark, thus the hint is white on black whatever the theme, as the hint of the
// legacy screens is
//noinspection HardcodedColor
const SDL_Color kHintTextColor = {255, 255, 255, 255};
//noinspection HardcodedColor
const SDL_Color kHintBackground = {0, 0, 0, 255};

/** The black screen of the screen-off hint, with the way to wake it in its middle. */
class ScreenOffView final : public UiLayout {
public:
    ScreenOffView() {
        message.setText("Press SELECT + A to wake screen");
        message.setColor(kHintTextColor);
    }

    /** Pass false to show the black screen with no message, as the screen goes dark. */
    void setMessageVisible(bool visible) { message.setVisible(visible); }

    void layout(UiLayoutContext& context) override {
        const UiSize size = context.getMaxSize();
        context.layoutChild(background, context.getMaxRect());
        const int textH = TTF_FontHeight(Fonts_getMedium());
        layoutCentered(context, message, (size.h - textH) / 2, textH);
    }

private:
    BoxView background{kHintBackground};
    TextView message{Fonts_getMedium(), true};
};

class AudiobookPlayingPageImpl final : public UiPageImpl<AudiobookPlayingPage> {
public:
    AudiobookPlayingPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screen.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screen.setContent(&body, ContentMargins::SidesAndFoot);
    }

    std::span<const ButtonRequest> getButtonRequests() const override { return kButtonRequests; }

    HelpId getHelpId() const override { return HELP_AUDIOBOOK_PLAYING; }

    void onEnter() override {
        Session& session = Session::getInstance();
        if (!session.isOpen()) {
            close();
            return;
        }
        const Book* book = session.getBook();
        screen.getTitleView().setText(book ? book->title : "Audiobook");
        getScheduler().postDelayed([this] { screen.getTitleView().startScrolling(); }, SCROLL_START_DELAY);
        if (session.isSeeking()) seekToast = Toast_showScreenBound("Resuming...", TOAST_DURATION_FOREVER);
        ModuleCommon_resetScreenOffHint();
        ModuleCommon_recordInputTime();
        refresh();
    }

    void onExit() override {
        Toast_dismiss(seekToast);
        // The next screen has its light and its keys
        if (screenOff) PLAT_enableBacklight(1);
        ModuleCommon_resetScreenOffHint();
    }

    void resume() override {
        ModuleCommon_recordInputTime();
        refresh();
    }

    void layout(UiLayoutContext& context) override {
        if (screenOff || ModuleCommon_isScreenOffHintActive()) {
            screenOffView.setMessageVisible(!screenOff);
            context.layoutChild(screenOffView, context.getMaxRect());
            return;
        }
        context.layoutChild(screen, context.getMaxRect());
    }

    void onTick() override {
        Session& session = Session::getInstance();
        if (ModuleCommon_isScreenOffHintActive() && ModuleCommon_processScreenOffHintTimeout()) {
            // The hint goes, and the light with it
            screenOff = true;
        }
        if (!session.isOpen()) {
            // A sleep timer that ends the book in the dark leaves the screen dark: the device goes
            // to sleep, and the next press closes the page
            if (screenOff || ModuleCommon_isScreenOffHintActive()) return;
            showEnd(session.takeEndReason());
            close();
            return;
        }
        if (seekToast != TOAST_TOKEN_NONE && !session.isSeeking()) {
            Toast_dismiss(seekToast);
            seekToast = TOAST_TOKEN_NONE;
        }
        if (screenOff || ModuleCommon_isScreenOffHintActive()) return;

        // A screen that keeps its content locks its keys in place of the hint
        if (!session.isPaused()) ModuleCommon_checkAutoScreenOffTimeout();
        refresh();
    }

    bool onRawEvent(const RawEvent& event) override {
        const std::optional<Button> pressed = getPressedButton(event);
        const bool system = pressed && isSystemButton(*pressed);
        if (screenOff || ModuleCommon_isScreenOffHintActive()) {
            if (!isButtonEvent(event) || system) return false;
            if (!pressed) return true;
            if (!Session::getInstance().isOpen()) {
                // The book ended in the dark. The press lights the screen and leaves the page.
                if (screenOff) PLAT_enableBacklight(1);
                screenOff = false;
                ModuleCommon_resetScreenOffHint();
                ModuleCommon_recordInputTime();
                showEnd(Session::getInstance().takeEndReason());
                close();
            } else if (screenOff) {
                // A press lights the screen with the hint, as the legacy screens do
                screenOff = false;
                PLAT_enableBacklight(1);
                ModuleCommon_startScreenOffHint();
            } else if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                ModuleCommon_resetScreenOffHint();
                ModuleCommon_recordInputTime();
                refresh();
            } else {
                // Any other press restarts the countdown of the hint
                ModuleCommon_startScreenOffHint();
            }
            return true;
        }
        if (pressed) ModuleCommon_recordInputTime();
        // The frame loop opens the lock of a screen that keeps its content at SELECT + A
        return InputState::getInstance().isKeyLocked() && isButtonEvent(event) && !system;
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        Session& session = Session::getInstance();
        if (event.isSingleButton(Button::A)) {
            session.togglePause();
        } else if (event.isSingleButton(Button::B)) {
            leave();
        } else if (event.isSingleButton(Button::X)) {
            openChapters();
        } else if (event.isSingleButton(Button::Y)) {
            Toast_show(session.cycleSleepTimer().c_str(), TOAST_DURATION);
        } else if (event.isSingleButton(Button::DpadLeft)) {
            session.skip(-kSkipBackMs);
        } else if (event.isSingleButton(Button::DpadRight)) {
            session.skip(kSkipForwardMs);
        } else if (event.isSingleButton(Button::DpadUp) || event.isSingleButton(Button::L1)) {
            stepChapter(-1);
        } else if (event.isSingleButton(Button::DpadDown) || event.isSingleButton(Button::R1)) {
            stepChapter(1);
        } else if (event.isSingleButton(Button::Select)) {
            ModuleCommon_startScreenOffHint();
        } else {
            return false;
        }
        refresh();
        return true;
    }

private:
    /** Reads the session, and shows it. */
    void refresh() {
        Session& session = Session::getInstance();
        if (!session.isOpen()) return;
        if (body.show(readState(session))) {
            // A new chapter title waits at its start before it scrolls
            body.getChapterTitle().resetScrolling();
            body.getChapterTitle().stopScrolling();
            getScheduler().cancelTask(titleScrollTask);
            titleScrollTask =
                getScheduler().postDelayed([this] { body.getChapterTitle().startScrolling(); }, SCROLL_START_DELAY);
        }

        // The art of a chapter comes with the load of its file
        SDL_Surface* art        = Player_getAlbumArt();
        const char* file        = Player_getCurrentFile();
        const std::string shown = file ? file : "";
        if (art != shownArt || shown != shownFile) {
            body.showCover(art);
            shownArt  = art;
            shownFile = shown;
        }

        const bool paused = session.isPaused();
        if (!hintsSet || paused != hintsPaused) {
            screen.getButtonHintsView().setHints(kLeftHints, paused ? kPausedHints : kPlayingHints);
            hintsSet    = true;
            hintsPaused = paused;
        }
    }

    /** B: a book that plays goes on in the background. A paused book stops, and keeps its place. */
    void leave() {
        Session& session = Session::getInstance();
        if (session.isPaused() || session.isSeeking()) session.close();
        else session.saveNow();
        close();
    }

    void stepChapter(int step) {
        Session& session = Session::getInstance();
        const int target = session.getChapterIndex() + step;
        if (target < 0 || target >= static_cast<int>(session.getChapters().size())) return;
        if (!session.playChapter(target)) Toast_show("Failed to play", TOAST_DURATION);
    }

    void openChapters() {
        getNavigator().openForResult(AudiobookChaptersPage{}, [](const std::optional<int>& chapter) {
            if (!chapter) return;
            if (!Session::getInstance().playChapter(*chapter)) Toast_show("Failed to play", TOAST_DURATION);
        });
    }

    /** The toast of a session that ended by itself. */
    static void showEnd(Session::EndReason reason) {
        switch (reason) {
            case Session::EndReason::Finished:   Toast_show("Finished", TOAST_DURATION); break;
            case Session::EndReason::SleepTimer: Toast_show("Sleep timer: stopped", TOAST_DURATION); break;
            case Session::EndReason::None:       break;
        }
    }

    PlayingBody body;
    ScreenView screen{Fonts_getLarge()};
    ScreenOffView screenOffView;
    ToastToken seekToast   = TOAST_TOKEN_NONE;
    TaskId titleScrollTask = 0;
    /** The backlight is off after the hint */
    bool screenOff         = false;
    SDL_Surface* shownArt  = nullptr;
    std::string shownFile;
    bool hintsSet    = false;
    bool hintsPaused = false;
};

}  // namespace

std::unique_ptr<UiPageImplBase> AudiobookPlayingPage::createPageImpl() const {
    return std::make_unique<AudiobookPlayingPageImpl>();
}
