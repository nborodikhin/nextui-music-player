// Tests for src/ui/wifi_dialog_page.cpp - the wait for Wi-Fi before an action that needs the
// network. The Wi-Fi functions, the toast and the layers are test doubles.

#include <optional>
#include <string>
#include <vector>

#include "test.h"
#include "input/common_button_handler.hpp"
#include "toast.h"
#include "ui/page/page_stack.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui/wifi_dialog_page.hpp"
#include "ui_fonts.h"
#include "ui_layers.h"
#include "ui_theme.h"
#include "wifi.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

namespace {

struct FakeWifi {
    bool connected            = false;
    bool enabled              = true;
    WifiConnectState state    = WIFI_CONNECT_IDLE;
    int enableCalls           = 0;
    int startCalls            = 0;
    bool startedJustEnabled   = false;
    /** True when the dialog drew a frame before Wi-Fi turned on */
    bool drawnBeforeEnable    = false;
};

FakeWifi wifi;
std::vector<std::string> toasts;
/** True after a layout of the stack with a dialog on the top */
bool dialogDrawn = false;

}  // namespace

extern "C" void UiLayer_clear(UiLayer) {}
extern "C" void UiLayer_blit(SDL_Surface*, int, int, UiLayer) {}

extern "C" bool Wifi_isConnected(void) { return wifi.connected; }
extern "C" bool Wifi_isEnabled(void) { return wifi.enabled; }
extern "C" void Wifi_enable(void) {
    wifi.enableCalls++;
    wifi.drawnBeforeEnable = dialogDrawn;
    wifi.enabled           = true;
}
extern "C" void Wifi_startConnect(bool wifiJustEnabled) {
    wifi.startCalls++;
    wifi.startedJustEnabled = wifiJustEnabled;
    if (wifi.state != WIFI_CONNECT_RUNNING) wifi.state = WIFI_CONNECT_RUNNING;
}
extern "C" WifiConnectState Wifi_getConnectState(void) { return wifi.state; }
extern "C" bool Wifi_ensureConnected(SDL_Surface*, int) { return wifi.connected; }

extern "C" ToastToken Toast_show(const char* msg, uint32_t) {
    toasts.push_back(msg);
    return ToastToken{};
}
extern "C" ToastToken Toast_showScreenBound(const char* msg, uint32_t) {
    toasts.push_back(msg);
    return ToastToken{};
}
extern "C" bool Toast_isShowing(ToastToken) { return false; }
extern "C" void Toast_dismiss(ToastToken) {}
extern "C" void Toast_init(DisplayContext*) {}
extern "C" void Toast_quit(void) {}
extern "C" void Toast_tick(void) {}
extern "C" void Toast_screenChanged(void) {}

namespace {

class NoPlayback : public input::PlaybackControl {
public:
    void togglePlayback() override {}
    void skipTrack(bool) override {}
    void changeVolume(int) override {}
};

/** A page that opens the Wi-Fi dialog for a result, and keeps the result. */
class WaitingPage final : public UiPage {
public:
    using Result = std::monostate;
    explicit WaitingPage(std::optional<bool>& result) : result(result) {}
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

private:
    std::optional<bool>& result;
};

class WaitingPageImpl final : public UiPageImpl<WaitingPage> {
public:
    explicit WaitingPageImpl(std::optional<bool>& result) : UiPageImpl(PageKind::FullScreen), result(result) {}
    void onEnter() override {
        getNavigator().openForResult(WifiDialogPage{}, [this](const bool& connected) { result = connected; });
    }
    void layout(UiLayoutContext&) override {}

private:
    std::optional<bool>& result;
};

std::unique_ptr<UiPageImplBase> WaitingPage::createPageImpl() const {
    return std::make_unique<WaitingPageImpl>(result);
}

constexpr const char* kToast = "Internet connection required";

struct DialogFixture {
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    UiCompositor compositor;
    NoPlayback playback;
    PageStack stack{playback};
    std::optional<bool> result;
    uint32_t now = 0;

    DialogFixture() {
        wifi        = FakeWifi{};
        toasts.clear();
        dialogDrawn = false;
        compositor.setScreen(screen);
        stack.start(WaitingPage(result).createPageImpl(), compositor);
        stack.startFrame(now);
    }
    ~DialogFixture() {
        stack.closeAll();
        SDL_FreeSurface(screen);
    }

    /** Runs one frame of the loop, stepMs after the last one */
    void frame(uint32_t stepMs = 16) {
        stack.tickInput();
        stack.tickPages();
        stack.layout(compositor);
        if (compositor.isSurfaceOnly()) dialogDrawn = true;
        stack.endFrame();
        stack.beginPoll();
        now += stepMs;
        stack.startFrame(now);
    }

    /** Runs frames for `ms`, as long as the state of the probe takes to show */
    void waitMs(uint32_t ms) {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += 16) frame();
    }

    void tap(input::Button button) {
        stack.getRawInput().onRawEvent({now, input::event::ButtonPressed{button}});
        frame();
        stack.getRawInput().onRawEvent({now, input::event::ButtonReleased{button}});
        frame();
    }
};

}  // namespace

TEST(connected_device_shows_no_dialog) {
    wifi.connected = true;
    DialogFixture f;
    wifi.connected = true;
    f.frame();
    f.frame();
    CHECK(f.result == std::optional<bool>(true));
    CHECK(!dialogDrawn);
    CHECK_EQ_INT(wifi.startCalls, 0);
    CHECK_EQ_SZ(toasts.size(), 0);
}

TEST(dialog_draws_before_wifi_turns_on) {
    DialogFixture f;
    wifi.enabled = false;
    for (int i = 0; i < 4; i++) f.frame();
    CHECK_EQ_INT(wifi.enableCalls, 1);
    CHECK(wifi.drawnBeforeEnable);
    CHECK_EQ_INT(wifi.startCalls, 1);
    CHECK(wifi.startedJustEnabled);
    CHECK(!f.result.has_value());
}

TEST(wifi_that_is_on_does_not_turn_on_again) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    CHECK_EQ_INT(wifi.enableCalls, 0);
    CHECK_EQ_INT(wifi.startCalls, 1);
    CHECK(!wifi.startedJustEnabled);
}

TEST(connection_after_some_frames_gives_true) {
    DialogFixture f;
    f.waitMs(500);
    CHECK(!f.result.has_value());
    wifi.state = WIFI_CONNECT_CONNECTED;
    f.waitMs(1100);
    CHECK(f.result == std::optional<bool>(true));
    CHECK_EQ_SZ(toasts.size(), 0);
}

TEST(failed_probe_gives_false_and_shows_the_toast) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    wifi.state = WIFI_CONNECT_FAILED;
    f.waitMs(1100);
    CHECK(f.result == std::optional<bool>(false));
    CHECK_EQ_SZ(toasts.size(), 1);
    CHECK(toasts.size() == 1 && toasts[0] == kToast);
}

TEST(b_gives_false_and_shows_no_toast) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    f.tap(input::Button::B);
    f.frame();
    CHECK(f.result == std::optional<bool>(false));
    CHECK_EQ_SZ(toasts.size(), 0);
}

TEST(deadline_gives_false_while_the_probe_runs) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    for (int i = 0; i < 70; i++) f.frame(100);
    CHECK(!f.result.has_value());
    for (int i = 0; i < 20; i++) f.frame(100);
    CHECK(f.result == std::optional<bool>(false));
    CHECK(toasts.size() == 1 && toasts[0] == kToast);
}

TEST(deadline_reads_a_connection_that_came_after_the_last_check) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    for (int i = 0; i < 79; i++) f.frame(100);
    CHECK(!f.result.has_value());
    wifi.state = WIFI_CONNECT_CONNECTED;
    for (int i = 0; i < 3; i++) f.frame(100);
    CHECK(f.result == std::optional<bool>(true));
    CHECK_EQ_SZ(toasts.size(), 0);
}

TEST(sleep_does_not_count_and_the_first_frame_after_it_reads_the_probe) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    // A sleep of 60 seconds in the tick of a frame, in which the device connects
    wifi.state = WIFI_CONNECT_CONNECTED;
    f.stack.onWakeFromSleep();
    f.frame(60000);
    CHECK(f.result == std::optional<bool>(true));
    CHECK_EQ_SZ(toasts.size(), 0);
}

TEST(sleep_keeps_the_dialog_open) {
    DialogFixture f;
    for (int i = 0; i < 4; i++) f.frame();
    f.stack.onWakeFromSleep();
    f.frame(60000);
    for (int i = 0; i < 4; i++) f.frame();
    CHECK(!f.result.has_value());
    CHECK_EQ_SZ(toasts.size(), 0);
}

int main() {
    SDL_Init(0);
    if (TTF_Init() != 0) {
        printf("SKIP no TTF: %s\n", TTF_GetError());
        return 0;
    }
    CFG_setFontFile(NULL);
    Fonts_load();
    if (!Fonts_getMedium()) {
        printf("SKIP no font at %s\n", RES_PATH);
        return 0;
    }
    SDL_Surface* themeScreen = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
    Theme_init(themeScreen);
    RUN(connected_device_shows_no_dialog);
    RUN(dialog_draws_before_wifi_turns_on);
    RUN(wifi_that_is_on_does_not_turn_on_again);
    RUN(connection_after_some_frames_gives_true);
    RUN(failed_probe_gives_false_and_shows_the_toast);
    RUN(b_gives_false_and_shows_no_toast);
    RUN(deadline_gives_false_while_the_probe_runs);
    RUN(deadline_reads_a_connection_that_came_after_the_last_check);
    RUN(sleep_does_not_count_and_the_first_frame_after_it_reads_the_probe);
    RUN(sleep_keeps_the_dialog_open);
    return test_summary();
}
