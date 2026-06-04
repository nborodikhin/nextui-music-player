#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_music_folders.h"
#include "music_folders.h"
#include "browser.h"
#include "ui_utils.h"
#include "ui_fonts.h"
#include "ui_main.h"  // render_confirmation_dialog

// Controls-help app states (match the convention used by other modules)
#define MF_HELP_STATE_LIST   56
#define MF_HELP_STATE_PICKER 57

typedef enum {
    MF_STATE_LIST,         // Manage configured folders
    MF_STATE_PICKER,       // Browse the SD card to add a folder
    MF_STATE_REMOVE_CONFIRM
} MfState;

static BrowserContext picker = {0};

// Toast
static char mf_toast[128] = "";
static uint32_t mf_toast_time = 0;
static void mf_setToast(const char* msg) {
    snprintf(mf_toast, sizeof(mf_toast), "%s", msg);
    mf_toast_time = SDL_GetTicks();
}

// ---- Folder picker (directories only) ----

static int compare_dir_entries(const void* a, const void* b) {
    return strcasecmp(((const FileEntry*)a)->name, ((const FileEntry*)b)->name);
}

// Truncate a path to its parent directory in place (no-op at root "/")
static void to_parent_dir(char* path) {
    char* slash = strrchr(path, '/');
    if (slash && slash != path) *slash = '\0';
}

// List subdirectories of `path` (plus ".." unless at the SD card root)
static void picker_load(const char* path) {
    Browser_freeEntries(&picker);
    snprintf(picker.current_path, sizeof(picker.current_path), "%s", path);
    picker.selected = 0;
    picker.scroll_offset = 0;

    bool at_root = (strcmp(path, SDCARD_PATH) == 0);

    DIR* dir = opendir(path);
    if (!dir) {
        LOG_error("Folder picker failed to open: %s\n", path);
        return;
    }

    int dir_count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char full[1024];
        if (snprintf(full, sizeof(full), "%s/%s", path, ent->d_name) >= (int)sizeof(full)) continue;
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) dir_count++;
    }

    int total = dir_count + (at_root ? 0 : 1);
    picker.entries = malloc(sizeof(FileEntry) * (total > 0 ? total : 1));
    if (!picker.entries) { closedir(dir); return; }

    int idx = 0;
    if (!at_root) {
        char parent[512];
        snprintf(parent, sizeof(parent), "%s", path);
        to_parent_dir(parent);
        snprintf(picker.entries[idx].name, sizeof(picker.entries[idx].name), "..");
        snprintf(picker.entries[idx].path, sizeof(picker.entries[idx].path), "%s", parent);
        picker.entries[idx].is_dir = true;
        picker.entries[idx].is_play_all = false;
        picker.entries[idx].format = AUDIO_FORMAT_UNKNOWN;
        idx++;
    }

    rewinddir(dir);
    while ((ent = readdir(dir)) != NULL && idx < total) {
        if (ent->d_name[0] == '.') continue;
        char full[1024];
        if (snprintf(full, sizeof(full), "%s/%s", path, ent->d_name) >= (int)sizeof(full)) continue;
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        snprintf(picker.entries[idx].name, sizeof(picker.entries[idx].name), "%s", ent->d_name);
        snprintf(picker.entries[idx].path, sizeof(picker.entries[idx].path), "%s", full);
        picker.entries[idx].is_dir = true;
        picker.entries[idx].is_play_all = false;
        picker.entries[idx].format = AUDIO_FORMAT_UNKNOWN;
        idx++;
    }
    closedir(dir);

    int sort_start = at_root ? 0 : 1;
    if (idx > sort_start + 1) {
        qsort(&picker.entries[sort_start], idx - sort_start, sizeof(FileEntry), compare_dir_entries);
    }
    picker.entry_count = idx;
}

// ---- Rendering ----

static void render_manager(SDL_Surface* screen, int show_setting, int selected, int* scroll) {
    GFX_clear(screen);
    render_screen_header(screen, "Music Folders", show_setting);

    ListLayout layout = calc_list_layout(screen);
    int count = MusicFolders_count();
    int total = count + 1;  // trailing "Add Folder"

    adjust_list_scroll(selected, scroll, layout.items_per_page);

    char truncated[256];
    char label[600];
    for (int i = 0; i < layout.items_per_page && *scroll + i < total; i++) {
        int idx = *scroll + i;
        bool sel = (idx == selected);
        int y = layout.list_y + i * layout.item_h;

        if (idx < count) {
            const char* path = MusicFolders_get(idx);
            if (!MusicFolders_existsAt(idx)) {
                snprintf(label, sizeof(label), "(missing) %s", path);
            } else {
                snprintf(label, sizeof(label), "%s", path);
            }
        } else {
            snprintf(label, sizeof(label), "+ Add Folder");
        }

        ListItemPos pos = render_list_item_pill(screen, &layout, label, truncated, y, sel, 0);
        int avail = pos.pill_width - SCALE1(BUTTON_PADDING * 2);
        render_list_item_text(screen, NULL, label, Fonts_getMedium(),
                              pos.text_x, pos.text_y, avail, sel);
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, total);

    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (selected >= count) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "OPEN", NULL}, 1, screen, 1);
    } else if (MusicFolders_isPinned(selected)) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "REMOVE", NULL}, 1, screen, 1);
    }

    render_toast(screen, mf_toast, mf_toast_time);
}

static void render_picker(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);
    render_screen_header(screen, "Add Folder", show_setting);

    ListLayout layout = calc_list_layout(screen);
    picker.items_per_page = layout.items_per_page;
    adjust_list_scroll(picker.selected, &picker.scroll_offset, picker.items_per_page);

    char truncated[256];
    for (int i = 0; i < picker.items_per_page && picker.scroll_offset + i < picker.entry_count; i++) {
        int idx = picker.scroll_offset + i;
        FileEntry* entry = &picker.entries[idx];
        bool sel = (idx == picker.selected);
        int y = layout.list_y + i * layout.item_h;

        char display[300];
        if (strcmp(entry->name, "..") == 0) {
            snprintf(display, sizeof(display), "..");
        } else {
            snprintf(display, sizeof(display), "[%s]", entry->name);
        }

        ListItemPos pos = render_list_item_pill(screen, &layout, display, truncated, y, sel, 0);
        int avail = pos.pill_width - SCALE1(BUTTON_PADDING * 2);
        render_list_item_text(screen, NULL, display, Fonts_getMedium(),
                              pos.text_x, pos.text_y, avail, sel);
    }

    render_scroll_indicators(screen, picker.scroll_offset, picker.items_per_page, picker.entry_count);

    // Use the left group for ADD FOLDER so the right group keeps A OPEN for nested browsing
    GFX_blitButtonGroup((char*[]){"X", "ADD FOLDER", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "OPEN", NULL}, 1, screen, 1);

    render_toast(screen, mf_toast, mf_toast_time);
}

// ---- Module loop ----

ModuleExitReason MusicFoldersModule_run(SDL_Surface* screen) {
    MfState state = MF_STATE_LIST;
    int selected = 0;
    int scroll = 0;
    int remove_index = -1;
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        GFX_startFrame();
        PAD_poll();

        int app_state = (state == MF_STATE_PICKER) ? MF_HELP_STATE_PICKER : MF_HELP_STATE_LIST;
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state);
        if (global.should_quit) {
            Browser_freeEntries(&picker);
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        int items_per_page = calc_list_layout(screen).items_per_page;

        switch (state) {
            case MF_STATE_LIST: {
                int count = MusicFolders_count();
                int total = count + 1;
                if (PAD_justRepeated(BTN_UP)) {
                    selected = (selected > 0) ? selected - 1 : total - 1;
                    dirty = 1;
                } else if (PAD_justRepeated(BTN_DOWN)) {
                    selected = (selected < total - 1) ? selected + 1 : 0;
                    dirty = 1;
                } else if (PAD_justPressed(BTN_LEFT)) {
                    list_page_up(&selected, &scroll, total, items_per_page);
                    dirty = 1;
                } else if (PAD_justPressed(BTN_RIGHT)) {
                    list_page_down(&selected, &scroll, total, items_per_page);
                    dirty = 1;
                } else if (PAD_justPressed(BTN_A)) {
                    if (selected >= count) {
                        // + Add Folder -> open the folder picker
                        picker_load(SDCARD_PATH);
                        state = MF_STATE_PICKER;
                        dirty = 1;
                    } else if (!MusicFolders_isPinned(selected)) {
                        // Remove the highlighted folder
                        remove_index = selected;
                        state = MF_STATE_REMOVE_CONFIRM;
                        dirty = 1;
                    }
                } else if (PAD_justPressed(BTN_B)) {
                    Browser_freeEntries(&picker);
                    return MODULE_EXIT_TO_MENU;
                }
                break;
            }

            case MF_STATE_PICKER: {
                if (PAD_justRepeated(BTN_UP)) {
                    if (picker.entry_count > 0)
                        picker.selected = (picker.selected > 0) ? picker.selected - 1 : picker.entry_count - 1;
                    dirty = 1;
                } else if (PAD_justRepeated(BTN_DOWN)) {
                    if (picker.entry_count > 0)
                        picker.selected = (picker.selected < picker.entry_count - 1) ? picker.selected + 1 : 0;
                    dirty = 1;
                } else if (PAD_justPressed(BTN_LEFT)) {
                    list_page_up(&picker.selected, &picker.scroll_offset, picker.entry_count, items_per_page);
                    dirty = 1;
                } else if (PAD_justPressed(BTN_RIGHT)) {
                    list_page_down(&picker.selected, &picker.scroll_offset, picker.entry_count, items_per_page);
                    dirty = 1;
                } else if (PAD_justPressed(BTN_A)) {
                    if (picker.entry_count > 0)
                        picker_load(picker.entries[picker.selected].path);
                    dirty = 1;
                } else if (PAD_justPressed(BTN_X)) {
                    // Add the highlighted folder
                    if (picker.entry_count > 0) {
                        FileEntry* sel = &picker.entries[picker.selected];
                        if (strcmp(sel->name, "..") == 0) {
                            // parent shortcut is not an addable folder
                        } else if (MusicFolders_add(sel->path)) {
                            mf_setToast("Folder added");
                            selected = 0;
                            scroll = 0;
                            Browser_freeEntries(&picker);
                            state = MF_STATE_LIST;
                        } else {
                            mf_setToast("Already added or overlaps a folder");
                        }
                        dirty = 1;
                    }
                } else if (PAD_justPressed(BTN_B)) {
                    // Up a level; at SD root, cancel back to the list
                    if (strcmp(picker.current_path, SDCARD_PATH) == 0) {
                        Browser_freeEntries(&picker);
                        state = MF_STATE_LIST;
                    } else {
                        char parent[512];
                        snprintf(parent, sizeof(parent), "%s", picker.current_path);
                        to_parent_dir(parent);
                        picker_load(parent);
                    }
                    dirty = 1;
                }
                break;
            }

            case MF_STATE_REMOVE_CONFIRM: {
                if (PAD_justPressed(BTN_A)) {
                    if (MusicFolders_removeAt(remove_index)) {
                        mf_setToast("Folder removed");
                    }
                    state = MF_STATE_LIST;
                    dirty = 1;
                } else if (PAD_justPressed(BTN_B)) {
                    state = MF_STATE_LIST;
                    dirty = 1;
                }
                break;
            }
        }

        ModuleCommon_PWR_update(&dirty, &show_setting);

        if (dirty) {
            switch (state) {
                case MF_STATE_PICKER:
                    render_picker(screen, show_setting);
                    break;
                case MF_STATE_REMOVE_CONFIRM:
                    render_manager(screen, show_setting, selected, &scroll);
                    render_confirmation_dialog(screen, MusicFolders_get(remove_index), "Remove Folder?");
                    break;
                default:
                    render_manager(screen, show_setting, selected, &scroll);
                    break;
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            ModuleCommon_tickToast(mf_toast, mf_toast_time, &dirty);
        } else {
            GFX_sync();
        }
    }
}
