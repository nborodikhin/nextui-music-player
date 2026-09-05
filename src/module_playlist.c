#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "help_screen.h"
#include "module_common.h"
#include "toast.h"
#include "module_playlist.h"
#include "module_player.h"
#include "playlist_m3u.h"
#include "playlist.h"
#include "keyboard.h"
#include "display_helper.h"
#include "ui_playlist.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "list_nav.h"

// Internal states
typedef enum {
    PLAYLIST_INTERNAL_LIST,
    PLAYLIST_INTERNAL_DETAIL
} PlaylistInternalState;

// List state
static PlaylistInfo playlists[MAX_PLAYLISTS];
static int playlist_count = 0;
static ListNav list_nav = {
    .selected = 0,
    .scroll = 0,
    .count = 0,
    .items_per_page = 1,
};

// Detail state
static PlaylistTrack detail_tracks[PLAYLIST_MAX_TRACKS];
static int detail_track_count = 0;
static ListNav detail_nav = {
    .selected = 0,
    .scroll = 0,
    .count = 0,
    .items_per_page = 1,
};
static int current_playlist_index = -1;  // Index into playlists[] for current detail view

// Confirmation dialog state
static bool show_confirm = false;
static char confirm_name[256] = "";
static int confirm_action = 0;  // 0 = delete playlist, 1 = remove track
static int confirm_target = -1;

static void refresh_playlists(void) {
    playlist_count = M3U_listPlaylists(playlists, MAX_PLAYLISTS);
}

static void refresh_detail(void) {
    if (current_playlist_index < 0 || current_playlist_index >= playlist_count) return;
    M3U_loadTracks(playlists[current_playlist_index].path, detail_tracks, PLAYLIST_MAX_TRACKS, &detail_track_count);
}

ModuleExitReason PlaylistModule_run(DisplayContext* display) {
    M3U_init();
    Keyboard_init();
    refresh_playlists();

    PlaylistInternalState state = PLAYLIST_INTERNAL_LIST;
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle confirmation dialog
        if (show_confirm) {
            if (PAD_justPressed(BTN_A)) {
                if (confirm_action == 0) {
                    // Delete playlist
                    int idx = confirm_target;
                    if (idx >= 0 && idx < playlist_count) {
                        M3U_delete(playlists[idx].path);
                        refresh_playlists();
                        ListNav_onItemRemoved(&list_nav, idx);
                        Toast_show("Playlist deleted", TOAST_DURATION);
                    }
                } else if (confirm_action == 1) {
                    // Remove track
                    int idx = confirm_target;
                    if (current_playlist_index >= 0 && current_playlist_index < playlist_count) {
                        M3U_removeTrack(playlists[current_playlist_index].path, idx);
                        refresh_detail();
                        // Update parent count
                        playlists[current_playlist_index].track_count = detail_track_count;
                        ListNav_onItemRemoved(&detail_nav, idx);
                        Toast_show("Track removed", TOAST_DURATION);
                    }
                }
                show_confirm = false;
                dirty = 1;
                continue;
            }
            if (PAD_justPressed(BTN_B)) {
                show_confirm = false;
                dirty = 1;
                continue;
            }
            // Render confirmation (dialog covers entire screen)
            const char* confirm_title = (confirm_action == 0) ? "Delete Playlist?" : "Remove Track?";
            render_confirmation_dialog(screen, confirm_name, confirm_title);
            GFX_flip(screen);
            GFX_sync();
            continue;
        }

        // Handle global input
        HelpId help_id = (state == PLAYLIST_INTERNAL_LIST) ? HELP_PLAYLIST_LIST : HELP_PLAYLIST_DETAIL;
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, help_id);
        if (global.should_quit) {
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        if (state == PLAYLIST_INTERNAL_LIST) {
            list_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&list_nav, playlist_count).moved) dirty = 1;

            if (PAD_justPressed(BTN_B)) {
                GFX_clearLayers(LAYER_SCROLLTEXT);
                return MODULE_EXIT_TO_MENU;
            }
            else if (ListNav_step(&list_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                // Enter playlist detail
                if (list_nav.selected >= 0 && list_nav.selected < playlist_count) {
                    current_playlist_index = list_nav.selected;
                    refresh_detail();
                    ListNav_scrollToTop(&detail_nav);
                    state = PLAYLIST_INTERNAL_DETAIL;
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_Y)) {
                // New Playlist
                char* name = Keyboard_open("Playlist name", MAX_PLAYLIST_NAME - 1);
                if (name && name[0]) {
                    if (M3U_create(name) == 0) {
                        Toast_show("Playlist created", TOAST_DURATION);
                        refresh_playlists();
                    } else {
                        Toast_show("Already exists", TOAST_DURATION);
                    }
                    free(name);
                } else if (name) {
                    free(name);
                }
                // The keyboard may have recreated the display - start a fresh frame.
                dirty = 1;
                continue;
            }
            else if (PAD_justPressed(BTN_X)) {
                // Delete playlist
                if (list_nav.selected >= 0 && list_nav.selected < playlist_count) {
                    snprintf(confirm_name, sizeof(confirm_name), "%s", playlists[list_nav.selected].name);
                    confirm_action = 0;
                    confirm_target = list_nav.selected;
                    show_confirm = true;
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    dirty = 1;
                }
            }

            // Animate scroll
            if (playlist_list_needs_scroll_refresh()) {
                playlist_list_animate_scroll();
            }
            if (playlist_list_scroll_needs_render()) dirty = 1;

        } else if (state == PLAYLIST_INTERNAL_DETAIL) {
            detail_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&detail_nav, detail_track_count).moved) dirty = 1;

            if (PAD_justPressed(BTN_B)) {
                GFX_clearLayers(LAYER_SCROLLTEXT);
                refresh_playlists();  // Refresh counts
                state = PLAYLIST_INTERNAL_LIST;
                dirty = 1;
            }
            else if (ListNav_step(&detail_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                if (detail_track_count > 0) {
                    // Play the playlist starting from selected track
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PlayerModule_setResumePlaylistPath(playlists[current_playlist_index].path);
                    PlayerModule_runWithPlaylist(display, detail_tracks, detail_track_count, detail_nav.selected);
                    PlayerModule_setResumePlaylistPath(NULL);
                    // On return, refresh and go back to detail
                    refresh_detail();
                    ListNav_reconcile(&detail_nav, detail_track_count);
                    // The player may have recreated the display - start a fresh frame.
                    dirty = 1;
                    continue;
                }
            }
            else if (PAD_justPressed(BTN_X)) {
                // Remove track
                if (detail_nav.selected >= 0 && detail_nav.selected < detail_track_count) {
                    snprintf(confirm_name, sizeof(confirm_name), "%s", detail_tracks[detail_nav.selected].name);
                    confirm_action = 1;
                    confirm_target = detail_nav.selected;
                    show_confirm = true;
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    dirty = 1;
                }
            }

            // Animate scroll
            if (playlist_list_needs_scroll_refresh()) {
                playlist_list_animate_scroll();
            }
            if (playlist_list_scroll_needs_render()) dirty = 1;
        }

        // Power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            // Bounds check: if current playlist was deleted externally, go back to list
            if (state == PLAYLIST_INTERNAL_DETAIL &&
                (current_playlist_index < 0 || current_playlist_index >= playlist_count)) {
                state = PLAYLIST_INTERNAL_LIST;
            }

            if (state == PLAYLIST_INTERNAL_LIST) {
                render_playlist_list(screen, show_setting, playlists, playlist_count, list_nav.selected, list_nav.scroll);
            } else {
                render_playlist_detail(screen, show_setting, playlists[current_playlist_index].name,
                                       detail_tracks, detail_track_count, detail_nav.selected, detail_nav.scroll);
            }

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
