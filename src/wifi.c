#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "wifi.h"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_layers.h"
#include "module_common.h"

// WiFi connection timeout (in 500ms intervals)
#define WIFI_CONNECT_TIMEOUT_INTERVALS 10  // 5 seconds total

// Render a simple "Connecting..." screen. The wait for the network blocks
// after it, thus the screen presents its frame here.
static void render_connecting_screen(SDL_Surface* scr, int show_setting) {
    // The marquee of the list under it leaves the layer
    UiLayer_clear(UI_LAYER_ANIMATION);
    GFX_clear(scr);

    int hw = scr->w;
    int hh = scr->h;

    // Center the message
    const char* msg = "Connecting to WiFi...";
    SDL_Surface* text = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (text) {
        SDL_Rect text_at = {
            .x = (hw - text->w) / 2,
            .y = (hh - text->h) / 2,
        };
        SDL_BlitSurface(text, NULL, scr, &text_at);
        SDL_FreeSurface(text);
    }

    // This screen draws the status group at each width. The wifi indicator in it is
    // what reports the connection, thus the screen loses its subject without it.
    GFX_blitHardwareGroup(scr, show_setting);
    ModuleCommon_markSurfaceDrawn();
    ModuleCommon_frameEnd(scr);
}

// Check if WiFi is currently connected
bool Wifi_isConnected(void) {
    return PLAT_wifiEnabled() && PLAT_wifiConnected();
}

bool Wifi_isEnabled(void) {
    return PLAT_wifiEnabled();
}

void Wifi_enable(void) {
    PLAT_wifiEnable(true);
}

// The DHCP step stops udhcpc and starts it again, thus one attempt at a time runs the steps
static pthread_mutex_t steps_mutex = PTHREAD_MUTEX_INITIALIZER;

// The time that a blocking caller waits for an attempt that another caller runs
#define WIFI_STEPS_LOCK_TIMEOUT_MS 8000
#define WIFI_STEPS_LOCK_RETRY_MS   50

// The time that wpa_supplicant needs after Wi-Fi turns on
#define WIFI_SUPPLICANT_START_US 1000000

static void request_dhcp(void) {
    system("killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b 2>/dev/null &");
    // Wait briefly for DHCP to complete
    usleep(1500000);  // 1.5 seconds
}

// Connects to a saved network. Wi-Fi is on, and the caller holds steps_mutex. Draws the
// "Connecting..." screen in each step when scr is not NULL. These steps write no configuration.
static bool run_connect_steps(SDL_Surface* scr, int show_setting) {
    // Another attempt can have connected while this caller waited for the mutex
    if (PLAT_wifiConnected()) return true;

    // Enable all saved networks (they may be disabled by select_network)
    system("wpa_cli -p /etc/wifi/sockets -i wlan0 enable_network all > /dev/null 2>&1");

    // Trigger reconnect to saved networks
    system("wpa_cli -p /etc/wifi/sockets -i wlan0 reconnect > /dev/null 2>&1");

    // Wait for connection to a known network
    for (int i = 0; i < WIFI_CONNECT_TIMEOUT_INTERVALS; i++) {
        if (PLAT_wifiConnected()) {
            request_dhcp();
            return true;
        }
        usleep(500000);  // 500ms

        // Keep rendering and processing events while waiting (only if screen provided)
        if (scr) {
            PAD_poll();
            render_connecting_screen(scr, show_setting);
        }
    }

    // Final check
    if (PLAT_wifiConnected()) {
        request_dhcp();
        return true;
    }
    return false;
}

// Ensure WiFi is connected, enabling if necessary
// Returns true if connected, false otherwise
// Shows "Connecting..." screen while waiting (if scr is not NULL)
// Can be called from background threads with scr=NULL to skip UI rendering
bool Wifi_ensureConnected(SDL_Surface* scr, int show_setting) {
    // Already connected?
    if (Wifi_isConnected()) {
        return true;
    }

    // Only render if screen is provided (not from background thread)
    if (scr) {
        render_connecting_screen(scr, show_setting);
    }

    // If WiFi is disabled, enable it
    if (!PLAT_wifiEnabled()) {
        PLAT_wifiEnable(true);
        usleep(WIFI_SUPPLICANT_START_US);
    }

    // A probe of a page can hold the mutex in a command that does not return
    // pthread_mutex_timedlock() is not on every platform, thus the wait tries the lock in steps
    int waited_ms = 0;
    while (pthread_mutex_trylock(&steps_mutex) != 0) {
        if (waited_ms >= WIFI_STEPS_LOCK_TIMEOUT_MS) return false;
        usleep(WIFI_STEPS_LOCK_RETRY_MS * 1000);
        waited_ms += WIFI_STEPS_LOCK_RETRY_MS;
    }
    const bool connected = run_connect_steps(scr, show_setting);
    pthread_mutex_unlock(&steps_mutex);
    return connected;
}

static int connect_state = WIFI_CONNECT_IDLE;

static void* connect_thread(void* arg) {
    if (arg) usleep(WIFI_SUPPLICANT_START_US);
    pthread_mutex_lock(&steps_mutex);
    const bool connected = run_connect_steps(NULL, 0);
    pthread_mutex_unlock(&steps_mutex);
    __atomic_store_n(&connect_state, connected ? WIFI_CONNECT_CONNECTED : WIFI_CONNECT_FAILED, __ATOMIC_SEQ_CST);
    return NULL;
}

void Wifi_startConnect(bool wifi_just_enabled) {
    int state = __atomic_load_n(&connect_state, __ATOMIC_SEQ_CST);
    do {
        if (state == WIFI_CONNECT_RUNNING) return;
    } while (!__atomic_compare_exchange_n(&connect_state, &state, WIFI_CONNECT_RUNNING, false, __ATOMIC_SEQ_CST,
                                          __ATOMIC_SEQ_CST));

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    // A non-NULL argument asks the thread to wait for wpa_supplicant
    void* wait_for_supplicant = wifi_just_enabled ? (void*)&connect_state : NULL;
    if (pthread_create(&thread, &attr, connect_thread, wait_for_supplicant) != 0) {
        __atomic_store_n(&connect_state, WIFI_CONNECT_FAILED, __ATOMIC_SEQ_CST);
    }
    pthread_attr_destroy(&attr);
}

WifiConnectState Wifi_getConnectState(void) {
    return (WifiConnectState)__atomic_load_n(&connect_state, __ATOMIC_SEQ_CST);
}
