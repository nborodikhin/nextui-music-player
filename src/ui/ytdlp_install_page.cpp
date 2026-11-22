#include "ui/ytdlp_install_page.hpp"

#include <string>

#include "downloader.h"
#include "ui/view/screen_view.hpp"
#include "ui/view/progress_bar_view.hpp"
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

namespace {

constexpr const char* kLeftHints[]    = {"START", "CONTROLS", nullptr};
constexpr const char* kCancelHints[]  = {"B", "CANCEL", nullptr};
constexpr const char* kInstallHints[] = {"B", "BACK", "A", "INSTALL", nullptr};
constexpr const char* kUpdateHints[]  = {"B", "BACK", "A", "UPDATE", nullptr};
constexpr const char* kBackHints[]    = {"B", "BACK", nullptr};

/** Returns true while a worker of the helpers runs */
bool isWorking(YtdlpUiState state) { return state == YTDLP_UI_CHECKING || state == YTDLP_UI_INSTALLING; }

/** The body of the installer: the versions, the status, and the progress of an install. */
class YtdlpInstallBody final : public UiLayout {
public:
    YtdlpInstallBody() {
        current.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        statusText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        latest.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        detail.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
    }

    /** Shows `status` in `state`, one snapshot of the state of the helpers. */
    void show(const DownloaderUpdateStatus& status, YtdlpUiState newState) {
        state = newState;
        // A missing helper makes this an install: the set is what the user has
        const bool installing = status.fresh_install;
        current.setText(std::string("Current: ") +
                        (installing ? DOWNLOADER_VERSION_NOT_INSTALLED : status.current_version));

        std::string message;
        switch (state) {
            case YTDLP_UI_UNCHECKED: message = "Checking connection..."; break;
            case YTDLP_UI_CHECKING:  message = "Checking for updates..."; break;
            case YTDLP_UI_CURRENT:   message = "Already up to date!"; break;
            case YTDLP_UI_AVAILABLE:
                // Current and Latest show above and below, thus the line names what the fetch covers
                message = std::string(installing ? "Install " : "Update ") + status.plan_summary + "?";
                break;
            case YTDLP_UI_INSTALLING:
                message = status.step_message[0] ? status.step_message : "Starting...";
                if (status.step_message[0] && status.step_count > 1) {
                    message += " (" + std::to_string(status.step_index) + " of " + std::to_string(status.step_count) + ")";
                }
                break;
            case YTDLP_UI_INSTALLED: message = installing ? "Install complete!" : "Update complete!"; break;
            case YTDLP_UI_FAILED:    message = status.error_message[0] ? status.error_message : "Update failed"; break;
        }
        statusText.setText(message);

        hasLatest = status.latest_version[0] != '\0';
        latest.setText(std::string("Latest: ") + status.latest_version);
        progress.setPercent(status.progress_percent);
        hasDetail = status.status_detail[0] != '\0';
        detail.setText(status.status_detail);
    }

    void layout(UiLayoutContext& context) override {
        const UiSize body      = context.getMaxSize();
        const int middle       = body.h / 2;
        const int mediumHeight = TTF_FontHeight(Fonts_getMedium());
        const int smallHeight  = TTF_FontHeight(Fonts_getSmall());
        layoutCentered(context, current, middle - SCALE1(50), mediumHeight);
        layoutCentered(context, statusText, middle, mediumHeight);
        if (hasLatest) layoutCentered(context, latest, middle + SCALE1(30), smallHeight);
        if (state != YTDLP_UI_INSTALLING) return;
        const int barTop          = middle + SCALE1(55);
        const int barHeight       = SCALE1(12);
        const int barLeft         = SCALE1(PADDING * 4);
        const SDL_Rect barRoom    = {0, 0, body.w - barLeft * 2, body.h};
        UiLayoutContext measuring = context.createMeasuringContext();
        const int viewHeight      = measuring.layoutChild(progress, barRoom).getSize().h;
        // The view centers the bar in its height, thus the bar starts at barTop
        context.layoutChild(progress, SDL_Rect{barLeft, barTop - (viewHeight - barHeight) / 2, barRoom.w, viewHeight});
        if (hasDetail) layoutCentered(context, detail, barTop + barHeight + SCALE1(6), smallHeight);
    }

private:
    TextView current{Fonts_getMedium(), true};
    TextView statusText{Fonts_getMedium(), true};
    TextView latest{Fonts_getSmall(), true};
    ProgressBarView progress{Fonts_getTiny(), SCALE1(12)};
    TextView detail{Fonts_getSmall(), true};
    YtdlpUiState state = YTDLP_UI_UNCHECKED;
    bool hasLatest     = false;
    bool hasDetail     = false;
};

class YtdlpInstallPageImpl final : public UiPageImpl<YtdlpInstallPage> {
public:
    YtdlpInstallPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screen.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screen.setContent(&body);
    }

    HelpId getHelpId() const override { return HELP_DOWNLOADER_YTDLP; }

    void onEnter() override {
        // A long title scrolls after a wait, as on a list screen
        getScheduler().postDelayed([this] { screen.getTitleView().startScrolling(); }, SCROLL_START_DELAY);
        // The helpers ship apart from the app, thus the paths of the downloader must exist first
        Downloader_init();
        Downloader_startUpdateCheck();
        showStatus();
        getScheduler().runEachFrame([this] {
            Downloader_update();
            showStatus();
        });
    }

    /** The work continues while a dialog covers the page */
    void resume() override { showStatus(); }

    /** A quit of the app stops the work that runs */
    void onExit() override {
        if (isWorking(shownState)) Downloader_cancelUpdate();
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(screen, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        // A button acts on the state of now, not on the state of the last frame
        if (event.isSingleButton(Button::A) || event.isSingleButton(Button::B)) showStatus();
        if (event.isSingleButton(Button::A)) {
            if (shownState == YTDLP_UI_AVAILABLE) {
                // Nothing is transferred before this
                Downloader_startUpdate();
            } else if (!isWorking(shownState)) {
                // A dismisses a settled result, as B does
                end();
            }
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            if (isWorking(shownState)) Downloader_cancelUpdate();
            end();
            return true;
        }
        return false;
    }

private:
    void end() {
        Downloader_refreshVersion();
        finish(Downloader_isAvailable());
    }

    /** Shows one snapshot of the state of the helpers. */
    void showStatus() {
        // The text and the hints come from one snapshot: the workers change it on their threads
        const DownloaderUpdateStatus status = Downloader_getUpdateStatus();
        const YtdlpUiState state            = Downloader_updateUiState(&status);
        const bool installing               = status.fresh_install;
        screen.getTitleView().setText(installing ? "Installing Youtube helpers" : "Updating Youtube helpers");
        body.show(status, state);

        const char* const* rightHints = kBackHints;
        if (state == YTDLP_UI_INSTALLING) rightHints = kCancelHints;
        if (state == YTDLP_UI_AVAILABLE) rightHints = installing ? kInstallHints : kUpdateHints;
        if (rightHints != shownHints) screen.getButtonHintsView().setHints(kLeftHints, rightHints);
        shownHints = rightHints;
        shownState = state;
    }

    YtdlpInstallBody body;
    ScreenView screen{Fonts_getLarge()};
    YtdlpUiState shownState       = YTDLP_UI_UNCHECKED;
    const char* const* shownHints = nullptr;
};

}  // namespace

std::unique_ptr<UiPageImplBase> YtdlpInstallPage::createPageImpl() const {
    return std::make_unique<YtdlpInstallPageImpl>();
}
