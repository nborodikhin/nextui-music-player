#include "ui/wifi_dialog_page.hpp"

#include "toast.h"
#include "ui/view/dialog_view.hpp"
#include "ui_fonts.h"
#include "wifi.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

/** The time that the dialog waits for the probe, in the active time of the page */
constexpr uint32_t kDeadlineMs = 8000;

/** The time between two reads of the state of the probe */
constexpr uint32_t kCheckPeriodMs = 1000;

class WifiDialogPageImpl final : public UiPageImpl<WifiDialogPage> {
public:
    WifiDialogPageImpl()
        : UiPageImpl(PageKind::Dialog),
          dialog(SCALE1(280), DialogView::Alignment::Center, Fonts_getMedium(), Fonts_getSmall()) {
        dialog.setTitle("Connecting to WiFi...");
        dialog.setHint("B: Cancel");
    }

    void onEnter() override {
        if (Wifi_isConnected()) {
            finish(true);
            return;
        }
        // A task that is posted in a frame runs in a later frame, before the draw of that frame.
        // The start can block while Wi-Fi turns on, thus it runs one frame after the first draw.
        getScheduler().post([this] { getScheduler().post([this] { startProbe(); }); });
    }

    /** A wake from a sleep reads the probe at once */
    void resume() override {
        if (probeStarted) checkProbe();
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(dialog, context.getMaxRect()); }

    bool onRawEvent(const RawEvent& event) override {
        // A media key goes on to the earphone buttons of the global handler
        return isButtonEvent(event);
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        // The user chose to stop the action, thus no toast tells about it
        if (event.isSingleButton(Button::B)) end(false);
        return true;
    }

private:
    void startProbe() {
        const bool turnOn = !Wifi_isEnabled();
        if (turnOn) Wifi_enable();
        Wifi_startConnect(turnOn);
        probeStarted = true;
        // The time that Wi-Fi took to turn on does not count in the deadline
        getScheduler().post([this] {
            deadlineTask = ScheduledTask(getScheduler(), getScheduler().postDelayed([this] { checkProbe(true); }, kDeadlineMs));
        });
        checkTask = ScheduledTask(getScheduler(), getScheduler().postRepeating([this] { checkProbe(); }, kCheckPeriodMs));
    }

    /** Ends the wait when the probe ended. Pass true in `deadline` to fail a probe that still runs. */
    void checkProbe(bool deadline = false) {
        const WifiConnectState state = Wifi_getConnectState();
        if (state == WIFI_CONNECT_CONNECTED) {
            end(true);
        } else if (state == WIFI_CONNECT_FAILED || deadline) {
            fail();
        }
    }

    void fail() {
        Toast_show("Internet connection required", TOAST_DURATION);
        end(false);
    }

    /** Stops the tasks, thus no task of a later frame gives a second result */
    void end(bool connected) {
        checkTask.cancel();
        deadlineTask.cancel();
        finish(connected);
    }

    DialogView dialog;
    ScheduledTask checkTask;
    ScheduledTask deadlineTask;
    bool probeStarted = false;
};

}  // namespace

std::unique_ptr<UiPageImplBase> WifiDialogPage::createPageImpl() const {
    return std::make_unique<WifiDialogPageImpl>();
}
