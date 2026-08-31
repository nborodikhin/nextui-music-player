#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <msettings.h>

#include "psa/crypto.h"
#include "defines.h"
#include "api.h"
#include "utils.h"
#include "config.h"
#include "player.h"
#include "selfupdate.h"

// UI modules
#include "ui_fonts.h"
#include "keyboard_map.h"
#include "ui_keyboard.h"
#include "ui_icons.h"
#include "toast.h"

// Module architecture
#include "module_common.h"
#include "module_menu.h"
#include "module_library.h"
#include "module_player.h"
#include "module_radio.h"
#include "module_podcast.h"
#include "downloader.h"
#include "module_settings.h"
#include "settings.h"
#include "resume.h"
#include "background.h"
#include "display_helper.h"

// Global quit flag
static bool quit = false;
static DisplayContext* display;

static void sigHandler(int sig) {
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        quit = true;
        break;
    default:
        break;
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    SDL_Surface* const screen = GFX_init(MODE_MAIN);
    display = DisplayHelper_init(screen);
    Toast_init(display);
    PWR_pinToCores(CPU_CORE_PERFORMANCE);
    // Load bundled fonts
    Fonts_load();

    // The keyboard's characters are filtered by what the font can draw, so the
    // map is built once the fonts are up rather than on the first keyboard
    KeyboardMap_prepare(Fonts_hasGlyph, Fonts_getMedium());

    // Show splash screen immediately while heavy subsystems initialize
    {
        GFX_clear(screen);
        SDL_Surface* title = TTF_RenderUTF8_Blended(Fonts_getTitle(), "Music Player", COLOR_WHITE);
        if (title) {
            SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){
                (screen->w - title->w) / 2,
                screen->h / 2 - title->h
            });
            SDL_FreeSurface(title);
        }
        SDL_Surface* loading = TTF_RenderUTF8_Blended(Fonts_getSmall(), "Loading...", COLOR_GRAY);
        if (loading) {
            SDL_BlitSurface(loading, NULL, screen, &(SDL_Rect){
                (screen->w - loading->w) / 2,
                screen->h / 2 + SCALE1(4)
            });
            SDL_FreeSurface(loading);
        }
        GFX_flip(screen);
    }

    InitSettings();
    PAD_init();
    PWR_init();
    WIFI_init();
    psa_crypto_init();
    Icons_init();

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // Seed random number generator for shuffle
    srand((unsigned int)time(NULL));

	// Mute hardware before opening audio device to prevent amplifier pop on TG5050
	SetRawVolume(0);

    // Initialize player core
    if (Player_init() != 0) {
        LOG_error("Failed to initialize audio player\n");
        goto cleanup;
    }

    // At startup, set software volume based on output device
    if (Player_isBluetoothActive() || Player_isUSBDACActive()) {
        // Use cubic curve for perceptual volume (human hearing is logarithmic)
        float v = GetVolume() / 20.0f;
        Player_setVolume(v * v * v);
    } else {
        Player_setVolume(1.0f);
    }

	// Restore hardware volume after audio device is open and stable
	SetVolume(GetVolume());

    // Initialize self-update module
    SelfUpdate_init(".");

    // Initialize common module (global input handling)
    ModuleCommon_init();

    // Initialize app-specific settings
    Settings_init();

    // Startup update check is opt-out; About can still check on demand
    if (Settings_getAutoUpdateEnabled()) {
        SelfUpdate_checkForUpdate();
    }

    // Initialize resume state
    Resume_init();

    // Initialize YouTube downloader (loads queue, auto-resumes pending downloads)
    Downloader_init();

    // Main application loop
    while (!quit) {
        Toast_screenChanged();
        MenuSelection selection = MenuModule_run(display);

        if (selection == MENU_QUIT) {
            quit = true;
            continue;
        }

        // Run the selected module
        Toast_screenChanged();
        ModuleExitReason reason = MODULE_EXIT_TO_MENU;

        switch (selection) {
            case MENU_NOW_PLAYING:
                switch (Background_getActive()) {
                    case BG_MUSIC:
                        reason = PlayerModule_run(display, true);  // Now Playing entry
                        break;
                    case BG_RADIO:
                        reason = RadioModule_run(display);
                        break;
                    case BG_PODCAST:
                        reason = PodcastModule_run(display);
                        break;
                    default:
                        break;
                }
                break;

            case MENU_RESUME: {
                const ResumeState* rs = Resume_getState();
                if (rs) {
                    reason = PlayerModule_runResume(display, rs);
                }
                break;
            }
            case MENU_LIBRARY:
                reason = LibraryModule_run(display);
                break;
            case MENU_RADIO:
                reason = RadioModule_run(display);
                break;
            case MENU_PODCAST:
                reason = PodcastModule_run(display);
                break;
            case MENU_SETTINGS:
                reason = SettingsModule_run(display);
                break;

            case MENU_NONE:
            case MENU_QUIT:
                break;
        }

        if (reason == MODULE_EXIT_QUIT) {
            quit = true;
        }
    }

cleanup:
    Background_stopAll();
    Downloader_cleanup();
    Settings_quit();
    ModuleCommon_quit();
    SelfUpdate_cleanup();
    PlayerModule_quit();
    Player_quit();
    Toast_quit();
    Icons_quit();
    UIKeyboard_quit();
    Fonts_unload();

    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();

    return EXIT_SUCCESS;
}
