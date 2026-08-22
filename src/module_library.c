#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "help_screen.h"
#include "module_common.h"
#include "toast.h"
#include "module_library.h"
#include "module_player.h"
#include "module_playlist.h"
#include "module_downloader.h"
#include "ui_utils.h"
#include "ui_fonts.h"
#include "list_nav.h"
#include "list_nav_pad.h"
#include "display_helper.h"

// Library submenu items
#define LIBRARY_FILES       0
#define LIBRARY_PLAYLISTS   1
#define LIBRARY_DOWNLOADER  2
#define LIBRARY_ITEM_COUNT  3

// Help state for controls dialog
static const char* library_items[] = {"Files", "Playlists", "Downloader"};

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
}

ModuleExitReason LibraryModule_run(DisplayContext* display) {
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
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle global input
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_LIBRARY_MENU);
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
                    reason = PlayerModule_run(display, false);  // File browser entry
                    break;
                case LIBRARY_PLAYLISTS:
                    reason = PlaylistModule_run(display);
                    break;
                case LIBRARY_DOWNLOADER:
                    reason = DownloaderModule_run(display);
                    break;
            }

            if (reason == MODULE_EXIT_QUIT) {
                return MODULE_EXIT_QUIT;
            }

            // Sub-module returned to library menu. Start a fresh frame: it may
            // have recreated the display, freeing this frame's surface.
            dirty = 1;
            continue;
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
        } else {
            GFX_sync();
        }
    }
}
