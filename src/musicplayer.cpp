#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include <msettings.h>
#include "defines.h"
#include "api.h"
#include "utils.h"
#include "config.h"
}

#include "psa/crypto.h"
#include "debug.h"
#include "error.h"
#include "player.h"
#include "selfupdate.h"

// UI modules
#include "ui_fonts.h"
#include "keyboard_map.h"
#include "keyboard.h"
#include "ui_keyboard.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include "toast.h"

// Module architecture
#include "module_common.h"
#include "ui/database_error_dialog_page.hpp"
#include "ui/main_menu_page.hpp"
#include "ui/page/page_stack.hpp"
#include "music_session.h"
#include "module_podcast.h"
#include "downloader.h"
#include "settings.h"
#include "resume.h"
#include "background.h"
#include "display_helper.h"
#include "spectrum.h"
#include "db.h"
#include "test_control.h"
#include "filedb.h"
#include "pad_reset.h"
#include "scoped_init.hpp"

static DisplayContext* display;

/** How many errors of each class the store keeps for the Errors page */
constexpr int kErrorCapacity = 10;

// A handler must call nothing that is not async-signal-safe, thus it only
// records the request. ModuleCommon_handleGlobalInput() acts on it, thus the
// module that is in operation returns and the app runs its cleanup.
static void sigHandler(int sig) {
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        ModuleCommon_signalStop();
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
        "Options:\n",
        program);
#ifdef TEST_CONTROL
    printf(
        "  --test-control <in>[,<out>]  Read button commands from <in> and write the\n"
        "                               replies to <out>. Each side is 'std' for the\n"
        "                               standard streams, 'fd:<n>' for a descriptor\n"
        "                               that the caller opened, or the path of a file\n"
        "                               or a FIFO. Without <out> the replies go to\n"
        "                               standard output, with '@' before each one.\n"
        "                               One bidirectional descriptor is given twice,\n"
        "                               as in fd:3,fd:3. The form\n"
        "                               --test-control=<in> is also accepted.\n");
#endif
    printf("  -h, --help                   Give this text and stop.\n");
#ifdef TEST_CONTROL
    printf(
        "\n"
        "Commands of the control channel, one step for each line:\n"
        "  press(BTN)  press(BTN, n)  hold(BTN, ms)  hold(BTN, keep)  release(BTN)\n"
        "  wait(ms)  wait_scan()  screenshot(path)  sql(sql)  new_display()\n"
        "  keep()  quit()\n"
        "\n"
        "BTN is UP, DOWN, LEFT, RIGHT, A, B, X, Y, START, SELECT, L1, R1, L2, R2,\n"
        "MENU, PLUS, MINUS, POWER, or a direction of the analog stick: STICK_UP,\n"
        "STICK_DOWN, STICK_LEFT or STICK_RIGHT.\n"
        "\n"
        "The app replies '@ok <line>' after each step, '@err <line> <message>' for a\n"
        "command that it cannot execute, and '@bye' before it stops. The end of a file\n"
        "or of a pipe stops the app, if the script did not give keep(). See README.md.\n");
#endif
}

int main(int argc, char* argv[]) {
    Debug_init();
    ScopedInit testControl{TestControl_quit};
    ScopedInit errors{[] { Error_init(kErrorCapacity); }, Error_quit};

    // Read the options before anything else, so --help gives its text without a
    // display, and an option that is not correct stops the app at once.
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
#ifdef TEST_CONTROL
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
#endif

        LOG_error("unknown option '%s', use --help for the options\n", arg);
        return EXIT_FAILURE;
    }

    ScopedInit gfx{[] { return GFX_init(MODE_MAIN); }, GFX_quit};
    SDL_Surface* const screen = gfx.value;
    Theme_init(screen);
    display = DisplayHelper_init(screen);
    ScopedInit toast{[] { Toast_init(display); }, Toast_quit};
    PWR_pinToCores(CPU_CORE_PERFORMANCE);
    // Load the font that the user selected in the settings of the system
    ScopedInit fonts{Fonts_load, Fonts_unload};

    // The keyboard's characters are filtered by what the font can draw, so the
    // map is built once the fonts are up rather than on the first keyboard
    ScopedInit keyboardMap{[] { KeyboardMap_prepare(Fonts_hasGlyph, Fonts_getMedium()); }, KeyboardMap_quit};

    // Show splash screen immediately while heavy subsystems initialize
    {
        GFX_clear(screen);
        SDL_Surface* title = TTF_RenderUTF8_Blended(
            Fonts_getTitle(), "Music Player", Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (title) {
            SDL_Rect at = {
                .x = (screen->w - title->w) / 2,
                .y = screen->h / 2 - title->h,
            };
            SDL_BlitSurface(title, nullptr, screen, &at);
            SDL_FreeSurface(title);
        }
        SDL_Surface* loading = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), "Loading...", Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (loading) {
            SDL_Rect at = {
                .x = (screen->w - loading->w) / 2,
                .y = screen->h / 2 + SCALE1(4),
            };
            SDL_BlitSurface(loading, nullptr, screen, &at);
            SDL_FreeSurface(loading);
        }
        // The init that follows blocks, thus the splash presents its frame here
        ModuleCommon_markSurfaceDrawn();
        ModuleCommon_frameEnd(screen);
    }

    ScopedInit systemSettings{InitSettings, QuitSettings};
    ScopedInit pad{PAD_init, PAD_quit};
    ScopedInit power{PWR_init, PWR_quit};
    WIFI_init();
    psa_crypto_init();
    ScopedInit icons{Icons_init, Icons_quit};
    ScopedInit keyboard{Keyboard_init, UIKeyboard_quit};

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // The common module does not use the database, and the dialog below needs it
    ScopedInit moduleCommon{ModuleCommon_init, ModuleCommon_quit};

    // The app does not run without its database. The dialog names the log, which gives the cause.
    ScopedInit database{Db_init, Db_quit};
    if (!database.value) {
        // A press during the splash is in the queue of the platform. The poll reads the queue, and
        // the reset drops what it read, thus that press does not close the dialog.
        PAD_poll();
        PadReset_run();
        PageStack::run(DatabaseErrorDialogPage{}, display);
        return EXIT_FAILURE;
    }

    // Start database-backed workers after schema and data migrations finish.
    ScopedInit fileDb{FileDb_start, FileDb_quit};

    ScopedInit settings{Settings_init, Settings_quit};

    // Seed random number generator for shuffle
    srand((unsigned int)time(nullptr));

	// Mute hardware before opening audio device to prevent amplifier pop on TG5050
	SetRawVolume(0);

    // Initialize player core
    if (Player_init() != 0) {
        LOG_error("Failed to initialize audio player\n");
        return EXIT_FAILURE;
    }
    ScopedInit player{Player_quit};
    ScopedInit musicSession{MusicSession_quit};
    ScopedInit background{Background_stopAll};

    // Restore the volume on the mixer of the output, after the audio device is open and stable
    Player_syncOutputVolume();

    // Initialize self-update module
    ScopedInit selfUpdate{[] { SelfUpdate_init("."); }, SelfUpdate_cleanup};

    // Initialize resume state
    Resume_init();

    // Initialize YouTube downloader (loads queue, auto-resumes pending downloads)
    ScopedInit downloader{Downloader_init, Downloader_cleanup};

    // The app runs one page stack, with Main at the bottom, until the user quits
    PageStack::run(MainMenuPage{}, display);

    return EXIT_SUCCESS;
}
