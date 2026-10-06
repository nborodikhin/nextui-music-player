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
#include "ui_theme.h"
#include "toast.h"

// Limit of the files that one directory adds to a playlist.
#define ADD_TO_PLAYLIST_MAX_FILES 1000

// Internal state
static bool active = false;

static PlaylistTrack* file_tracks = NULL;
static int             file_count = 0;

static PlaylistInfo playlists[MAX_PLAYLISTS];
static int playlist_count = 0;
static int selected = 0;
static int scroll = 0;

static void free_file_list(void) {
    free(file_tracks);
    file_tracks = NULL;
    file_count = 0;
}

void AddToPlaylist_open(int file_id) {
    if (file_id <= 0) return;
    free_file_list();

    file_tracks = calloc(1, sizeof(*file_tracks));
    if (!file_tracks) return;
    file_tracks[0].file_id = file_id;
    file_count = 1;

    M3U_init();
    playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);
    selected = 0;
    scroll = 0;
    active = true;
}

void AddToPlaylist_openDir(int dir_id) {
    if (dir_id < 0) return;
    free_file_list();

    file_count = Playlist_collectDirectory(dir_id, ADD_TO_PLAYLIST_MAX_FILES,
                                           &file_tracks);
    if (file_count <= 0) {
        free_file_list();
        return;
    }

    M3U_init();
    playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);
    selected = 0;
    scroll = 0;
    active = true;
}

bool AddToPlaylist_isActive(void) {
    return active;
}

// Appends the files of the dialog to a playlist. Returns the number of files added.
static int add_files_to_playlist(int playlist_id) {
    int* ids = calloc((size_t)file_count, sizeof(*ids));
    if (!ids) return 0;
    for (int i = 0; i < file_count; i++) ids[i] = file_tracks[i].file_id;
    int added = M3U_addTracks(playlist_id, ids, file_count);
    free(ids);
    return added;
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
            char* name = Keyboard_open("Playlist name", MAX_PLAYLIST_NAME - 1);
            if (name && name[0]) {
                int playlist_id = M3U_create(name);
                PlaylistInfo* info = M3U_getInfo(playlist_id);
                if (info) {
                    int added = add_files_to_playlist(playlist_id);
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Added %d/%d files to %s", added, file_count, info->name);
                    Toast_show(msg, TOAST_DURATION);
                }
                free(info);
                free(name);
            }
            free_file_list();
            active = false;
            return 1;
        } else {
            // Existing playlist
            int idx = selected - 1;
            if (idx >= 0 && idx < playlist_count) {
                int added = add_files_to_playlist(playlists[idx].id);
                char msg[128];
                snprintf(msg, sizeof(msg), "Added %d/%d files to %s",
                         added, file_count, playlists[idx].name);
                Toast_show(msg, TOAST_DURATION);
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
    SDL_Surface* title_surf = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), title, Theme_getColor(THEME_ROLE_PRIMARY, false));
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

        SDL_Color color = Theme_getColor(THEME_ROLE_SECONDARY, is_selected);
        TTF_Font* font = Fonts_getSmall();

        SDL_Rect sel_bg = {db.content_x - SCALE1(4), y_offset, db.content_w + SCALE1(8), line_height};
        draw_list_item_bg(screen, &sel_bg, is_selected);

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
        SDL_Surface* up = TTF_RenderUTF8_Blended(
            Fonts_getTiny(), "...", Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (up) {
            SDL_BlitSurface(up, NULL, screen, &(SDL_Rect){db.box_x + db.box_w - SCALE1(25), db.box_y + SCALE1(32)});
            SDL_FreeSurface(up);
        }
    }
    if (scroll + items_per_page < total_items) {
        SDL_Surface* dn = TTF_RenderUTF8_Blended(
            Fonts_getTiny(), "...", Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (dn) {
            SDL_BlitSurface(dn, NULL, screen, &(SDL_Rect){db.box_x + db.box_w - SCALE1(25), db.box_y + db.box_h - SCALE1(18)});
            SDL_FreeSurface(dn);
        }
    }

    const char* hint = "A: Select   B: Cancel";
    SDL_Surface* hint_surf = TTF_RenderUTF8_Blended(
        Fonts_getSmall(), hint, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (hint_surf) {
        int hint_y = db.box_y + db.box_h - SCALE1(10) - hint_surf->h;
        SDL_BlitSurface(hint_surf, NULL, screen, &(SDL_Rect){(screen->w - hint_surf->w) / 2, hint_y});
        SDL_FreeSurface(hint_surf);
    }
}
