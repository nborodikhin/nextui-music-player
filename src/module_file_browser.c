#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "help_screen.h"
#include "module_common.h"
#include "module_file_browser.h"
#include "module_music_playing.h"
#include "music_session.h"
#include "spectrum.h"
#include "browser.h"
#include "ui_music.h"
#include "ui_layers.h"
#include "ui_main.h"
#include "add_to_playlist.h"
#include "ui_utils.h"
#include "background.h"
#include "db.h"
#include "filedb.h"
#include "file_utils.h"

// Module state
static BrowserContext browser = {0};
static bool initialized = false;

// Delete confirmation state
static bool show_delete_confirm = false;
static char delete_target_path[512] = "";
static char delete_target_name[256] = "";

// Navigation stack — push on A-into-folder, pop on B-back
typedef struct {
    int dir_id;
    int selected;
} NavEntry;
#define NAV_STACK_DEPTH 32

static NavEntry nav_stack[NAV_STACK_DEPTH];
static int      nav_stack_top = 0;

// Load a directory and trigger a refresh.
static void load_directory(int dir_id) {
    Browser_loadDirectory(&browser, dir_id);
    FileDb_scanDir(dir_id, false);
}

// Initialize the file browser module
static void init_browser(void) {
    if (initialized) return;
    mkdir(get_music_path(), 0755);
    load_directory(Db_getRootDirId());
    nav_stack_top = 0;
    initialized = true;
}

void FileBrowserModule_quit(void) {
    if (!initialized) return;

    Browser_freeEntries(&browser);
    initialized = false;
}

// Writes the absolute path of an indexed file to out. Returns false when the file has no row,
// or the path does not fit in out.
static bool indexed_file_path(int file_id, char* out, size_t out_size) {
    DbFileResult* result = Db_getFile(file_id);
    if (!result) return false;

    bool found = get_music_abspath(result->file.path, out, out_size);
    Db_freeResult(result);
    return found;
}

// Render delete confirmation dialog
static void render_delete_dialog(SDL_Surface* screen) {
    render_confirmation_dialog(screen, delete_target_name, NULL);
    ModuleCommon_markSurfaceDrawn();
}

// Goes to the parent of the current directory, which the ".." row of the list
// names. The cursor goes to the row that the user was on when they entered this
// directory, where the history knows it, and to the row of the directory that
// they left otherwise. A stale history, such as after a resume, is dropped.
static void browser_go_up(void) {
    int child_id = browser.dir_id;
    int parent_id = browser.is_root ? 0 : browser.entries[0].db_id;

    int sel = -1;
    if (nav_stack_top > 0 && nav_stack[nav_stack_top - 1].dir_id == parent_id) {
        nav_stack_top--;
        sel = nav_stack[nav_stack_top].selected;
    } else {
        nav_stack_top = 0;
    }

    load_directory(parent_id);
    // The browser shows the root when the parent is gone, thus the history is stale
    if (browser.dir_id != parent_id) {
        nav_stack_top = 0;
        sel = -1;
    }

    if (sel < 0) {
        sel = 0;
        for (int i = 0; i < browser.entry_count; i++) {
            if (browser.entries[i].is_dir && browser.entries[i].db_id == child_id) {
                sel = i;
                break;
            }
        }
    }
    if (sel >= browser.entry_count) sel = browser.entry_count > 0 ? browser.entry_count - 1 : 0;
    browser.selected = sel;
    int half = browser.items_per_page / 2;
    browser.scroll_offset = sel - half;
    if (browser.scroll_offset < 0) browser.scroll_offset = 0;
    int max_scroll = browser.entry_count - browser.items_per_page;
    if (max_scroll < 0) max_scroll = 0;
    if (browser.scroll_offset > max_scroll) browser.scroll_offset = max_scroll;
}

// Handles the input of the browser. Sets `*play` to true when a track of the session plays.
// Returns: true when the module exits to the menu
static bool handle_browser_input(int *dirty, bool *play) {
    if (PAD_justPressed(BTN_B)) {
        if (!browser.is_root) {
            browser_go_up();
            *dirty = 1;
        } else {
            UiLayer_clear(UI_LAYER_ANIMATION);
            if (!Background_isPlaying()) {
                Spectrum_quit();
                Browser_freeEntries(&browser);
            }
            return true;
        }
    }
    else if (browser.entry_count > 0) {
        if (PAD_justRepeated(BTN_UP)) {
            browser.selected = (browser.selected > 0) ? browser.selected - 1 : browser.entry_count - 1;
            *dirty = 1;
        }
        else if (PAD_justRepeated(BTN_DOWN)) {
            browser.selected = (browser.selected < browser.entry_count - 1) ? browser.selected + 1 : 0;
            *dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            FileEntry* entry = &browser.entries[browser.selected];
            if (entry->is_dir && strcmp(entry->name, "..") == 0) {
                // The parent row is the way up, the same as B
                browser_go_up();
                *dirty = 1;
            } else if (entry->is_dir) {
                if (nav_stack_top < NAV_STACK_DEPTH) {
                    nav_stack[nav_stack_top].dir_id = browser.dir_id;
                    nav_stack[nav_stack_top].selected = browser.selected;
                    nav_stack_top++;
                }
                load_directory(entry->db_id);
                *dirty = 1;
            } else if (MusicSession_startFolder(browser.dir_id,
                                                entry->is_play_all ? 0 : entry->db_id)) {
                *play = true;
            }
        }
        else if (PAD_justPressed(BTN_X)) {
            FileEntry* entry = &browser.entries[browser.selected];
            if (!entry->is_dir && !entry->is_play_all &&
                indexed_file_path(entry->db_id, delete_target_path,
                                  sizeof(delete_target_path))) {
                snprintf(delete_target_name, sizeof(delete_target_name), "%s", entry->name);
                show_delete_confirm = true;
                UiLayer_clear(UI_LAYER_ANIMATION);
                *dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_Y)) {
            FileEntry* entry = &browser.entries[browser.selected];
            // Inactive on parent-dir (".."): user must navigate up to add its contents.
            // Inactive on "Play All": it's a playback shortcut, not a target.
            if (entry->is_play_all || strcmp(entry->name, "..") == 0) {
                // no-op
            } else if (entry->is_dir) {
                AddToPlaylist_openDir(entry->db_id);
                *dirty = 1;
            } else {
                AddToPlaylist_open(entry->db_id);
                *dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_LEFT)) {
            list_page_up(&browser.selected, &browser.scroll_offset, browser.entry_count, browser.items_per_page);
            *dirty = 1;
        }
        else if (PAD_justPressed(BTN_RIGHT)) {
            list_page_down(&browser.selected, &browser.scroll_offset, browser.entry_count, browser.items_per_page);
            *dirty = 1;
        }
    }

    // Animate browser scroll
    if (browser_needs_scroll_refresh()) {
        browser_animate_scroll();
    }
    if (browser_scroll_needs_render()) *dirty = 1;

    return false;
}

ModuleExitReason FileBrowserModule_run(DisplayContext* display) {
    init_browser();
    load_directory(browser.dir_id);

    int dirty = 1;
    int show_setting = 0;
    uint32_t last_browser_refresh = SDL_GetTicks();
    // A path that changes while the title moves scrolls in, thus the marquee
    // does not jump on each directory.
    ScreenTitle_start(true);
    ModuleCommon_recordInputTime();

    while (1) {
        ModuleCommon_frameBegin(true);
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        uint32_t now = SDL_GetTicks();
        if (now - last_browser_refresh >= 100) {
            if (Browser_hasUpdate(&browser) && Browser_refresh(&browser)) dirty = 1;
            last_browser_refresh = now;
        }

        // Handle add-to-playlist dialog overlay
        if (AddToPlaylist_isActive()) {
            if (AddToPlaylist_handleInput()) {
                // Dialog closed — skip rest of input to avoid double-handling buttons
                dirty = 1;
                continue;
            }
            // Still active, render dialog (covers entire screen)
            AddToPlaylist_render(screen);
            ModuleCommon_markSurfaceDrawn();
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Handle delete confirmation dialog (module-specific)
        // A pending stop skips the dialog, thus the global input below reports the
        // quit and the module leaves through the path that it already has.
        if (show_delete_confirm && !ModuleCommon_stopSignalled()) {
            if (PAD_justPressed(BTN_A)) {
                if (unlink(delete_target_path) == 0) {
                    load_directory(browser.dir_id);
                    if (browser.selected >= browser.entry_count) {
                        browser.selected = browser.entry_count > 0 ? browser.entry_count - 1 : 0;
                    }
                }
            }
            if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) {
                delete_target_path[0] = '\0';
                delete_target_name[0] = '\0';
                show_delete_confirm = false;
                dirty = 1;
                continue;
            }
            // Render delete dialog
            render_delete_dialog(screen);
            ModuleCommon_frameEnd(screen);
            continue;
        }

        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_BROWSER);
        if (global.should_quit) {
            Browser_freeEntries(&browser);
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            ModuleCommon_frameEnd(screen);
            continue;
        }

        bool play = false;
        if (handle_browser_input(&dirty, &play)) {
            return MODULE_EXIT_TO_MENU;
        }
        if (play) {
            bool queue_ended = false;
            if (MusicPlayingModule_run(display, &queue_ended) == MODULE_EXIT_QUIT) {
                Browser_freeEntries(&browser);
                return MODULE_EXIT_QUIT;
            }
            if (queue_ended) {
                load_directory(Db_getRootDirId());
                nav_stack_top = 0;
            }
            // The playing screen may have recreated the display, thus a new frame starts
            ScreenTitle_start(true);
            dirty = 1;
            continue;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            render_browser(screen, show_setting, &browser);
            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ScreenTitle_frameEnd(&dirty, true);
        ModuleCommon_frameEnd(screen);
    }
}

