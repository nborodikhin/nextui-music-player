#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "add_to_playlist.h"
#include "playlist.h"
#include "playlist_m3u.h"
#include "keyboard.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "module_common.h"
#include "display_helper.h"

// Internal state
static bool active = false;

static char** file_paths = NULL;
static int    file_count  = 0;

static PlaylistInfo playlists[MAX_PLAYLISTS];
static int playlist_count = 0;
static int selected = 0;
static int scroll = 0;

// Toast state (shown after adding)
static char toast_msg[128] = "";
static uint32_t toast_time = 0;

// Extract display name from a file path: basename without extension
// TODO: prefer the track name from file metadata (ID3 / Vorbis comment / etc.)
// over the filename when available; fall back to this only when metadata is missing.
static void display_name_from_path(const char* path, char* out, int out_size) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, out_size, "%s", base);
    char* dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

static void free_file_list(void) {
    Playlist_freePaths(file_paths, file_count);
    file_paths = NULL;
    file_count  = 0;
}

void AddToPlaylist_open(const char* path, const char* display_name) {
    if (!path) return;
    free_file_list();

    file_paths = malloc(sizeof(char*));
    if (!file_paths) return;
    file_paths[0] = strdup(path);
    if (!file_paths[0]) { free(file_paths); file_paths = NULL; return; }
    file_count = 1;

    (void)display_name;  // unused — display name always derived from path at add time

    M3U_init();
    playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);
    selected = 0;
    scroll = 0;
    active = true;
}

void AddToPlaylist_openDir(const char* dir_path) {
    if (!dir_path) return;
    free_file_list();

    file_count = Playlist_collectPaths(dir_path, &file_paths, 1000);
    if (file_count == 0) return;  // no audio files — no-op

    M3U_init();
    playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);
    selected = 0;
    scroll = 0;
    active = true;
}

bool AddToPlaylist_isActive(void) {
    return active;
}

int AddToPlaylist_handleInput(void) {
    if (!active) return 1;

    if (PAD_justPressed(BTN_B)) {
        free_file_list();
        active = false;
        return 1;
    }

    int total_items = playlist_count + 1;  // +1 for "New Playlist"

    if (PAD_justRepeated(BTN_UP)) {
        selected = (selected > 0) ? selected - 1 : total_items - 1;
    }
    else if (PAD_justRepeated(BTN_DOWN)) {
        selected = (selected < total_items - 1) ? selected + 1 : 0;
    }
    else if (PAD_justPressed(BTN_A)) {
        if (selected == 0) {
            // New Playlist
            DisplayHelper_prepareForExternal();
            char* name = Keyboard_open("Playlist name");
            PAD_poll(); PAD_reset();
            DisplayHelper_recoverDisplay();
            if (name && name[0]) {
                if (M3U_create(name) == 0) {
                    char new_path[512];
                    snprintf(new_path, sizeof(new_path), "%s/%s.m3u", PLAYLISTS_DIR, name);
                    int added = 0;
                    char dname[256];
                    for (int i = 0; i < file_count; i++) {
                        display_name_from_path(file_paths[i], dname, sizeof(dname));
                        if (M3U_addTrack(new_path, file_paths[i], dname) == 0) added++;
                    }
                    snprintf(toast_msg, sizeof(toast_msg), "Added %d/%d files to %s",
                             added, file_count, name);
                    toast_time = SDL_GetTicks();
                }
                free(name);
            }
            free_file_list();
            active = false;
            return 1;
        } else {
            // Existing playlist
            int idx = selected - 1;
            if (idx >= 0 && idx < playlist_count) {
                int added = 0;
                char dname[256];
                for (int i = 0; i < file_count; i++) {
                    display_name_from_path(file_paths[i], dname, sizeof(dname));
                    if (M3U_addTrack(playlists[idx].path, file_paths[i], dname) == 0) added++;
                }
                snprintf(toast_msg, sizeof(toast_msg), "Added %d/%d files to %s",
                         added, file_count, playlists[idx].name);
                toast_time = SDL_GetTicks();
            }
            free_file_list();
            active = false;
            return 1;
        }
    }

    return 0;
}

void AddToPlaylist_render(SDL_Surface* screen) {
    if (!active) return;

    int total_items = playlist_count + 1;
    int visible_items = 6;
    if (total_items < visible_items) visible_items = total_items;

    int line_height = SCALE1(22);
    DialogBox db = render_dialog_box(screen, SCALE1(260), SCALE1(70) + (visible_items * line_height));

    // Title with file count
    char title[64];
    snprintf(title, sizeof(title), "Add to Playlist: (%d %s)",
             file_count, file_count == 1 ? "file" : "files");
    SDL_Surface* title_surf = TTF_RenderUTF8_Blended(Fonts_getMedium(), title, COLOR_WHITE);
    if (title_surf) {
        SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){db.content_x, db.box_y + SCALE1(10)});
        SDL_FreeSurface(title_surf);
    }

    // Adjust scroll
    int items_per_page = visible_items;
    adjust_list_scroll(selected, &scroll, items_per_page);

    // List items
    int y_offset = db.box_y + SCALE1(35);
    for (int i = 0; i < items_per_page && (scroll + i) < total_items; i++) {
        int idx = scroll + i;
        bool is_selected = (idx == selected);

        const char* label;
        char buf[160];
        if (idx == 0) {
            label = "+ New Playlist";
        } else {
            PlaylistInfo* pl = &playlists[idx - 1];
            snprintf(buf, sizeof(buf), "%s (%d)", pl->name, pl->track_count);
            label = buf;
        }

        SDL_Color color = is_selected ? COLOR_WHITE : COLOR_GRAY;
        TTF_Font* font = Fonts_getSmall();

        // Selection indicator
        if (is_selected) {
            SDL_Rect sel_bg = {db.content_x - SCALE1(4), y_offset, db.content_w + SCALE1(8), line_height};
            render_rounded_rect_bg(screen, sel_bg.x, sel_bg.y, sel_bg.w, sel_bg.h,
                                   SDL_MapRGB(screen->format, 60, 60, 60));
        }

        // Truncate text if needed
        char truncated[160];
        GFX_truncateText(font, label, truncated, db.content_w, 0);

        SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font, truncated, color);
        if (text_surf) {
            SDL_BlitSurface(text_surf, NULL, screen, &(SDL_Rect){db.content_x, y_offset + SCALE1(2)});
            SDL_FreeSurface(text_surf);
        }

        y_offset += line_height;
    }

    // Scroll indicators
    if (scroll > 0) {
        SDL_Surface* up = TTF_RenderUTF8_Blended(Fonts_getTiny(), "...", COLOR_GRAY);
        if (up) {
            SDL_BlitSurface(up, NULL, screen, &(SDL_Rect){db.box_x + db.box_w - SCALE1(25), db.box_y + SCALE1(32)});
            SDL_FreeSurface(up);
        }
    }
    if (scroll + items_per_page < total_items) {
        SDL_Surface* dn = TTF_RenderUTF8_Blended(Fonts_getTiny(), "...", COLOR_GRAY);
        if (dn) {
            SDL_BlitSurface(dn, NULL, screen, &(SDL_Rect){db.box_x + db.box_w - SCALE1(25), db.box_y + db.box_h - SCALE1(18)});
            SDL_FreeSurface(dn);
        }
    }

    const char* hint = "A: Select   B: Cancel";
    SDL_Surface* hint_surf = TTF_RenderUTF8_Blended(Fonts_getSmall(), hint, COLOR_GRAY);
    if (hint_surf) {
        int hint_y = db.box_y + db.box_h - SCALE1(10) - hint_surf->h;
        SDL_BlitSurface(hint_surf, NULL, screen, &(SDL_Rect){(screen->w - hint_surf->w) / 2, hint_y});
        SDL_FreeSurface(hint_surf);
    }
}

// Get toast message and time (for callers to display after dialog closes)
const char* AddToPlaylist_getToastMessage(void) {
    return toast_msg;
}

uint32_t AddToPlaylist_getToastTime(void) {
    return toast_time;
}

void AddToPlaylist_clearToast(void) {
    toast_msg[0] = '\0';
    toast_time = 0;
}
