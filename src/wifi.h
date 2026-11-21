#ifndef __WIFI_H__
#define __WIFI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>
#include <stdbool.h>

// Ensure WiFi is connected, enabling if necessary
// Returns true if connected, false otherwise. Returns false when another attempt runs for longer
// than the wait of this call.
// Shows "Connecting..." screen while waiting (if scr is not NULL)
// Can be called from background threads with scr=NULL to skip UI rendering
bool Wifi_ensureConnected(SDL_Surface* scr, int show_setting);

// Check if WiFi is currently connected
bool Wifi_isConnected(void);

// Returns true when Wi-Fi is on.
bool Wifi_isEnabled(void);

// Turns Wi-Fi on. Blocks until the platform started it. Call it from the main thread only, because
// it writes the configuration of the platform.
void Wifi_enable(void);

typedef enum {
    WIFI_CONNECT_IDLE,
    WIFI_CONNECT_RUNNING,
    WIFI_CONNECT_CONNECTED,
    WIFI_CONNECT_FAILED,
} WifiConnectState;

// Starts an attempt to connect to a saved network on a background thread, and returns at once.
// Wi-Fi must be on: turn it on from the main thread first, because that writes the configuration.
// Pass true in wifi_just_enabled when Wi-Fi turned on just before, thus the attempt waits for
// wpa_supplicant. Does nothing while an attempt runs.
void Wifi_startConnect(bool wifi_just_enabled);

// Returns the state of the last attempt that Wifi_startConnect() started.
WifiConnectState Wifi_getConnectState(void);

#ifdef __cplusplus
}
#endif

#endif
