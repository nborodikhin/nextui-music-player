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
#include "ui_theme.h"
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
#include "test_control.h"

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

static void print_usage(const char* program) {
    printf(
        "Music Player for NextUI\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --test-control <in>[,<out>]  Read button commands from <in> and write the\n"
        "                               replies to <out>. Each side is 'std' for the\n"
        "                               standard streams, 'fd:<n>' for a descriptor\n"
        "                               that the caller opened, or the path of a file\n"
        "                               or a FIFO. Without <out> the replies go to\n"
        "                               standard output, with '@' before each one.\n"
        "                               One bidirectional descriptor is given twice,\n"
        "                               as in fd:3,fd:3. The form\n"
        "                               --test-control=<in> is also accepted.\n"
        "  -h, --help                   Give this text and stop.\n"
        "\n"
        "Commands of the control channel, one step for each line:\n"
        "  press(BTN)  press(BTN, n)  hold(BTN, ms)  hold(BTN, keep)  release(BTN)\n"
        "  wait(ms)  screenshot(path)  keep()  quit()\n"
        "\n"
        "BTN is UP, DOWN, LEFT, RIGHT, A, B, X, Y, START, SELECT, L1, R1, L2, R2,\n"
        "MENU, PLUS, MINUS or POWER.\n"
        "\n"
        "The app replies '@ok <line>' after each step, '@err <line> <message>' for a\n"
        "command that it cannot execute, and '@bye' before it stops. The end of a file\n"
        "or of a pipe stops the app, if the script did not give keep(). See README.md.\n",
        program);
}

int main(int argc, char* argv[]) {
    // Read the options before anything else, so --help gives its text without a
    // display, and an option that is not correct stops the app at once.
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strncmp(arg, "--test-control=", 15) == 0) {
            if (!TestControl_init(arg + 15)) return EXIT_FAILURE;
            continue;
        }
        if (strcmp(arg, "--test-control") == 0) {
            if (i + 1 >= argc) {
                LOG_error("--test-control needs a value\n");
                return EXIT_FAILURE;
            }
            if (!TestControl_init(argv[++i])) return EXIT_FAILURE;
            continue;
        }

        LOG_error("unknown option '%s', use --help for the options\n", arg);
        return EXIT_FAILURE;
    }

    SDL_Surface* const screen = GFX_init(MODE_MAIN);
    Theme_init(screen);
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
        SDL_Surface* title = TTF_RenderUTF8_Blended(
            Fonts_getTitle(), "Music Player", Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (title) {
            SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){
                (screen->w - title->w) / 2,
                screen->h / 2 - title->h
            });
            SDL_FreeSurface(title);
        }
        SDL_Surface* loading = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), "Loading...", Theme_getColor(THEME_ROLE_SECONDARY, false));
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
    KeyboardMap_quit();
    Fonts_unload();

    QuitSettings();
    TestControl_quit();
    PWR_quit();
    PAD_quit();
    GFX_quit();

    return EXIT_SUCCESS;
}
