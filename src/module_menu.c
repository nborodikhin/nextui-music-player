#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_menu.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "resume.h"
#include "background.h"
#include "crash_handler.h"
#include "settings.h"
#include "watchdog.h"
#include "log_trace.h"

// Toast message state
static char menu_toast_message[128] = "";
static uint32_t menu_toast_time = 0;

// Two-step exit state: timestamp of first B-press; 0 = not armed.
// Window equals TOAST_DURATION so the exit toast and the arm-window expire together.
static uint32_t exit_armed_at = 0;

// Crash Report dialog state.
static bool show_crash_dialog = false;
static int  crash_dialog_cursor = CRASH_DIALOG_ACTION_CLOSE;
static char crash_bundle_path[320] = "";

// Cached crash-row state. The filesystem is scanned exactly ONCE — the first
// time the app enters the menu — then this cache is reused for the rest of the
// process lifetime. The bundle set can't change underneath us (a crash restarts
// the app); only Skip / Never-collect hide the row, and both update the cache
// in-memory. Avoids any per-frame or per-entry SD-card I/O.
static bool crash_scan_done = false;
static bool crash_row_visible = false;

// How long to show the "Exiting..." toast before actually quitting.
// It has to be a few frames to ensure the text is drawn which is important since
// the last frame rendered by the app will stay visible after app exit
// until NextUI draws its first frame.
#define EXIT_TOAST_DELAY_MS 100

int MenuModule_run(SDL_Surface* screen) {
    LOG_trace("MenuModule_run: enter");
    int menu_selected = 0;
    int dirty = 1;
    int show_setting = 0;
    int exiting = 0;

    // Reset two-step exit arming on (re-)entry, so a stale arm from a previous
    // visit to the menu can't make a single B-press here read as the confirm.
    exit_armed_at = 0;

    // Scan the filesystem for the newest unsent crash bundle exactly once, the
    // first time the app reaches the menu. Subsequent menu entries reuse the
    // cached result (see crash_scan_done above). The scan also fills
    // crash_bundle_path with the bundle to operate on.
    if (!crash_scan_done) {
        crash_row_visible = CrashHandler_findUnsentBundle(
            crash_bundle_path, sizeof(crash_bundle_path));
        crash_scan_done = true;
    }

    while (1) {
        GFX_startFrame();
        PAD_poll();
        Watchdog_heartbeat();
        ModuleCommon_traceButtons();

        // Handle background player updates (track advancement, resume saving)
        Background_tick();
        if (Background_isPlaying()) {
            ModuleCommon_setAutosleepDisabled(true);
        }

        // Determine first item: Now Playing (if BG active) > Resume > none
        int first_item_mode = MENU_FIRST_NONE;
        if (Background_isPlaying()) {
            first_item_mode = MENU_FIRST_NOW_PLAYING;
        } else if (Resume_isAvailable()) {
            first_item_mode = MENU_FIRST_RESUME;
        }
        bool has_first = (first_item_mode != MENU_FIRST_NONE);

        // crash_row_visible is cached (scanned on entry, refreshed after
        // Skip / Never-collect) — see the scan above the loop.
        int item_count = (has_first ? 5 : 4) + (crash_row_visible ? 1 : 0);
        // Crash row, when present, sits immediately above Settings (the last row).
        int crash_row_index = crash_row_visible ? (item_count - 2) : -1;

        // Crash Report dialog has full input focus while visible.
        // TODO: like every other overlay dialog in the app, this block skips
        // ModuleCommon_handleGlobalInput() and ModuleCommon_PWR_update() while
        // open — so power button / volume / auto-screen-off don't work here.
        // App-wide convention; fix as a single refactor across all dialogs.
        // See spec/menu-behavior.md > "TODO: dialogs should respect power management".
        if (show_crash_dialog) {
            if (PAD_justRepeated(BTN_UP)) {
                crash_dialog_cursor = (crash_dialog_cursor > 0)
                    ? crash_dialog_cursor - 1
                    : CRASH_DIALOG_ACTION_COUNT - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                crash_dialog_cursor = (crash_dialog_cursor < CRASH_DIALOG_ACTION_COUNT - 1)
                    ? crash_dialog_cursor + 1
                    : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                LOG_trace("dialog exit: crash_report action=%d", crash_dialog_cursor);
                switch (crash_dialog_cursor) {
                    case CRASH_DIALOG_ACTION_CLOSE:
                        // No-op; just close.
                        break;
                    case CRASH_DIALOG_ACTION_SKIP:
                        CrashHandler_skipBundle(crash_bundle_path);
                        // Newest bundle is now skipped → hide the row. Update the
                        // cache in-memory — no filesystem rescan (scan-once design).
                        crash_row_visible = false;
                        break;
                    case CRASH_DIALOG_ACTION_NEVER_COLLECT:
                        Settings_setCollectCrashReports(false);
                        // Collection disabled → hide the row. In-memory, no rescan.
                        crash_row_visible = false;
                        break;
                }
                show_crash_dialog = false;
                crash_dialog_cursor = CRASH_DIALOG_ACTION_CLOSE;
                dirty = 1;
                continue;  // Let next iteration render the underlying menu cleanly.
            }
            else if (PAD_justPressed(BTN_B)) {
                LOG_trace("dialog exit: crash_report action=cancel(B)");
                show_crash_dialog = false;
                crash_dialog_cursor = CRASH_DIALOG_ACTION_CLOSE;
                dirty = 1;
                continue;
            }

            // Render dialog overlay.
            if (dirty) {
                render_menu(screen, show_setting, menu_selected,
                            menu_toast_message, menu_toast_time, first_item_mode,
                            crash_row_visible);
                render_crash_report_dialog(screen, crash_bundle_path, crash_dialog_cursor);
                if (show_setting) {
                    GFX_blitHardwareHints(screen, show_setting);
                }
                GFX_flip(screen);
                dirty = 0;
            } else {
                GFX_sync();
            }
            continue;
        }

        // Clamp cursor in case the visible row count just shrank.
        if (menu_selected >= item_count) menu_selected = item_count - 1;
        if (menu_selected < 0) menu_selected = 0;

        // Handle global input first (volume, START dialogs, power)
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 0);
        if (global.should_quit) {
            return MENU_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // Menu navigation
        int items_per_page = calc_list_layout(screen).items_per_page;
        if (PAD_justRepeated(BTN_UP)) {
            menu_selected = (menu_selected > 0) ? menu_selected - 1 : item_count - 1;
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justRepeated(BTN_DOWN)) {
            menu_selected = (menu_selected < item_count - 1) ? menu_selected + 1 : 0;
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_LEFT)) {
            int scroll = 0;
            list_page_up(&menu_selected, &scroll, item_count, items_per_page);
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_RIGHT)) {
            int scroll = 0;
            list_page_down(&menu_selected, &scroll, item_count, items_per_page);
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            GFX_clearLayers(LAYER_SCROLLTEXT);
            if (crash_row_index >= 0 && menu_selected == crash_row_index) {
                // Open the Crash Report dialog; do NOT return — stay in the menu.
                LOG_trace("dialog enter: crash_report bundle=%s", crash_bundle_path);
                show_crash_dialog = true;
                crash_dialog_cursor = CRASH_DIALOG_ACTION_CLOSE;
                dirty = 1;
                continue;  // Let the next iteration render the dialog overlay.
            } else {
                // Adjust selection to match MENU_* constants. Items below the
                // crash row need to be shifted back into the original index space.
                int original_idx = menu_selected;
                if (crash_row_index >= 0 && menu_selected > crash_row_index) {
                    original_idx -= 1;
                }
                int selection = original_idx;
                if (!has_first) selection += 1;  // Skip first-item slot
                return selection;
            }
        }
        else if (PAD_justPressed(BTN_X)) {
            // X applies to the first item only and only when one is shown.
            // (Doesn't apply to the conditional crash row.)
            if (has_first && menu_selected == 0) {
                if (first_item_mode == MENU_FIRST_NOW_PLAYING) {
                    // Stop background playback
                    Background_stopAll();
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    menu_selected = 0;
                    dirty = 1;
                } else if (first_item_mode == MENU_FIRST_RESUME) {
                    // Clear resume history
                    Resume_clear();
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    menu_selected = 0;
                    dirty = 1;
                }
            }
        }
        else if (PAD_justPressed(BTN_B)) {
            uint32_t now = SDL_GetTicks();
            if (exit_armed_at != 0 && now - exit_armed_at < TOAST_DURATION) {
                GFX_clearLayers(LAYER_SCROLLTEXT);
                exit_armed_at = 0;
                exiting = 1;
                snprintf(menu_toast_message, sizeof(menu_toast_message), "Exiting...");
            } else {
                // First press (or window expired) — arm and show toast
                exit_armed_at = now;
                snprintf(menu_toast_message, sizeof(menu_toast_message),
                         "Press B again to exit");
            }
            menu_toast_time = now;
            dirty = 1;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            render_menu(screen, show_setting, menu_selected,
                        menu_toast_message, menu_toast_time, first_item_mode,
                        crash_row_visible);

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

            // Keep refreshing while toast is visible
            ModuleCommon_tickToast(menu_toast_message, menu_toast_time, &dirty);
        } else {
            // Software scroll needs continuous redraws
            if (menu_needs_scroll_redraw()) dirty = 1;
            GFX_sync();
        }
    }
}

// Set toast message (called by modules that return to menu with a message)
void MenuModule_setToast(const char* message) {
    snprintf(menu_toast_message, sizeof(menu_toast_message), "%s", message);
    menu_toast_time = SDL_GetTicks();
}
