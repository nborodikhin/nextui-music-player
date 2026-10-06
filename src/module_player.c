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
#include "module_player.h"
#include "player.h"
#include "spectrum.h"
#include "browser.h"
#include "playlist.h"
#include "ui_music.h"
#include "ui_music_playing.h"
#include "ui_layers.h"
#include "ui_album_art.h"
#include "ui_main.h"
#include "lyrics.h"
#include "settings.h"
#include "add_to_playlist.h"
#include "display_helper.h"
#include "ui_utils.h"
#include "resume.h"
#include "playlist_m3u.h"
#include "background.h"
#include "album_art.h"
#include "db.h"
#include "filedb.h"
#include "file_utils.h"

// Internal states
typedef enum {
    PLAYER_INTERNAL_BROWSER,
    PLAYER_INTERNAL_PLAYING
} PlayerInternalState;

// Module state
static BrowserContext browser = {0};
static bool shuffle_enabled = false;
static bool repeat_enabled = false;
static PlaylistContext playlist = {0};
static bool playlist_active = false;
static bool initialized = false;

// Delete confirmation state
static bool show_delete_confirm = false;
static char delete_target_path[512] = "";
static char delete_target_name[256] = "";

// Screen off state (module-local)
static bool screen_off = false;

// Navigation stack — push on A-into-folder, pop on B-back
typedef struct {
    int dir_id;
    int selected;
} NavEntry;
#define NAV_STACK_DEPTH 32

// The seek of a short press of LEFT or RIGHT
#define MUSIC_SEEK_STEP_MS 5000
static NavEntry nav_stack[NAV_STACK_DEPTH];
static int      nav_stack_top = 0;

// Resume: playlist id while a playlist is playing.
static int resume_playlist_id = 0;

// Resume: last save timestamp for periodic updates
static uint32_t last_resume_save = 0;
static int pending_resume_position = 0;

// A display recreate drops every GPU layer. The next frame paints each one
// again, whatever the caches of the screen say.
static void display_recreated(void) {
    MusicPlaying_invalidate();
}

// Load a directory and trigger a refresh.
static void load_directory(int dir_id) {
    Browser_loadDirectory(&browser, dir_id);
    FileDb_scanDir(dir_id, false);
}

static void save_resume_position(void) {
    PlayerState state = Player_getState();
    if (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_PAUSED) {
        Resume_updatePosition(Player_getPosition());
    }
}

// Initialize player module
static void init_player(void) {
    if (initialized) return;
    mkdir(get_music_path(), 0755);
    load_directory(Db_getRootDirId());
    nav_stack_top = 0;
    DisplayHelper_addRecreatedCallback(display_recreated);
    initialized = true;
}

void PlayerModule_quit(void) {
    if (!initialized) return;

    DisplayHelper_removeRecreatedCallback(display_recreated);
    Browser_freeEntries(&browser);
    Playlist_free(&playlist);
    initialized = false;
}

// Loads and plays the file at path. Pass the id of its index row in file_id, for the resume
// record. Returns true on success.
static bool try_load_and_play(const char *path, int file_id) {
    if (Player_load(path) == 0) {
        Player_play();
        const TrackInfo* info = Player_getTrackInfo();

        // Fetch album art (async) and lyrics after playback starts
        if (info && !Player_getAlbumArt()) {
            const char* artist = info->artist[0] ? info->artist : "";
            const char* title = info->title[0] ? info->title : "";
            if (artist[0] || title[0]) {
                album_art_fetch(artist, title);
            }
        }
        // The lyrics belong to the track, thus a new track takes the old ones
        // away, and fetches its own where the lyrics are on
        Lyrics_clear();
        if (Settings_getBool(&SETTING_LYRICS_ENABLED) && info) {
            Lyrics_fetch(info->artist, info->title, info->duration_ms / 1000,
                         Player_getCurrentFile(), Player_getEmbeddedLyrics());
        }

        if (pending_resume_position > 0) {
            Player_seek(pending_resume_position);
            pending_resume_position = 0;
        }

        // Save resume state on every track change.
        char track_name[256];
        if (info && info->title[0]) {
            snprintf(track_name, sizeof(track_name), "%s", info->title);
        } else {
            const char* slash = strrchr(path, '/');
            get_file_display_name(slash ? slash + 1 : path, track_name, sizeof(track_name));
        }
        int position_ms = Player_getPosition();
        if (resume_playlist_id > 0 && playlist_active) {
            Resume_savePlaylist(resume_playlist_id, file_id, track_name, position_ms);
        } else {
            Resume_saveFiles(browser.dir_id, file_id, track_name, position_ms);
        }
        last_resume_save = SDL_GetTicks();

        return true;
    }
    return false;
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

// Loads and plays an indexed file. Returns true on success.
static bool try_play_file(int file_id) {
    char path[1024];
    return indexed_file_path(file_id, path, sizeof(path)) && try_load_and_play(path, file_id);
}

// Try to play a playlist track by index (-1 means current). Returns true on success.
static bool playlist_try_play(int idx) {
    const PlaylistTrack* track = (idx < 0)
        ? Playlist_getCurrentTrack(&playlist)
        : Playlist_getTrack(&playlist, idx);
    return track && try_load_and_play(track->path, track->file_id);
}

// Pick a random audio file from the browser (excluding current). Returns true on success.
static bool browser_pick_random(void) {
    int audio_count = browser.audio_count;
    if (audio_count <= 1) return false;

    int random_idx = rand() % (audio_count - 1);
    int count = 0;
    for (int i = 0; i < browser.entry_count; i++) {
        if (!browser.entries[i].is_dir && !browser.entries[i].is_play_all &&
            i != browser.selected) {
            if (count == random_idx) {
                browser.selected = i;
                return try_play_file(browser.entries[i].db_id);
            }
            count++;
        }
    }
    return false;
}

// Pick the next audio file in the browser after current. Returns true on success.
static bool browser_pick_next(void) {
    for (int i = browser.selected + 1; i < browser.entry_count; i++) {
        if (!browser.entries[i].is_dir && !browser.entries[i].is_play_all) {
            browser.selected = i;
            return try_play_file(browser.entries[i].db_id);
        }
    }
    return false;
}

// Handle next track logic
static bool handle_track_ended(void) {
    if (repeat_enabled) {
        if (playlist_active) return playlist_try_play(-1);
        return try_play_file(browser.entries[browser.selected].db_id);
    }

    if (shuffle_enabled) {
        if (playlist_active) return playlist_try_play(Playlist_shuffle(&playlist));
        return browser_pick_random();
    }

    if (playlist_active) {
        int next_idx = Playlist_next(&playlist);
        if (next_idx < 0) return false;  // End of playlist
        return playlist_try_play(next_idx);
    }
    return browser_pick_next();
}

// The layers of the playing screen, once for each frame of the loop. A frame
// that redraws the screen paints them after that redraw, thus this one leaves
// them to the render block.
static void refresh_gpu_layers(int* dirty) {
    if (ModuleCommon_isScreenOffHintActive()) return;
    if (MusicPlaying_frame(*dirty != 0)) *dirty = 1;
}

// Start playback of a track (load + play + init spectrum)
static bool start_playback(const char* path, int file_id) {
    // Stop any other background player before starting music playback
    if (Background_getActive() != BG_MUSIC) {
        Background_stopAll();
    }
    if (try_load_and_play(path, file_id)) {
        Spectrum_init();
        ModuleCommon_recordInputTime();
        ModuleCommon_setAutosleepDisabled(true);
        return true;
    }
    return false;
}

// Clean up playback state. Pass true in `quit_spectrum` where the playing screen
// goes as well, thus the spectrum releases its FFT and its layer.
static void cleanup_playback(bool quit_spectrum) {
    MusicPlaying_leave();
    Lyrics_clear();
    if (quit_spectrum) {
        Spectrum_quit();
    }
    Playlist_free(&playlist);
    playlist_active = false;
    ModuleCommon_setAutosleepDisabled(false);
}

// Clean up playback UI only (audio keeps playing in background). The lyrics
// stay with the track, thus the screen shows them again on the way back.
static void cleanup_playback_ui(void) {
    MusicPlaying_leave();
    Spectrum_quit();
}

// Build a playlist from a directory and start playing the first track
static bool build_and_start_playlist(int dir_id, int start_file_id) {
    resume_playlist_id = 0;
    Playlist_free(&playlist);
    int track_count = Playlist_buildFromDirectory(&playlist, dir_id, start_file_id);
    if (track_count > 0) {
        playlist_active = true;
        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
        if (track && start_playback(track->path, track->file_id)) {
            return true;
        }
    }
    return false;
}

// Render delete confirmation dialog
static void render_delete_dialog(SDL_Surface* screen) {
    render_confirmation_dialog(screen, delete_target_name, NULL);
    ModuleCommon_markSurfaceDrawn();
}

// Handle USB/Bluetooth media button events
static void handle_hid_events(void) {
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
            Player_togglePause();
        } else if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
            PlayerModule_nextTrack();
        } else if (hid_event == USB_HID_EVENT_PREV_TRACK) {
            PlayerModule_prevTrack();
        } else {
            ModuleCommon_handleHIDVolume(hid_event);
        }
    }
}

// Try to start playback from a browser entry (play-all or single file). Returns true on success.
static bool browser_play_entry(FileEntry *entry) {
    if (entry->is_play_all)
        return build_and_start_playlist(browser.dir_id, 0);
    if (build_and_start_playlist(browser.dir_id, entry->db_id))
        return true;
    playlist_active = false;
    char path[1024];
    return indexed_file_path(entry->db_id, path, sizeof(path)) &&
           start_playback(path, entry->db_id);
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

// Handle input in browser state. Returns true if module should exit to menu.
static bool handle_browser_input(PlayerInternalState *state, int *dirty) {
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
            } else if (browser_play_entry(entry)) {
                *state = PLAYER_INTERNAL_PLAYING;
                *dirty = 1;
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

// Handle input in playing state. Returns true when main loop should continue (skip render).
static bool handle_playing_input(SDL_Surface *screen, PlayerInternalState *state, int *dirty) {
    // Handle screen off hint
    if (ModuleCommon_isScreenOffHintActive()) {
        handle_hid_events();
        ModuleCommon_handleHardwareVolume();
        Player_update();

        // SELECT+A during hint -> full wake
        if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
            ModuleCommon_resetScreenOffHint();
            ModuleCommon_recordInputTime();
            *dirty = 1;
            return true;  // skip this frame's input so A doesn't toggle pause
        } else {
            // Any other button resets the hint timer
            if (PAD_anyPressed()) {
                ModuleCommon_startScreenOffHint();
            }
            if (ModuleCommon_processScreenOffHintTimeout()) {
                screen_off = true;
                GFX_clear(screen);
                ModuleCommon_markSurfaceDrawn();
            }
            ModuleCommon_frameEnd(screen);
            return true;
        }
    }

    // Handle screen off
    if (screen_off) {
        handle_hid_events();
        ModuleCommon_handleHardwareVolume();
        Player_update();

        // Any button -> show hint
        if (PAD_anyPressed()) {
            screen_off = false;
            PLAT_enableBacklight(1);
            ModuleCommon_startScreenOffHint();
            GFX_clear(screen);
            render_screen_off_hint(screen);
            ModuleCommon_markSurfaceDrawn();
        }

        if (Player_getState() == PLAYER_STATE_STOPPED) {
            if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
                Resume_clear();
                screen_off = false;
                PLAT_enableBacklight(1);
                cleanup_playback(false);
                load_directory(Db_getRootDirId());
                nav_stack_top = 0;
                *state = PLAYER_INTERNAL_BROWSER;
                *dirty = 1;
            }
        }
        ModuleCommon_frameEnd(screen);
        return true;
    }

    // Normal input handling
    if (PAD_anyPressed()) {
        ModuleCommon_recordInputTime();
    }

    if (ModuleCommon_updateSeekScan(MUSIC_SEEK_STEP_MS, MUSIC_SEEK_STEP_MS)) {
        *dirty = 1;
    }

    if (PAD_justPressed(BTN_A)) {
        Player_togglePause();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_B)) {
        save_resume_position();
        cleanup_album_art_background();
        if (Player_getState() == PLAYER_STATE_PLAYING) {
            cleanup_playback_ui();
            Background_setActive(BG_MUSIC);
        } else {
            Player_stop();
            cleanup_playback(true);
        }
        *state = PLAYER_INTERNAL_BROWSER;
        *dirty = 1;
        return true;  // Skip track-ended check to prevent auto-advance
    }
    else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
        PlayerModule_prevTrack();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
        PlayerModule_nextTrack();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_X)) {
        shuffle_enabled = !shuffle_enabled;
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_Y)) {
        repeat_enabled = !repeat_enabled;
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
        Spectrum_cycleNext();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_R3) || PAD_justPressed(BTN_R2)) {
        // Lyrics that are off hide, and keep their data. Lyrics that come back
        // fetch only where the track has none.
        if (Settings_toggleBool(&SETTING_LYRICS_ENABLED)) {
            const TrackInfo* info = Player_getTrackInfo();
            if (info) {
                Lyrics_fetch(info->artist, info->title, info->duration_ms / 1000,
                             Player_getCurrentFile(), Player_getEmbeddedLyrics());
            }
        }
        *dirty = 1;
    }
    else if (PAD_tappedSelect(SDL_GetTicks())) {
        ModuleCommon_startScreenOffHint();
        MusicPlaying_leave();
        *dirty = 1;
    }

    // Check if track ended
    Player_update();
    if (Player_getState() == PLAYER_STATE_STOPPED) {
        if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
            Resume_clear();
            cleanup_playback(false);
            load_directory(Db_getRootDirId());
            nav_stack_top = 0;
            *state = PLAYER_INTERNAL_BROWSER;
        }
        *dirty = 1;
    }

    // Save resume position periodically
    if (Player_getState() == PLAYER_STATE_PLAYING) {
        uint32_t now = SDL_GetTicks();
        if (now - last_resume_save > 5000) {
            Resume_updatePosition(Player_getPosition());
            last_resume_save = now;
        }
    }

    // Auto screen-off after inactivity
    if (Player_getState() == PLAYER_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
        MusicPlaying_leave();
        *dirty = 1;
    }

    // Re-render when async album art fetch completes
    if (album_art_is_fetching()) *dirty = 1;

    refresh_gpu_layers(dirty);

    return false;
}

ModuleExitReason PlayerModule_run(DisplayContext* display, bool now_playing_entry) {
    init_player();
    load_directory(browser.dir_id);

    PlayerInternalState state = PLAYER_INTERNAL_BROWSER;
    int dirty = 1;
    int show_setting = 0;
    uint32_t last_browser_refresh = SDL_GetTicks();
    // A path that changes while the title moves scrolls in, thus the marquee
    // does not jump on each directory.
    ScreenTitle_start(true);

    screen_off = false;
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();

    // Reclaim background music — re-enter playing state (only when entered via Now Playing)
    bool entered_via_now_playing = false;
    if (now_playing_entry && Background_getActive() == BG_MUSIC && PlayerModule_isActive()) {
        Background_setActive(BG_NONE);
        Spectrum_init();
        ModuleCommon_setAutosleepDisabled(true);
        state = PLAYER_INTERNAL_PLAYING;
        entered_via_now_playing = true;
    }

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        if (state == PLAYER_INTERNAL_BROWSER) {
            uint32_t now = SDL_GetTicks();
            if (now - last_browser_refresh >= 100) {
                if (Browser_hasUpdate(&browser) && Browser_refresh(&browser)) dirty = 1;
                last_browser_refresh = now;
            }
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

        // Handle global input (skip if screen off or hint active)
        // A signal must reach the global input, thus a pending stop passes this
        // guard. ModuleCommon_handleGlobalInput() reports the quit and returns
        // before it reads any input, thus the screen-off states keep their
        // behavior.
        if (ModuleCommon_stopSignalled()
            || (!screen_off && !ModuleCommon_isScreenOffHintActive())) {
            HelpId help_id = (state == PLAYER_INTERNAL_BROWSER) ? HELP_BROWSER : HELP_PLAYER;
            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, help_id);
            if (global.should_quit) {
                save_resume_position();
                cleanup_playback(true);
                Browser_freeEntries(&browser);
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                ModuleCommon_frameEnd(screen);
                continue;
            }
        }

        if (state == PLAYER_INTERNAL_BROWSER) {
            if (handle_browser_input(&state, &dirty)) {
                return MODULE_EXIT_TO_MENU;
            }
        }
        else if (state == PLAYER_INTERNAL_PLAYING) {
            bool skip_render = handle_playing_input(screen, &state, &dirty);
            if (entered_via_now_playing && state != PLAYER_INTERNAL_PLAYING) {
                return MODULE_EXIT_TO_MENU;
            }
            if (skip_render) continue;
        }

        // Handle power management
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            ModuleCommon_PWR_update(&dirty, &show_setting);
        }

        // Render
        if (dirty && !screen_off) {
            if (ModuleCommon_isScreenOffHintActive()) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
            } else if (state == PLAYER_INTERNAL_BROWSER) {
                render_browser(screen, show_setting, &browser);
            } else {
                int pl_track = playlist_active ? Playlist_getCurrentIndex(&playlist) + 1 : 0;
                int pl_total = playlist_active ? Playlist_getCount(&playlist) : 0;
                render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
                MusicPlaying_paintLayers();
            }

            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ScreenTitle_frameEnd(&dirty, state == PLAYER_INTERNAL_BROWSER && !screen_off);
        ModuleCommon_frameEnd(screen);
    }
}

// Check if music player module is active
bool PlayerModule_isActive(void) {
    PlayerState state = Player_getState();
    return (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_PAUSED);
}

// Play next track (for USB HID button support)
void PlayerModule_nextTrack(void) {
    if (playlist_active) {
        int new_idx = Playlist_next(&playlist);
        if (new_idx < 0) {
            new_idx = 0;
            Playlist_setCurrentIndex(&playlist, new_idx);
        }
        save_resume_position();
        Player_stop();
        playlist_try_play(new_idx);
    } else if (initialized) {
        for (int i = browser.selected + 1; i < browser.entry_count; i++) {
            if (!browser.entries[i].is_dir && !browser.entries[i].is_play_all) {
                save_resume_position();
                Player_stop();
                browser.selected = i;
                try_play_file(browser.entries[i].db_id);
                break;
            }
        }
    }
}

// Play previous track (for USB HID button support)
void PlayerModule_prevTrack(void) {
    if (playlist_active) {
        int new_idx = Playlist_prev(&playlist);
        if (new_idx < 0) {
            new_idx = playlist.track_count - 1;
            Playlist_setCurrentIndex(&playlist, new_idx);
        }
        save_resume_position();
        Player_stop();
        playlist_try_play(new_idx);
    } else if (initialized) {
        for (int i = browser.selected - 1; i >= 0; i--) {
            if (!browser.entries[i].is_dir && !browser.entries[i].is_play_all) {
                save_resume_position();
                Player_stop();
                browser.selected = i;
                try_play_file(browser.entries[i].db_id);
                break;
            }
        }
    }
}

// Run the player directly with a pre-built playlist (from PlaylistModule)
ModuleExitReason PlayerModule_runWithPlaylist(DisplayContext* display,
                                              PlaylistTrack* tracks,
                                              int track_count,
                                              int start_index,
                                              int playlist_id) {
    if (!tracks || track_count <= 0) return MODULE_EXIT_TO_MENU;

    init_player();
    resume_playlist_id = playlist_id;
    ScreenTitle_start(true);

    // Set up the playlist context
    Playlist_free(&playlist);
    Playlist_init(&playlist);
    if (!playlist.tracks) return MODULE_EXIT_TO_MENU;
    int loaded_track_count = track_count < PLAYLIST_MAX_TRACKS
        ? track_count
        : PLAYLIST_MAX_TRACKS;
    for (int i = 0; i < loaded_track_count; i++) {
        playlist.tracks[i] = tracks[i];
    }
    playlist.track_count = loaded_track_count;
    playlist.current_index = start_index >= 0 && start_index < loaded_track_count
        ? start_index
        : 0;
    playlist_active = true;

    // Start playback
    const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
    if (!track || !start_playback(track->path, track->file_id)) {
        Playlist_free(&playlist);
        playlist_active = false;
        return MODULE_EXIT_TO_MENU;
    }

    int dirty = 1;
    int show_setting = 0;
    screen_off = false;
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle add-to-playlist dialog overlay
        if (AddToPlaylist_isActive()) {
            if (AddToPlaylist_handleInput()) {
                // Dialog closed — skip rest of input to avoid double-handling buttons
                dirty = 1;
                continue;
            }
            // Dialog covers entire screen, no need to render underlying content
            AddToPlaylist_render(screen);
            ModuleCommon_markSurfaceDrawn();
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Handle global input (skip if screen off or hint active)
        // A signal must reach the global input, thus a pending stop passes this
        // guard. ModuleCommon_handleGlobalInput() reports the quit and returns
        // before it reads any input, thus the screen-off states keep their
        // behavior.
        if (ModuleCommon_stopSignalled()
            || (!screen_off && !ModuleCommon_isScreenOffHintActive())) {
            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_PLAYER);
            if (global.should_quit) {
                save_resume_position();
                Player_stop();
                cleanup_album_art_background();
                cleanup_playback(true);
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                ModuleCommon_frameEnd(screen);
                continue;
            }
        }

        // Handle screen off hint
        if (ModuleCommon_isScreenOffHintActive()) {
            handle_hid_events();
            ModuleCommon_handleHardwareVolume();
            if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                ModuleCommon_resetScreenOffHint();
                ModuleCommon_recordInputTime();
                dirty = 1;
                continue;  // skip this frame's input so A doesn't toggle pause
            } else {
                if (PAD_anyPressed()) {
                    ModuleCommon_startScreenOffHint();
                }
                if (ModuleCommon_processScreenOffHintTimeout()) {
                    screen_off = true;
                    GFX_clear(screen);
                    ModuleCommon_markSurfaceDrawn();
                }
                Player_update();
                ModuleCommon_frameEnd(screen);
                continue;
            }
        }

        // Handle screen off mode
        if (screen_off) {
            if (PAD_anyPressed()) {
                screen_off = false;
                PLAT_enableBacklight(1);
                ModuleCommon_startScreenOffHint();
                GFX_clear(screen);
                render_screen_off_hint(screen);
                ModuleCommon_markSurfaceDrawn();
            }
            handle_hid_events();
            ModuleCommon_handleHardwareVolume();
            Player_update();

                if (Player_getState() == PLAYER_STATE_STOPPED) {
                if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
                    Resume_clear();
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    Player_stop();
                    cleanup_album_art_background();
                    cleanup_playback(true);
                    return MODULE_EXIT_TO_MENU;
                }
            }
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Normal input handling
        if (PAD_anyPressed()) {
            ModuleCommon_recordInputTime();
        }

        if (ModuleCommon_updateSeekScan(MUSIC_SEEK_STEP_MS, MUSIC_SEEK_STEP_MS)) {
            dirty = 1;
        }

        if (PAD_justPressed(BTN_A)) {
            Player_togglePause();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_B)) {
            save_resume_position();
            cleanup_album_art_background();
            if (Player_getState() == PLAYER_STATE_PLAYING) {
                cleanup_playback_ui();
                Background_setActive(BG_MUSIC);
            } else {
                Player_stop();
                cleanup_playback(true);
            }
            return MODULE_EXIT_TO_MENU;
        }
        else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
            PlayerModule_prevTrack();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
            PlayerModule_nextTrack();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_X)) {
            shuffle_enabled = !shuffle_enabled;
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_Y)) {
            repeat_enabled = !repeat_enabled;
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
            Spectrum_cycleNext();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_R3) || PAD_justPressed(BTN_R2)) {
            if (Settings_toggleBool(&SETTING_LYRICS_ENABLED)) {
                const TrackInfo* info = Player_getTrackInfo();
                if (info) {
                    Lyrics_fetch(info->artist, info->title, info->duration_ms / 1000,
                                 Player_getCurrentFile(), Player_getEmbeddedLyrics());
                }
            }
            dirty = 1;
        }
        else if (PAD_tappedSelect(SDL_GetTicks())) {
            ModuleCommon_startScreenOffHint();
            MusicPlaying_leave();
            dirty = 1;
        }

        // Check if track ended
        Player_update();
        if (Player_getState() == PLAYER_STATE_STOPPED) {
            if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
                    Resume_clear();
                cleanup_album_art_background();
                cleanup_playback(true);
                return MODULE_EXIT_TO_MENU;
            }
            dirty = 1;
        }

        // Save resume position periodically
        if (Player_getState() == PLAYER_STATE_PLAYING) {
            uint32_t now = SDL_GetTicks();
            if (now - last_resume_save > 5000) {
                Resume_updatePosition(Player_getPosition());
                last_resume_save = now;
            }
        }

        // Auto screen-off after inactivity
        if (Player_getState() == PLAYER_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
            MusicPlaying_leave();
            dirty = 1;
        }

        refresh_gpu_layers(&dirty);

        // Handle power management
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            ModuleCommon_PWR_update(&dirty, &show_setting);
        }

        // Render
        if (dirty && !screen_off) {
            if (ModuleCommon_isScreenOffHintActive()) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
            } else {
                int pl_track = Playlist_getCurrentIndex(&playlist) + 1;
                int pl_total = Playlist_getCount(&playlist);
                render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
                MusicPlaying_paintLayers();
            }

            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ModuleCommon_frameEnd(screen);
    }
}

// Run player restoring a saved resume state
ModuleExitReason PlayerModule_runResume(DisplayContext* display, const ResumeState* resume) {
    if (!resume) return MODULE_EXIT_TO_MENU;

    if (resume->type == RESUME_TYPE_FILES) {
        // Initialize browser with saved folder
        init_player();
        load_directory(resume->dir_id);
        nav_stack_top = 0;
        resume_playlist_id = 0;

        // Build playlist from directory starting at the saved track
        Playlist_free(&playlist);
        int count = Playlist_buildFromDirectory(
            &playlist, resume->dir_id, resume->file_id);
        if (count <= 0) return MODULE_EXIT_TO_MENU;
        playlist_active = true;

        // Start playback
        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
        if (!track || !start_playback(track->path, track->file_id)) {
            cleanup_playback(false);
            return MODULE_EXIT_TO_MENU;
        }

        // Seek to saved position. The position belongs to the saved file only.
        if (resume->position_ms > 0 && track->file_id == resume->file_id) {
            Player_seek(resume->position_ms);
        }

        // Set browser.selected to match the current track for display
        for (int i = 0; i < browser.entry_count; i++) {
            if (browser.entries[i].db_id == track->file_id) {
                browser.selected = i;
                break;
            }
        }

        // Use the shared playing loop via handle_playing_input
        int dirty = 1;
        int show_setting = 0;
        screen_off = false;
        ScreenTitle_start(true);
        ModuleCommon_resetScreenOffHint();
        ModuleCommon_recordInputTime();
        PlayerInternalState state = PLAYER_INTERNAL_PLAYING;

        while (1) {
            ModuleCommon_frameBegin();
            SDL_Surface* const screen = DisplayHelper_getSurface(display);

            // Handle add-to-playlist dialog overlay
            if (AddToPlaylist_isActive()) {
                if (AddToPlaylist_handleInput()) {
                    dirty = 1;
                    continue;
                }
                AddToPlaylist_render(screen);
                ModuleCommon_markSurfaceDrawn();
                ModuleCommon_frameEnd(screen);
                continue;
            }

            // Handle global input
            // A signal must reach the global input, thus a pending stop passes this
            // guard. ModuleCommon_handleGlobalInput() reports the quit and returns
            // before it reads any input, thus the screen-off states keep their
            // behavior.
            if (ModuleCommon_stopSignalled()
                || (!screen_off && !ModuleCommon_isScreenOffHintActive())) {
                GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_PLAYER);
                if (global.should_quit) {
                    save_resume_position();
                    Player_stop();
                    cleanup_album_art_background();
                    cleanup_playback(true);
                    return MODULE_EXIT_QUIT;
                }
                if (global.input_consumed) {
                    if (global.dirty) dirty = 1;
                    ModuleCommon_frameEnd(screen);
                    continue;
                }
            }

            // Delegate to shared playing input handler
            if (handle_playing_input(screen, &state, &dirty)) {
                // If state left playing (BTN_B or all tracks ended), return to menu immediately
                // Don't continue the loop or handle_playing_input will re-trigger track-ended logic
                if (state != PLAYER_INTERNAL_PLAYING) {
                    return MODULE_EXIT_TO_MENU;
                }
                continue;
            }

            // If state left playing, return to menu
            if (state != PLAYER_INTERNAL_PLAYING) {
                return MODULE_EXIT_TO_MENU;
            }

            // Handle power management
            if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
                ModuleCommon_PWR_update(&dirty, &show_setting);
            }

            // Render
            if (dirty && !screen_off) {
                if (ModuleCommon_isScreenOffHintActive()) {
                    GFX_clear(screen);
                    render_screen_off_hint(screen);
                } else {
                    int pl_track = Playlist_getCurrentIndex(&playlist) + 1;
                    int pl_total = Playlist_getCount(&playlist);
                    render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
                    MusicPlaying_paintLayers();
                }

                ModuleCommon_markSurfaceDrawn();
                dirty = 0;
            }
            ModuleCommon_frameEnd(screen);
        }

    } else if (resume->type == RESUME_TYPE_PLAYLIST) {
        // Load the M3U playlist tracks. A playlist that is gone ends Resume.
        PlaylistTrack m3u_tracks[PLAYLIST_MAX_TRACKS];
        int m3u_count = M3U_loadTracks(resume->playlist_id, m3u_tracks, PLAYLIST_MAX_TRACKS);
        if (m3u_count < 0) {
            Resume_clear();
            return MODULE_EXIT_TO_MENU;
        }
        if (m3u_count <= 0) return MODULE_EXIT_TO_MENU;

        // Find the track index in the loaded playlist
        int start_idx = 0;
        for (int i = 0; i < m3u_count; i++) {
            if (m3u_tracks[i].file_id == resume->file_id) {
                start_idx = i;
                break;
            }
        }

        // Run with the playlist. The position belongs to the saved file only.
        pending_resume_position =
            m3u_tracks[start_idx].file_id == resume->file_id ? resume->position_ms : 0;
        ModuleExitReason reason = PlayerModule_runWithPlaylist(
            display, m3u_tracks, m3u_count, start_idx, resume->playlist_id);
        pending_resume_position = 0;
        return reason;
    }

    return MODULE_EXIT_TO_MENU;
}

// Background tick: handle track advancement and resume saving while in menu
void PlayerModule_backgroundTick(void) {
    Player_update();

    // Handle track ended (auto-advance)
    if (Player_getState() == PLAYER_STATE_STOPPED) {
        if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
            // All tracks finished
            Resume_clear();
            cleanup_playback(false);
            Background_setActive(BG_NONE);
            ModuleCommon_setAutosleepDisabled(false);
        }
        return;
    }

    // Save resume position periodically
    if (Player_getState() == PLAYER_STATE_PLAYING) {
        uint32_t now = SDL_GetTicks();
        if (now - last_resume_save > 5000) {
            Resume_updatePosition(Player_getPosition());
            last_resume_save = now;
        }
    }
}
