#include "ui/app_update_page.hpp"

#include <string>

#include "module_common.h"
#include "selfupdate.h"
#include "ui/view/screen_view.hpp"
#include "ui/view/multiline_text_view.hpp"
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
constexpr const char* kRestartHints[] = {"A", "RESTART", nullptr};
constexpr const char* kCancelHints[]  = {"B", "CANCEL", nullptr};
constexpr const char* kBackHints[]    = {"B", "BACK", nullptr};

/** The most lines of release notes that the page shows */
constexpr int kMaxNoteLines = 16;

/**
 * Returns the release notes in the form that the page shows: a carriage return becomes a space,
 * the spaces at the start of a line go away, and a blank line goes away.
 */
std::string normalizeNotes(const char* notes) {
    std::string result;
    std::string line;
    const auto addLine = [&] {
        const size_t start = line.find_first_not_of(' ');
        if (start != std::string::npos) {
            if (!result.empty()) result += '\n';
            result += line.substr(start);
        }
        line.clear();
    };
    for (const char* at = notes; *at; at++) {
        if (*at == '\n') {
            addLine();
        } else {
            line += *at == '\r' ? ' ' : *at;
        }
    }
    addLine();
    return result;
}

bool isBarVisible(SelfUpdateState state) {
    return state == SELFUPDATE_STATE_DOWNLOADING || state == SELFUPDATE_STATE_EXTRACTING ||
           state == SELFUPDATE_STATE_APPLYING;
}

const char* const* getRightHints(SelfUpdateState state) {
    if (state == SELFUPDATE_STATE_COMPLETED) return kRestartHints;
    if (state == SELFUPDATE_STATE_DOWNLOADING) return kCancelHints;
    return kBackHints;
}

/** The body of the app update: the versions, the release notes, the progress and the status. */
class AppUpdateBody final : public UiLayout {
public:
    AppUpdateBody() {
        version.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        checking.setText("Checking for updates...");
        checking.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        detail.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        notes.setLineHeight(SCALE1(18));
        notes.setMaxLines(kMaxNoteLines);
        notes.setCutToSpace(true);
    }

    /** Shows `status`, one snapshot of the state of the update. */
    void show(const SelfUpdateStatus& status) {
        state = status.state;
        std::string versionText = status.current_version;
        if (status.latest_version[0]) versionText += std::string("  ->  ") + status.latest_version;
        version.setText(versionText);
        notes.setText(normalizeNotes(status.release_notes));

        progress.setPercent(status.progress_percent);
        hasDetail = status.status_detail[0] != '\0';
        detail.setText(status.status_detail);

        // During the download the detail shows the size, thus the status text does not show
        hasStatusText = state == SELFUPDATE_STATE_EXTRACTING || state == SELFUPDATE_STATE_APPLYING ||
                        state == SELFUPDATE_STATE_COMPLETED || state == SELFUPDATE_STATE_ERROR;
        ThemeRole role = THEME_ROLE_PRIMARY;
        if (state == SELFUPDATE_STATE_ERROR) role = THEME_ROLE_STATUS_ERROR;
        if (state == SELFUPDATE_STATE_COMPLETED) role = THEME_ROLE_STATUS_SUCCESS;
        const bool showError = state == SELFUPDATE_STATE_ERROR && status.error_message[0];
        statusText.setText(showError ? status.error_message : status.status_message);
        statusText.setColor(Theme_getColor(role, false));
    }

    void layout(UiLayoutContext& context) override {
        const UiSize body     = context.getMaxSize();
        const int versionTop  = SCALE1(PADDING * 2 + 5);
        const int smallHeight = TTF_FontHeight(Fonts_getSmall());
        const int barTop      = body.h - SCALE1(PADDING * 9);
        const int statusTop   = body.h - SCALE1(PADDING * 5);
        const bool barVisible = isBarVisible(state);
        const int notesTop    = versionTop + SCALE1(30);
        layoutCentered(context, version, versionTop, TTF_FontHeight(Fonts_getMedium()));
        if (state == SELFUPDATE_STATE_CHECKING) {
            layoutCentered(context, checking, notesTop, smallHeight);
        } else {
            // The notes run down to what comes after them, one padding short of it
            const int notesBottom = (barVisible ? barTop : statusTop) - SCALE1(PADDING);
            const int notesLeft   = SCALE1(PADDING * 3);
            context.layoutChild(notes, SDL_Rect{notesLeft, notesTop, body.w - notesLeft * 2, notesBottom - notesTop});
        }
        if (barVisible) {
            const int barHeight       = SCALE1(12);
            const int barLeft         = SCALE1(PADDING * 4);
            const SDL_Rect barRoom    = {0, 0, body.w - barLeft * 2, body.h};
            UiLayoutContext measuring = context.createMeasuringContext();
            const int viewHeight      = measuring.layoutChild(progress, barRoom).getSize().h;
            // The view centers the bar in its height, thus the bar starts at barTop
            context.layoutChild(progress, SDL_Rect{barLeft, barTop - (viewHeight - barHeight) / 2, barRoom.w, viewHeight});
            if (hasDetail) layoutCentered(context, detail, barTop + barHeight + SCALE1(6), smallHeight);
        }
        if (hasStatusText) layoutCentered(context, statusText, statusTop, smallHeight);
    }

private:
    TextView version{Fonts_getMedium(), true};
    MultilineTextView notes{Fonts_getSmall()};
    TextView checking{Fonts_getSmall(), true};
    ProgressBarView progress{Fonts_getTiny(), SCALE1(12)};
    TextView detail{Fonts_getSmall(), true};
    TextView statusText{Fonts_getSmall(), true};
    SelfUpdateState state = SELFUPDATE_STATE_IDLE;
    bool hasDetail        = false;
    bool hasStatusText    = false;
};

class AppUpdatePageImpl final : public UiPageImpl<AppUpdatePage> {
public:
    AppUpdatePageImpl() : UiPageImpl(PageKind::FullScreen) {
        screen.getTitleView().setText("App Update");
        screen.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screen.setContent(&body);
    }

    HelpId getHelpId() const override { return HELP_ABOUT; }

    void onEnter() override {
        // A long title scrolls after a wait, as on a list screen
        getScheduler().postDelayed([this] { screen.getTitleView().startScrolling(); }, SCROLL_START_DELAY);
        ModuleCommon_setAutosleepDisabled(true);
        SelfUpdate_startUpdate();
        showStatus();
        getScheduler().runEachFrame([this] {
            // The end of the background playback turns the automatic sleep on again
            ModuleCommon_setAutosleepDisabled(true);
            SelfUpdate_update();
            showStatus();
        });
    }

    void onExit() override { ModuleCommon_setAutosleepDisabled(false); }

    /** The update continues while a dialog covers the page */
    void resume() override { showStatus(); }

    void layout(UiLayoutContext& context) override { context.layoutChild(screen, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        // A button acts on the state of now, not on the state of the last frame
        if (event.isSingleButton(Button::A) || event.isSingleButton(Button::B)) showStatus();
        if (event.isSingleButton(Button::A)) {
            if (shownState == SELFUPDATE_STATE_COMPLETED) {
                // The new binary is on disk. The exit with the flag set lets launch.sh start it.
                SelfUpdate_requestRestart();
                getNavigator().exit();
            }
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            if (shownState == SELFUPDATE_STATE_COMPLETED) return true;
            if (shownState == SELFUPDATE_STATE_DOWNLOADING) SelfUpdate_cancelUpdate();
            close();
            return true;
        }
        return false;
    }

private:
    /** Shows one snapshot of the state of the update. */
    void showStatus() {
        const SelfUpdateStatus status = SelfUpdate_getStatus();
        body.show(status);
        if (status.state != shownState || !hintsSet) {
            screen.getButtonHintsView().setHints(kLeftHints, getRightHints(status.state));
        }
        hintsSet   = true;
        shownState = status.state;
    }

    AppUpdateBody body;
    ScreenView screen{Fonts_getLarge()};
    SelfUpdateState shownState = SELFUPDATE_STATE_IDLE;
    bool hintsSet              = false;
};

}  // namespace

std::unique_ptr<UiPageImplBase> AppUpdatePage::createPageImpl() const {
    return std::make_unique<AppUpdatePageImpl>();
}
