#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_library.h"
#include "module_player.h"
#include "module_playlist.h"
#include "module_downloader.h"
#include "ui_utils.h"
#include "ui_fonts.h"
#include "list_nav.h"
#include "list_nav_pad.h"

// Library submenu items
#define LIBRARY_FILES       0
#define LIBRARY_PLAYLISTS   1
#define LIBRARY_DOWNLOADER  2
#define LIBRARY_ITEM_COUNT  3

// Help state for controls dialog
#define LIBRARY_MENU_HELP_STATE 55

static const char* library_items[] = {"Files", "Playlists", "Downloader"};

// Toast state
static char library_toast_message[128] = "";
static uint32_t library_toast_time = 0;

static void render_library_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                                int menu_scroll) {
    SimpleMenuConfig config = {
        .title = "Library",
        .items = library_items,
        .item_count = LIBRARY_ITEM_COUNT,
        .btn_b_label = "BACK",
        .get_label = NULL,
        .render_badge = NULL,
        .get_icon = NULL
    };
    render_simple_menu(screen, show_setting, menu_selected, menu_scroll, &config);
    render_toast(screen, library_toast_message, library_toast_time);
}

void LibraryModule_setToast(const char* message) {
    snprintf(library_toast_message, sizeof(library_toast_message), "%s", message);
    library_toast_time = SDL_GetTicks();
}

ModuleExitReason LibraryModule_run(SDL_Surface* screen) {
    ListNav nav = {
        .selected = 0,
        .scroll = 0,
        .count = LIBRARY_ITEM_COUNT,
        .items_per_page = 1,
    };
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        ModuleCommon_frameBegin();

        // Handle global input
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, LIBRARY_MENU_HELP_STATE);
        if (global.should_quit) {
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // Menu navigation
        nav.items_per_page = calc_list_layout(screen).items_per_page;
        if (ListNav_step(&nav, ListNavPad_read()).moved) {
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            ModuleExitReason reason = MODULE_EXIT_TO_MENU;

            switch (nav.selected) {
                case LIBRARY_FILES:
                    reason = PlayerModule_run(screen, false);  // File browser entry
                    break;
                case LIBRARY_PLAYLISTS:
                    reason = PlaylistModule_run(screen);
                    break;
                case LIBRARY_DOWNLOADER:
                    reason = DownloaderModule_run(screen);
                    break;
            }

            if (reason == MODULE_EXIT_QUIT) {
                return MODULE_EXIT_QUIT;
            }

            // Sub-module returned to library menu
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_B)) {
            return MODULE_EXIT_TO_MENU;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            render_library_menu(screen, show_setting, nav.selected, nav.scroll);

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            ModuleCommon_tickToast(library_toast_message, library_toast_time, &dirty);
        } else {
            GFX_sync();
        }
    }
}
