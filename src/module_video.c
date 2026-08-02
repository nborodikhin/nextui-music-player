#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_video.h"
#include "ui_video.h"
#include "ui_utils.h"
#include "ui_fonts.h"
#include "browser.h"
#include "player.h"
#include "settings.h"

#define VIDEOS_PATH SDCARD_PATH "/Videos"

static char video_toast_message[128] = "";
static uint32_t video_toast_time = 0;

static const char* video_extensions[] = {
    ".mp4", ".mkv", ".avi", ".mov", ".webm",
    ".flv", ".wmv", ".m4v", ".ts",  ".3gp",
    ".mpg", ".mpeg", NULL
};

bool VideoModule_isVideoFile(const char* filename) {
    if (!filename) return false;
    const char* dot = strrchr(filename, '.');
    if (!dot) return false;
    for (int i = 0; video_extensions[i] != NULL; i++) {
        if (strcasecmp(dot, video_extensions[i]) == 0) {
            return true;
        }
    }
    return false;
}

void VideoModule_setToast(const char* message) {
    snprintf(video_toast_message, sizeof(video_toast_message), "%s", message);
    video_toast_time = SDL_GetTicks();
}

static void video_browser_load(BrowserContext* ctx, const char* path) {
    Browser_freeEntries(ctx);

    strncpy(ctx->current_path, path, sizeof(ctx->current_path) - 1);
    ctx->selected = 0;
    ctx->scroll_offset = 0;

    mkdir(path, 0755);

    DIR* dir = opendir(path);
    if (!dir) {
        LOG_error("VideoBrowser: Failed to open %s\n", path);
        return;
    }

    int dir_count = 0;
    int video_count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            dir_count++;
        } else if (VideoModule_isVideoFile(ent->d_name)) {
            video_count++;
        }
    }

    int count = dir_count + video_count;
    bool has_parent = (strcmp(path, VIDEOS_PATH) != 0);
    if (has_parent) count++;

    ctx->entries = malloc(sizeof(FileEntry) * (count > 0 ? count : 1));
    if (!ctx->entries) {
        closedir(dir);
        return;
    }

    int idx = 0;
    if (has_parent) {
        strncpy(ctx->entries[idx].name, "..", sizeof(ctx->entries[idx].name) - 1);
        ctx->entries[idx].is_dir = true;
        ctx->entries[idx].is_play_all = false;
        ctx->entries[idx].format = AUDIO_FORMAT_UNKNOWN;
        idx++;
    }

    rewinddir(dir);
    while ((ent = readdir(dir)) != NULL && idx < count) {
        if (ent->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            strncpy(ctx->entries[idx].name, ent->d_name, sizeof(ctx->entries[idx].name) - 1);
            ctx->entries[idx].is_dir = true;
            ctx->entries[idx].is_play_all = false;
            ctx->entries[idx].format = AUDIO_FORMAT_UNKNOWN;
            idx++;
        } else if (VideoModule_isVideoFile(ent->d_name)) {
            strncpy(ctx->entries[idx].name, ent->d_name, sizeof(ctx->entries[idx].name) - 1);
            ctx->entries[idx].is_dir = false;
            ctx->entries[idx].is_play_all = false;
            ctx->entries[idx].format = AUDIO_FORMAT_UNKNOWN;
            idx++;
        }
    }
    closedir(dir);
    ctx->entry_count = idx;
}

// Play single video file with hardware video player (Full on-screen video + audio)
static ModuleExitReason play_video_file(SDL_Surface* screen, const char* filepath) {
    if (!filepath) return MODULE_EXIT_TO_MENU;

    // Stop internal audio engine to free audio device
    Player_stop();

    // Clear display to black before launching player
    GFX_clear(screen);
    GFX_flip(screen);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "echo 1 > /tmp/stay_awake; "
        "if [ -f /usr/trimui/apps/player/launch.sh ]; then "
        "  chmod +x /usr/trimui/apps/player/launch.sh; "
        "  /usr/trimui/apps/player/launch.sh \"%s\"; "
        "elif [ -f /usr/trimui/apps/player/player ]; then "
        "  chmod +x /usr/trimui/apps/player/player; "
        "  /usr/trimui/apps/player/player \"%s\"; "
        "elif [ -f /usr/trimui/apps/player/bin/player ]; then "
        "  chmod +x /usr/trimui/apps/player/bin/player; "
        "  /usr/trimui/apps/player/bin/player \"%s\"; "
        "fi; "
        "rm -f /tmp/stay_awake",
        filepath, filepath, filepath);

    system(cmd);

    // Re-clear screen after player exits
    GFX_clear(screen);
    GFX_flip(screen);

    return MODULE_EXIT_TO_MENU;
}

// Play audio-only stream with Lock Screen support
static ModuleExitReason play_video_audio_only(SDL_Surface* screen, const char* filepath) {
    if (!filepath) return MODULE_EXIT_TO_MENU;

    // Extract title from filename
    char title[256];
    const char* slash = strrchr(filepath, '/');
    const char* base = slash ? slash + 1 : filepath;
    strncpy(title, base, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    char* dot = strrchr(title, '.');
    if (dot) *dot = '\0';

    // Start playing audio/media stream via Player
    if (Player_load(filepath) != 0 || Player_play() != 0) {
        VideoModule_setToast("Failed to open audio");
        return MODULE_EXIT_TO_MENU;
    }

    bool is_paused = false;
    bool is_locked = false;
    bool show_hud = true;
    uint32_t hud_timer = SDL_GetTicks();
    int show_setting = 0;
    int dirty = 1;
    int seek_feedback = 0;
    uint32_t seek_feedback_time = 0;

    while (1) {
        GFX_startFrame();
        PAD_poll();

        // Handle global inputs (Volume, START menu, Power, Sleep timer)
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 0);
        if (global.should_quit) {
            Player_stop();
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // Lock Screen mode handling
        if (is_locked) {
            if (PAD_justPressed(BTN_A)) {
                is_locked = false;
                show_hud = true;
                hud_timer = SDL_GetTicks();
                dirty = 1;
            } else if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START) || PAD_justPressed(BTN_SELECT)) {
                // Ignore other buttons on lock screen
            }
        } else {
            // Unlocked playback controls
            if (PAD_justPressed(BTN_SELECT)) {
                // Lock screen: keep audio running, lock display
                is_locked = true;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                // Play / Pause
                if (is_paused) {
                    Player_play();
                    is_paused = false;
                } else {
                    Player_pause();
                    is_paused = true;
                }
                show_hud = true;
                hud_timer = SDL_GetTicks();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                // Exit video
                Player_stop();
                return MODULE_EXIT_TO_MENU;
            }
            else if (PAD_justRepeated(BTN_LEFT) || PAD_justRepeated(BTN_RIGHT)) {
                // Seek ±10 seconds
                int seek_dir = PAD_justRepeated(BTN_RIGHT) ? 10 : -10;
                int cur_pos = Player_getPosition() / 1000;
                int dur = Player_getDuration() / 1000;
                int new_pos = cur_pos + seek_dir;
                if (new_pos < 0) new_pos = 0;
                if (dur > 0 && new_pos > dur) new_pos = dur;
                Player_seek(new_pos * 1000);
                seek_feedback = seek_dir;
                seek_feedback_time = SDL_GetTicks();
                show_hud = true;
                hud_timer = SDL_GetTicks();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_L1) || PAD_justPressed(BTN_R1)) {
                // Seek ±60 seconds
                int seek_dir = PAD_justPressed(BTN_R1) ? 60 : -60;
                int cur_pos = Player_getPosition() / 1000;
                int dur = Player_getDuration() / 1000;
                int new_pos = cur_pos + seek_dir;
                if (new_pos < 0) new_pos = 0;
                if (dur > 0 && new_pos > dur) new_pos = dur;
                Player_seek(new_pos * 1000);
                seek_feedback = seek_dir;
                seek_feedback_time = SDL_GetTicks();
                show_hud = true;
                hud_timer = SDL_GetTicks();
                dirty = 1;
            }

            // Auto-hide HUD after 3 seconds of inactivity
            if (show_hud && !is_paused && SDL_GetTicks() - hud_timer > 3000) {
                show_hud = false;
                dirty = 1;
            }

            if (seek_feedback != 0 && SDL_GetTicks() - seek_feedback_time > 1000) {
                seek_feedback = 0;
                dirty = 1;
            }
        }

        // Check if playback reached the end
        if (Player_getState() == PLAYER_STATE_STOPPED) {
            Player_stop();
            return MODULE_EXIT_TO_MENU;
        }

        // Power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Rendering
        if (dirty || show_hud || is_paused || is_locked) {
            GFX_clear(screen);

            int cur_sec = Player_getPosition() / 1000;
            int dur_sec = Player_getDuration() / 1000;

            render_video_osd(screen, title, cur_sec, dur_sec, is_paused, is_locked,
                             show_setting, seek_feedback, show_hud);

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        }

        GFX_sync();
    }
}

void VideoModule_init(void) {
    mkdir(VIDEOS_PATH, 0755);
}

ModuleExitReason VideoModule_run(SDL_Surface* screen) {
    VideoModule_init();

    BrowserContext browser;
    memset(&browser, 0, sizeof(browser));
    video_browser_load(&browser, VIDEOS_PATH);

    int dirty = 1;
    int show_setting = 0;

    while (1) {
        GFX_startFrame();
        PAD_poll();

        // Handle global input
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 0);
        if (global.should_quit) {
            Browser_freeEntries(&browser);
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        int items_per_page = calc_list_layout(screen).items_per_page;

        if (PAD_justRepeated(BTN_UP)) {
            browser.selected = (browser.selected > 0) ? browser.selected - 1 : (browser.entry_count > 0 ? browser.entry_count - 1 : 0);
            dirty = 1;
        }
        else if (PAD_justRepeated(BTN_DOWN)) {
            browser.selected = (browser.selected < browser.entry_count - 1) ? browser.selected + 1 : 0;
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_LEFT)) {
            list_page_up(&browser.selected, &browser.scroll_offset, browser.entry_count, items_per_page);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_RIGHT)) {
            list_page_down(&browser.selected, &browser.scroll_offset, browser.entry_count, items_per_page);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            if (browser.entry_count > 0 && browser.selected < browser.entry_count) {
                FileEntry* ent = &browser.entries[browser.selected];
                if (ent->is_dir) {
                    if (strcmp(ent->name, "..") == 0) {
                        // Go up
                        char* last_slash = strrchr(browser.current_path, '/');
                        if (last_slash && last_slash != browser.current_path) {
                            *last_slash = '\0';
                        } else {
                            strncpy(browser.current_path, VIDEOS_PATH, sizeof(browser.current_path) - 1);
                        }
                    } else {
                        // Go into dir
                        char next_path[1024];
                        snprintf(next_path, sizeof(next_path), "%s/%s", browser.current_path, ent->name);
                        strncpy(browser.current_path, next_path, sizeof(browser.current_path) - 1);
                    }
                    video_browser_load(&browser, browser.current_path);
                    dirty = 1;
                } else {
                    // Play video
                    char video_path[1024];
                    snprintf(video_path, sizeof(video_path), "%s/%s", browser.current_path, ent->name);
                    ModuleExitReason r = play_video_file(screen, video_path);
                    if (r == MODULE_EXIT_QUIT) {
                        Browser_freeEntries(&browser);
                        return MODULE_EXIT_QUIT;
                    }
                    dirty = 1;
                }
            }
        }
        else if (PAD_justPressed(BTN_X)) {
            if (browser.entry_count > 0 && browser.selected < browser.entry_count) {
                FileEntry* ent = &browser.entries[browser.selected];
                if (!ent->is_dir) {
                    // Play audio-only with Lock Screen
                    char video_path[1024];
                    snprintf(video_path, sizeof(video_path), "%s/%s", browser.current_path, ent->name);
                    ModuleExitReason r = play_video_audio_only(screen, video_path);
                    if (r == MODULE_EXIT_QUIT) {
                        Browser_freeEntries(&browser);
                        return MODULE_EXIT_QUIT;
                    }
                    dirty = 1;
                }
            }
        }
        else if (PAD_justPressed(BTN_B)) {
            // If in subdirectory, go up, otherwise return to main menu
            if (strcmp(browser.current_path, VIDEOS_PATH) != 0) {
                char* last_slash = strrchr(browser.current_path, '/');
                if (last_slash && last_slash != browser.current_path) {
                    *last_slash = '\0';
                } else {
                    strncpy(browser.current_path, VIDEOS_PATH, sizeof(browser.current_path) - 1);
                }
                video_browser_load(&browser, browser.current_path);
                dirty = 1;
            } else {
                Browser_freeEntries(&browser);
                return MODULE_EXIT_TO_MENU;
            }
        }

        ModuleCommon_PWR_update(&dirty, &show_setting);

        if (dirty) {
            render_video_browser(screen, show_setting, &browser);

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            render_toast(screen, video_toast_message, video_toast_time);
            GFX_flip(screen);
            dirty = 0;

            ModuleCommon_tickToast(video_toast_message, video_toast_time, &dirty);
        } else {
            GFX_sync();
        }
    }
}
