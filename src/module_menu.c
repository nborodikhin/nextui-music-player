#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "help_screen.h"
#include "module_common.h"
#include "module_menu.h"
#include "display_helper.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "resume.h"
#include "background.h"
#include "list_nav.h"
#include "menu_rows.h"
#include "toast.h"

// How long the exit prompt stays up, which is also how long the confirming
// press has to arrive: the prompt being on screen *is* the armed state.
#define EXIT_CONFIRM_WINDOW_MS 3000

// The exit prompt, while it is showing. TOAST_TOKEN_NONE = not armed.
static ToastToken exit_prompt = TOAST_TOKEN_NONE;

// How long to show the "Exiting..." toast before actually quitting.
// It has to be a few frames to ensure the text is drawn which is important since
// the last frame rendered by the app will stay visible after app exit
// until NextUI draws its first frame.
#define EXIT_TOAST_DELAY_MS 100

// The item the cursor was on when the menu was last left.
static MenuSelection last_selection = MENU_LIBRARY;

// State of the playback row. Now Playing wins over Resume when both apply.
static MenuSelection menu_playing_item(void) {
    if (Background_isPlaying()) return MENU_NOW_PLAYING;
    if (Resume_isAvailable())   return MENU_RESUME;
    return MENU_NONE;
}

MenuSelection MenuModule_run(DisplayContext* display) {
    ListNav nav = {
        .selected = 0,
        .scroll = 0,
        .count = 0,
        .items_per_page = 1,
    };

    // Row map as last rendered. count -1 so the first frame always differs.
    MenuRows prev_rows = {
        .playing_item = MENU_NONE,
        .count        = -1,
    };

    int dirty = 1;
    int show_setting = 0;
    int exiting = 0;

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle background player updates (track advancement, resume saving)
        Background_tick();
        if (Background_isPlaying()) {
            ModuleCommon_setAutosleepDisabled(true);
        }

        MenuRows rows = MenuRows_build(menu_playing_item());

        // The playback row appears and disappears on its own, so the map can
        // change on a frame with no input and no cursor move - which repaints
        // every row whether or not the cursor is one of them.
        if (!MenuRows_equal(&rows, &prev_rows)) dirty = 1;
        prev_rows = rows;

        // Note: play item may come and go, we need to check each frame.
        int row = MenuRows_isPlayItem(last_selection)
                      ? MenuRows_getPlayingItemRow(&rows)
                      : MenuRows_rowOf(&rows, last_selection);
        if (row >= 0 && row != nav.selected) {
            nav.selected = row;
            dirty = 1;
        }
        // A remembered item that is gone leaves the cursor where it was, which
        // reconcile then clamps into the new map.
        if (ListNav_reconcile(&nav, rows.count).moved) dirty = 1;
        last_selection = MenuRows_selectionAt(&rows, nav.selected);

        // Handle global input first (volume, START dialogs, power)
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_MAIN_MENU);
        if (global.should_quit) {
            return MENU_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // Menu navigation
        nav.items_per_page = calc_list_layout(screen).items_per_page;
        ListNavChange ch = ListNav_step(&nav, ListNavPad_read());
        if (ch.moved) {
            last_selection = MenuRows_selectionAt(&rows, nav.selected);
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            MenuSelection selection = MenuRows_selectionAt(&rows, nav.selected);
            if (selection == MENU_NONE) continue;  // cursor off the end; ignore
            GFX_clearLayers(LAYER_SCROLLTEXT);
            last_selection = selection;
            return selection;
        }
        else if (PAD_justPressed(BTN_X)) {
            MenuSelection sel = MenuRows_selectionAt(&rows, nav.selected);
            if (MenuRows_isPlayItem(sel)) {
                if (sel == MENU_NOW_PLAYING) {
                    Background_stopAll();
                } else {
                    Resume_clear();
                }
                GFX_clearLayers(LAYER_SCROLLTEXT);
                nav.selected = 0;
                last_selection = MenuRows_selectionAt(&rows, 0);
                dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_B)) {
            if (Toast_isShowing(exit_prompt)) {
                GFX_clearLayers(LAYER_SCROLLTEXT);
                exiting = 1;
                // Unbound: it has to outlive the menu as the last frame on screen.
                Toast_show("Exiting...", TOAST_DURATION);
            } else {
                exit_prompt = Toast_showScreenBound("Press B again to exit",
                                                    EXIT_CONFIRM_WINDOW_MS);
            }
            dirty = 1;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            render_menu(screen, show_setting, nav.selected, nav.scroll, &rows);

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            if (exiting) {
                // Give the "Exiting..." toast a moment on screen before NextUI
                // takes over and starts rendering its own UI.
                SDL_Delay(EXIT_TOAST_DELAY_MS);
                return MENU_QUIT;
            }
        } else {
            // Software scroll needs continuous redraws
            if (menu_needs_scroll_redraw()) dirty = 1;
            GFX_sync();
        }
    }
}
