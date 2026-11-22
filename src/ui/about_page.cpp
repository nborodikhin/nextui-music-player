#include "ui/about_page.hpp"

#include <string>

#include "qr_code_data.h"
#include "selfupdate.h"
#include "ui/app_update_page.hpp"
#include "ui/view/bitmap_view.hpp"
#include "ui/view/screen_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui/wifi_dialog_page.hpp"
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
constexpr const char* kBackHints[]    = {"B", "BACK", nullptr};
constexpr const char* kUpdateHints[]  = {"B", "BACK", "A", "UPDATE", nullptr};
constexpr const char* kCheckHints[]   = {"B", "BACK", "A", "CHECK UPDATE", nullptr};
constexpr const char* kRestartHints[] = {"B", "BACK", "A", "RESTART", nullptr};

/** Returns the right hints of the state: the action of A, where the state has one. */
const char* const* getRightHints(UpdateUiState state) {
    switch (state) {
        case UPDATE_UI_AVAILABLE: return kUpdateHints;
        case UPDATE_UI_UNCHECKED:
        case UPDATE_UI_FAILED:    return kCheckHints;
        case UPDATE_UI_RESTART:   return kRestartHints;
        case UPDATE_UI_CHECKING:
        case UPDATE_UI_CURRENT:   break;
    }
    return kBackHints;
}

/** The body of About: the name of the app, the tagline, the state of the update and the QR code. */
class AboutBody final : public UiLayout {
public:
    AboutBody() {
        appName.setText(std::string("Music Player (") + SelfUpdate_getVersion() + ")");
        appName.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        taglineFirst.setText("Your favorite tunes on the go,");
        taglineFirst.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        taglineSecond.setText("powered by your gaming handheld.");
        taglineSecond.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        qrCode.loadMemory(qr_code_png, sizeof(qr_code_png));
    }

    void layout(UiLayoutContext& context) override {
        const UiSize body     = context.getMaxSize();
        const int smallHeight = TTF_FontHeight(Fonts_getSmall());
        layoutCentered(context, appName, 0, TTF_FontHeight(Fonts_getLarge()));
        layoutCentered(context, taglineFirst, SCALE1(30), smallHeight);
        layoutCentered(context, taglineSecond, SCALE1(48), smallHeight);
        layoutCentered(context, updateStatus, SCALE1(70), smallHeight);
        const int qrSize = SCALE1(75);
        const int qrTop  = body.h - SCALE1(PADDING * 3) - qrSize;
        context.layoutChild(qrCode, SDL_Rect{(body.w - qrSize) / 2, qrTop, qrSize, qrSize});
    }

    TextView updateStatus{Fonts_getSmall(), true};

private:
    TextView appName{Fonts_getLarge(), true};
    TextView taglineFirst{Fonts_getSmall(), true};
    TextView taglineSecond{Fonts_getSmall(), true};
    BitmapView qrCode;
};

class AboutPageImpl final : public UiPageImpl<AboutPage> {
public:
    AboutPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screen.getTitleView().setText("About");
        screen.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screen.setContent(&body);
    }

    HelpId getHelpId() const override { return HELP_ABOUT; }

    void onEnter() override {
        // A long title scrolls after a wait, as on a list screen
        getScheduler().postDelayed([this] { screen.getTitleView().startScrolling(); }, SCROLL_START_DELAY);
        showUpdateState();
        getScheduler().runEachFrame([this] {
            SelfUpdate_update();
            showUpdateState();
        });
    }

    void resume() override { showUpdateState(); }

    void layout(UiLayoutContext& context) override { context.layoutChild(screen, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (event.isSingleButton(Button::A)) {
            // A acts on the state of now, not on the state of the last frame
            showUpdateState();
            activate();
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        return false;
    }

private:
    void activate() {
        switch (shownState) {
            case UPDATE_UI_RESTART:
                // The new binary is on disk. The exit with the flag set lets launch.sh start it.
                SelfUpdate_requestRestart();
                getNavigator().exit();
                break;
            case UPDATE_UI_AVAILABLE: getNavigator().open(AppUpdatePage{}); break;
            case UPDATE_UI_UNCHECKED:
            case UPDATE_UI_FAILED:
                getNavigator().openForResult(WifiDialogPage{}, [](const bool& connected) {
                    if (connected) SelfUpdate_checkForUpdate();
                });
                break;
            case UPDATE_UI_CHECKING:
            case UPDATE_UI_CURRENT: break;
        }
    }

    /** Shows the status text and the hints of one snapshot of the update state. */
    void showUpdateState() {
        // One snapshot for the text and the hints: the check thread can change the state between them
        const SelfUpdateStatus status = SelfUpdate_getStatus();
        const UpdateUiState state     = SelfUpdate_uiState(&status);
        const std::string latest = status.latest_version;
        std::string text;
        ThemeRole role = THEME_ROLE_PRIMARY;
        switch (state) {
            case UPDATE_UI_UNCHECKED: break;
            case UPDATE_UI_CHECKING:
                text = "Checking for updates...";
                role = THEME_ROLE_SECONDARY;
                break;
            case UPDATE_UI_AVAILABLE:
                text = "Update available: " + latest;
                role = THEME_ROLE_STATUS_SUCCESS;
                break;
            case UPDATE_UI_CURRENT:
                text = "Up to date (latest version " + latest + ")";
                role = THEME_ROLE_SECONDARY;
                break;
            case UPDATE_UI_RESTART:
                text = "Update installed: " + latest;
                role = THEME_ROLE_STATUS_SUCCESS;
                break;
            case UPDATE_UI_FAILED:
                text = status.error_message[0] ? status.error_message : "Update check failed";
                role = THEME_ROLE_STATUS_ERROR;
                break;
        }
        body.updateStatus.setText(text);
        body.updateStatus.setColor(Theme_getColor(role, false));
        if (state != shownState || !hintsSet) screen.getButtonHintsView().setHints(kLeftHints, getRightHints(state));
        hintsSet   = true;
        shownState = state;
    }

    AboutBody body;
    ScreenView screen{Fonts_getLarge()};
    UpdateUiState shownState = UPDATE_UI_UNCHECKED;
    bool hintsSet            = false;
};

}  // namespace

std::unique_ptr<UiPageImplBase> AboutPage::createPageImpl() const {
    return std::make_unique<AboutPageImpl>();
}
