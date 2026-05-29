#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

// Render the main menu (first_item_mode: 0=none, 1=Resume, 2=Now Playing).
// show_crash_row: when true, inserts a "Send Crash Report" row immediately
// above Settings.
void render_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                 char* toast_message, uint32_t toast_time, int first_item_mode,
                 bool show_crash_row);

// Render confirmation dialog overlay (title + optional content + "A: Yes  B: No")
void render_confirmation_dialog(SDL_Surface* screen, const char* content, const char* title);

// Crash Report dialog actions (cursor positions, in display order).
typedef enum {
    CRASH_DIALOG_ACTION_CLOSE = 0,
    CRASH_DIALOG_ACTION_SKIP,
    CRASH_DIALOG_ACTION_NEVER_COLLECT,
    CRASH_DIALOG_ACTION_COUNT
} CrashDialogAction;

// Render the Crash Report dialog overlay. bundle_path is the absolute path of
// the unsent bundle; the dialog displays it with SDCARD root prefix stripped.
// selected_action is the cursor index (0..CRASH_DIALOG_ACTION_COUNT-1).
void render_crash_report_dialog(SDL_Surface* screen, const char* bundle_path,
                                int selected_action);

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, int app_state);

// Render screen off hint message
void render_screen_off_hint(SDL_Surface* screen);

// Check if Resume scroll needs continuous redraw (software scroll mode)
bool menu_needs_scroll_redraw(void);

#endif
