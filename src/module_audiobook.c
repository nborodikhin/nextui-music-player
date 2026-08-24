#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "audiobook.h"
#include "background.h"
#include "help_screen.h"
#include "module_audiobook.h"
#include "module_common.h"
#include "player.h"
#include "toast.h"
#include "ui_album_art.h"
#include "ui_audiobook.h"
#include "ui_main.h"
#include "ui_utils.h"

typedef enum {
    AUDIOBOOK_STATE_LIBRARY,
    AUDIOBOOK_STATE_CHAPTERS,
    AUDIOBOOK_STATE_SEEKING,
    AUDIOBOOK_STATE_PLAYING
} AudiobookInternalState;

#define PROGRESS_SAVE_INTERVAL_MS 30000
#define SKIP_BACK_MS  10000
#define SKIP_FWD_MS   30000

// Sleep timer cycle: Off -> 15 -> 30 -> 45 -> 60 min -> end of chapter -> Off
static const int SLEEP_MINUTES[] = {0, 15, 30, 45, 60, -1};
#define SLEEP_OPTION_COUNT ((int)(sizeof(SLEEP_MINUTES) / sizeof(SLEEP_MINUTES[0])))

// List state
static int library_selected = 0;
static int library_scroll = 0;
static int chapters_selected = 0;
static int chapters_scroll = 0;

// The book currently loaded into the player. Identified by key rather than by
// index: a rescan reorders the library (in-progress books float to the top).
static char current_key[AUDIOBOOK_MAX_PATH] = "";
static int current_book_index = -1;
static int current_chapter = 0;
static bool playback_started = false;

// Player_load() leaves the player STOPPED until Player_play() runs, and a
// pending seek keeps it there for several frames. Until playback has actually
// been observed, STOPPED means "not started yet", not "chapter finished".
static bool playback_confirmed = false;

static uint32_t last_progress_save_time = 0;

// Sleep timer
static int sleep_option = 0;
static uint32_t sleep_deadline = 0;      // SDL ticks, 0 = not armed
static bool sleep_at_chapter_end = false;

static ToastToken seek_toast = TOAST_TOKEN_NONE;
static bool screen_off = false;

// ---------------------------------------------------------------------------
// Current book helpers
// ---------------------------------------------------------------------------

static Audiobook* current_book(void) {
    Audiobook* book = Audiobook_get(current_book_index);
    if (book && strcmp(book->key, current_key) == 0) return book;
    return NULL;
}

// Re-point current_book_index after a rescan
static void resolve_current_index(void) {
    current_book_index = -1;
    if (!current_key[0]) return;
    for (int i = 0; i < Audiobook_getCount(); i++) {
        Audiobook* book = Audiobook_get(i);
        if (book && strcmp(book->key, current_key) == 0) {
            current_book_index = i;
            return;
        }
    }
}

// Position inside the current chapter. For a single-file book the player
// reports a position measured from the start of the whole book.
static int chapter_position_ms(const Audiobook* book) {
    int position = Player_getPosition();
    if (book && book->single_file) {
        AudiobookChapter* chapter = Audiobook_getChapter(current_chapter);
        if (chapter) position -= chapter->start_ms;
    }
    return position > 0 ? position : 0;
}

static void save_progress_now(void) {
    Audiobook* book = current_book();
    if (!book || !playback_started) return;
    Audiobook_saveProgress(book, current_chapter, chapter_position_ms(book));
    Audiobook_flushProgress();
}

// Load a chapter and seek into it.
// Returns 1 when a seek is in flight, 0 when ready to play, -1 on failure.
static int start_chapter(int chapter_index, int position_ms) {
    Audiobook* book = current_book();
    AudiobookChapter* chapter = Audiobook_getChapter(chapter_index);
    if (!book || !chapter) return -1;

    // A single-file book stays loaded across chapters; only reload on a new file
    const char* loaded = Player_getCurrentFile();
    bool already_loaded = loaded && loaded[0] && strcmp(loaded, chapter->path) == 0 &&
                          Player_getState() != PLAYER_STATE_STOPPED;
    if (!already_loaded) {
        // The cached background keys off the art surface pointer, so drop it
        // before the old surface is freed by the next load
        cleanup_album_art_background();
        if (Player_load(chapter->path) != 0) return -1;
    }

    current_chapter = chapter_index;
    playback_started = true;
    playback_confirmed = false;
    last_progress_save_time = SDL_GetTicks();

    int seek_ms = book->single_file ? chapter->start_ms + position_ms : position_ms;
    if (seek_ms > 0 || already_loaded) {
        Player_seek(seek_ms);
        return 1;
    }
    return 0;
}

static void release_playback(void) {
    save_progress_now();
    Player_stop();
    cleanup_album_art_background();
    playback_started = false;
    playback_confirmed = false;
    sleep_deadline = 0;
    sleep_at_chapter_end = false;
    sleep_option = 0;
    Background_setActive(BG_NONE);
    ModuleCommon_setAutosleepDisabled(false);
}

// ---------------------------------------------------------------------------
// Sleep timer
// ---------------------------------------------------------------------------

static void cycle_sleep_timer(void) {
    sleep_option = (sleep_option + 1) % SLEEP_OPTION_COUNT;
    int minutes = SLEEP_MINUTES[sleep_option];

    sleep_deadline = 0;
    sleep_at_chapter_end = false;

    if (minutes > 0) {
        sleep_deadline = SDL_GetTicks() + (uint32_t)minutes * 60000u;
        char message[64];
        snprintf(message, sizeof(message), "Sleep timer: %d min", minutes);
        Toast_show(message, TOAST_DURATION);
    } else if (minutes < 0) {
        sleep_at_chapter_end = true;
        Toast_show("Sleep timer: end of chapter", TOAST_DURATION);
    } else {
        Toast_show("Sleep timer off", TOAST_DURATION);
    }
}

// Label for the playing screen; NULL when no timer is armed
static const char* sleep_timer_label(char* buffer, size_t size) {
    if (sleep_at_chapter_end) return "end of chapter";
    if (sleep_deadline == 0) return NULL;

    uint32_t now = SDL_GetTicks();
    uint32_t remaining = (sleep_deadline > now) ? (sleep_deadline - now) / 1000 : 0;
    snprintf(buffer, size, "%u:%02u", remaining / 60, remaining % 60);
    return buffer;
}

static void sleep_timer_fire(void) {
    Player_pause();
    save_progress_now();
    sleep_deadline = 0;
    sleep_at_chapter_end = false;
    sleep_option = 0;
    Background_setActive(BG_NONE);
    ModuleCommon_setAutosleepDisabled(false);
}

// ---------------------------------------------------------------------------
// Shared tick — runs in the PLAYING loop and from the background tick, so the
// two can never drift apart the way the podcast module's copies did.
// ---------------------------------------------------------------------------

static void audiobook_tick(void) {
    Player_update();

    Audiobook* book = current_book();
    if (!book || !playback_started) return;

    // Nothing has started yet: the file is loaded but Player_play() has not run,
    // or a resume seek is still in flight. Reading STOPPED here would look like
    // the chapter had ended the instant the user opened the book.
    if (Player_getState() != PLAYER_STATE_STOPPED) {
        playback_confirmed = true;
    } else if (!playback_confirmed) {
        return;
    }

    int chapter_count = Audiobook_getChapterCount();
    bool chapter_changed = false;

    // A single-file book runs straight through: track which chapter we are in
    if (book->single_file) {
        int position = Player_getPosition();
        while (current_chapter + 1 < chapter_count) {
            AudiobookChapter* next = Audiobook_getChapter(current_chapter + 1);
            if (!next || position < next->start_ms) break;
            current_chapter++;
            chapter_changed = true;
        }
    }

    // A multi-file book ends its file at each chapter boundary
    if (Player_getState() == PLAYER_STATE_STOPPED) {
        if (!book->single_file && current_chapter + 1 < chapter_count) {
            if (sleep_at_chapter_end) {
                Audiobook_saveProgress(book, current_chapter + 1, 0);
                Audiobook_flushProgress();
                current_chapter++;
                sleep_timer_fire();
                return;
            }
            if (start_chapter(current_chapter + 1, 0) >= 0) {
                Player_play();
                Audiobook_saveProgress(book, current_chapter, 0);
                Audiobook_flushProgress();
                return;
            }
        }

        // End of the book
        Audiobook_saveProgress(book, chapter_count - 1, 0);
        Audiobook_markFinished(book, true);
        Audiobook_flushProgress();
        Player_stop();
        cleanup_album_art_background();
        playback_started = false;
        playback_confirmed = false;
        sleep_deadline = 0;
        sleep_at_chapter_end = false;
        Background_setActive(BG_NONE);
        ModuleCommon_setAutosleepDisabled(false);
        return;
    }

    if (chapter_changed) {
        Audiobook_saveProgress(book, current_chapter, chapter_position_ms(book));
        Audiobook_flushProgress();
        if (sleep_at_chapter_end) {
            sleep_timer_fire();
            return;
        }
    }

    uint32_t now = SDL_GetTicks();
    if (sleep_deadline != 0 && now >= sleep_deadline) {
        sleep_timer_fire();
        return;
    }

    if (Player_getState() == PLAYER_STATE_PLAYING &&
        now - last_progress_save_time >= PROGRESS_SAVE_INTERVAL_MS) {
        save_progress_now();
        last_progress_save_time = now;
    }
}

// ---------------------------------------------------------------------------
// Screen-off plumbing (mirrors the podcast player)
// ---------------------------------------------------------------------------

static void handle_hid_events(void) {
    USBHIDEvent event;
    while ((event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (event == USB_HID_EVENT_PLAY_PAUSE) {
            if (Player_getState() == PLAYER_STATE_PAUSED) Player_play();
            else Player_pause();
        } else {
            ModuleCommon_handleHIDVolume(event);
        }
    }
}

static void clear_and_show_screen_off_hint(SDL_Surface* screen) {
    AudiobookUI_clearPlayingScreen();
    GFX_clear(screen);
    render_screen_off_hint(screen);
    GFX_flip(screen);
}

// Leave the playing screen without stopping the audio
static void leave_playing_screen(AudiobookInternalState* state, int* dirty) {
    save_progress_now();
    AudiobookUI_clearPlayingScreen();
    chapters_selected = current_chapter;
    *state = AUDIOBOOK_STATE_CHAPTERS;
    *dirty = 1;
}

// ---------------------------------------------------------------------------
// Module entry point
// ---------------------------------------------------------------------------

ModuleExitReason AudiobookModule_run(DisplayContext* display) {
    Audiobook_init();

    AudiobookInternalState state = AUDIOBOOK_STATE_LIBRARY;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();

    bool resuming_background = (Background_getActive() == BG_AUDIOBOOK) && AudiobookModule_isActive();

    Audiobook_scanLibrary();
    resolve_current_index();

    // The scan drops the chapter buffer, so a book playing in the background
    // needs its chapters reloaded before the playing screen can draw them.
    if (resuming_background && current_book() && Audiobook_loadChapters(current_book()) > 0) {
        Background_setActive(BG_NONE);
        ModuleCommon_setAutosleepDisabled(true);
        state = AUDIOBOOK_STATE_PLAYING;
    } else {
        library_selected = 0;
        library_scroll = 0;
    }

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            HelpId help_id;
            switch (state) {
                case AUDIOBOOK_STATE_CHAPTERS: help_id = HELP_AUDIOBOOK_CHAPTERS; break;
                case AUDIOBOOK_STATE_SEEKING:
                case AUDIOBOOK_STATE_PLAYING:  help_id = HELP_AUDIOBOOK_PLAYING; break;
                default:                       help_id = HELP_AUDIOBOOK_LIBRARY; break;
            }

            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, help_id);
            if (global.should_quit) {
                save_progress_now();
                Audiobook_flushProgress();
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        // =========================================
        // LIBRARY
        // =========================================
        if (state == AUDIOBOOK_STATE_LIBRARY) {
            // Row 0 is the pinned "Continue" entry when a book is in progress;
            // it points at the same book that also appears in the list below.
            int resume_index = Audiobook_getResumeIndex();
            int has_resume = (resume_index >= 0) ? 1 : 0;
            int count = has_resume + Audiobook_getCount();
            int items_per_page = calc_list_layout(screen).list_h / (SCALE1(PILL_SIZE) * 3 / 2);

            // The resume row can appear or vanish while the list is open
            if (library_selected >= count) library_selected = count > 0 ? count - 1 : 0;

            int book_index = (library_selected == 0 && has_resume)
                           ? resume_index : library_selected - has_resume;

            if (AudiobookUI_isTitleScrolling()) AudiobookUI_animateTitleScroll();
            if (AudiobookUI_titleScrollNeedsRender()) dirty = 1;

            if (PAD_justRepeated(BTN_UP) && count > 0) {
                library_selected = (library_selected > 0) ? library_selected - 1 : count - 1;
                AudiobookUI_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                library_selected = (library_selected < count - 1) ? library_selected + 1 : 0;
                AudiobookUI_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_LEFT) && count > 0) {
                if (list_page_up(&library_selected, &library_scroll, count, items_per_page)) {
                    AudiobookUI_clearTitleScroll();
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_RIGHT) && count > 0) {
                if (list_page_down(&library_selected, &library_scroll, count, items_per_page)) {
                    AudiobookUI_clearTitleScroll();
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_A) && count > 0) {
                Audiobook* book = Audiobook_get(book_index);
                if (book) {
                    // Only one book at a time; switching saves the outgoing one
                    if (playback_started && strcmp(book->key, current_key) != 0) {
                        release_playback();
                    }
                    Background_stopAll();

                    snprintf(current_key, sizeof(current_key), "%s", book->key);
                    current_book_index = book_index;

                    if (Audiobook_loadChapters(book) == 0) {
                        Toast_show("No playable chapters", TOAST_DURATION);
                        current_key[0] = '\0';
                        current_book_index = -1;
                    } else {
                        int chapter = book->current_chapter;
                        if (chapter < 0 || chapter >= Audiobook_getChapterCount()) chapter = 0;
                        int result = start_chapter(chapter, book->position_ms);
                        if (result < 0) {
                            Toast_show("Failed to play", TOAST_DURATION);
                        } else {
                            AudiobookUI_clearTitleScroll();
                            ModuleCommon_recordInputTime();
                            if (result == 1) {
                                seek_toast = Toast_showScreenBound("Resuming...", TOAST_DURATION_FOREVER);
                                state = AUDIOBOOK_STATE_SEEKING;
                            } else {
                                Player_play();
                                state = AUDIOBOOK_STATE_PLAYING;
                            }
                        }
                    }
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && count > 0) {
                // Toggle the finished flag, keeping the saved position intact
                Audiobook* book = Audiobook_get(book_index);
                if (book) {
                    Audiobook_markFinished(book, !book->finished);
                    Audiobook_flushProgress();
                    Toast_show(book->finished ? "Marked as finished" : "Marked as unfinished", TOAST_DURATION);
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                AudiobookUI_clearTitleScroll();
                if (AudiobookModule_isActive()) {
                    save_progress_now();
                    Background_setActive(BG_AUDIOBOOK);
                } else {
                    Audiobook_flushProgress();
                }
                return MODULE_EXIT_TO_MENU;
            }

            if (playback_started) audiobook_tick();
        }
        // =========================================
        // CHAPTERS
        // =========================================
        else if (state == AUDIOBOOK_STATE_CHAPTERS) {
            int count = Audiobook_getChapterCount();
            int items_per_page = calc_list_layout(screen).items_per_page;

            if (AudiobookUI_isTitleScrolling()) AudiobookUI_animateTitleScroll();
            if (AudiobookUI_titleScrollNeedsRender()) dirty = 1;

            if (PAD_justRepeated(BTN_UP) && count > 0) {
                chapters_selected = (chapters_selected > 0) ? chapters_selected - 1 : count - 1;
                AudiobookUI_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                chapters_selected = (chapters_selected < count - 1) ? chapters_selected + 1 : 0;
                AudiobookUI_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_LEFT) && count > 0) {
                if (list_page_up(&chapters_selected, &chapters_scroll, count, items_per_page)) {
                    AudiobookUI_clearTitleScroll();
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_RIGHT) && count > 0) {
                if (list_page_down(&chapters_selected, &chapters_scroll, count, items_per_page)) {
                    AudiobookUI_clearTitleScroll();
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_A) && count > 0) {
                int result = start_chapter(chapters_selected, 0);
                if (result < 0) {
                    Toast_show("Failed to play", TOAST_DURATION);
                } else {
                    AudiobookUI_clearTitleScroll();
                    ModuleCommon_recordInputTime();
                    if (result == 1) {
                        seek_toast = Toast_showScreenBound("Resuming...", TOAST_DURATION_FOREVER);
                        state = AUDIOBOOK_STATE_SEEKING;
                    } else {
                        Player_play();
                        state = AUDIOBOOK_STATE_PLAYING;
                    }
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                AudiobookUI_clearTitleScroll();
                if (playback_started) {
                    save_progress_now();
                }
                state = AUDIOBOOK_STATE_LIBRARY;
                dirty = 1;
            }

            if (playback_started) audiobook_tick();
        }
        // =========================================
        // SEEKING (resuming to the saved position)
        // =========================================
        else if (state == AUDIOBOOK_STATE_SEEKING) {
            ModuleCommon_setAutosleepDisabled(true);

            // Despite the name, Player_resume() reports "a seek is still running"
            if (!Player_resume()) {
                Player_play();
                Toast_dismiss(seek_toast);
                ModuleCommon_recordInputTime();
                last_progress_save_time = SDL_GetTicks();
                state = AUDIOBOOK_STATE_PLAYING;
            }
            else if (PAD_justPressed(BTN_B)) {
                Toast_dismiss(seek_toast);
                release_playback();
                AudiobookUI_clearPlayingScreen();
                state = AUDIOBOOK_STATE_CHAPTERS;
                dirty = 1;
                continue;
            }

            dirty = 1;
        }
        // =========================================
        // PLAYING
        // =========================================
        else if (state == AUDIOBOOK_STATE_PLAYING) {
            ModuleCommon_setAutosleepDisabled(true);

            if (ModuleCommon_isScreenOffHintActive()) {
                handle_hid_events();
                ModuleCommon_handleHardwareVolume();
                audiobook_tick();

                if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                    ModuleCommon_resetScreenOffHint();
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                    continue;
                }
                if (PAD_anyPressed()) ModuleCommon_startScreenOffHint();
                if (ModuleCommon_processScreenOffHintTimeout()) {
                    screen_off = true;
                    GFX_clear(screen);
                    GFX_flip(screen);
                }
                GFX_sync();
                continue;
            }
            else if (screen_off) {
                handle_hid_events();
                ModuleCommon_handleHardwareVolume();
                audiobook_tick();

                if (PAD_anyPressed()) {
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    ModuleCommon_startScreenOffHint();
                    GFX_clear(screen);
                    render_screen_off_hint(screen);
                    GFX_flip(screen);
                }
                GFX_sync();
                continue;
            }

            Audiobook* book = current_book();
            int chapter_count = Audiobook_getChapterCount();

            if (PAD_justPressed(BTN_A)) {
                if (Player_getState() == PLAYER_STATE_PAUSED) {
                    Player_play();
                } else {
                    Player_pause();
                    save_progress_now();
                }
                ModuleCommon_recordInputTime();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                if (Player_getState() == PLAYER_STATE_PLAYING) {
                    leave_playing_screen(&state, &dirty);
                } else {
                    release_playback();
                    AudiobookUI_clearPlayingScreen();
                    chapters_selected = current_chapter;
                    state = AUDIOBOOK_STATE_CHAPTERS;
                    dirty = 1;
                }
                continue;
            }
            else if (PAD_justPressed(BTN_X)) {
                leave_playing_screen(&state, &dirty);
                continue;
            }
            else if (PAD_justPressed(BTN_Y)) {
                cycle_sleep_timer();
                ModuleCommon_recordInputTime();
                dirty = 1;
            }
            else if (PAD_tappedSelect(SDL_GetTicks())) {
                ModuleCommon_startScreenOffHint();
                clear_and_show_screen_off_hint(screen);
                continue;
            }
            else if (PAD_justRepeated(BTN_LEFT)) {
                int position = Player_getPosition() - SKIP_BACK_MS;
                // Never skip past the start of the current chapter
                int floor_ms = 0;
                if (book && book->single_file) {
                    AudiobookChapter* chapter = Audiobook_getChapter(current_chapter);
                    if (chapter) floor_ms = chapter->start_ms;
                }
                if (position < floor_ms) position = floor_ms;
                Player_seek(position);
                ModuleCommon_recordInputTime();
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_RIGHT)) {
                int position = Player_getPosition() + SKIP_FWD_MS;
                int duration = Player_getDuration();
                if (duration > 0 && position > duration) position = duration;
                Player_seek(position);
                ModuleCommon_recordInputTime();
                dirty = 1;
            }
            else if ((PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_L1)) && current_chapter > 0) {
                if (start_chapter(current_chapter - 1, 0) >= 0) {
                    Player_play();
                    AudiobookUI_clearPlayingScreen();
                    save_progress_now();
                }
                dirty = 1;
            }
            else if ((PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_R1)) &&
                     current_chapter + 1 < chapter_count) {
                if (start_chapter(current_chapter + 1, 0) >= 0) {
                    Player_play();
                    AudiobookUI_clearPlayingScreen();
                    save_progress_now();
                }
                dirty = 1;
            }

            audiobook_tick();

            // Playback may have ended or the sleep timer fired inside the tick
            if (!playback_started) {
                AudiobookUI_clearPlayingScreen();
                Audiobook_scanLibrary();
                resolve_current_index();
                chapters_selected = current_chapter;
                state = AUDIOBOOK_STATE_LIBRARY;
                Toast_show("Finished", TOAST_DURATION);
                dirty = 1;
                continue;
            }

            if (AudiobookUI_isTitleScrolling()) AudiobookUI_animateTitleScroll();
            if (AudiobookUI_titleScrollNeedsRender()) dirty = 1;

            if (AudiobookProgress_needsRefresh()) {
                AudiobookProgress_renderGPU();
            }

            // Repaint once a second so the sleep countdown stays current
            if (sleep_deadline != 0) {
                static uint32_t last_sleep_second = 0;
                uint32_t now = SDL_GetTicks();
                uint32_t remaining = (sleep_deadline > now) ? (sleep_deadline - now) / 1000 : 0;
                if (remaining != last_sleep_second) {
                    last_sleep_second = remaining;
                    dirty = 1;
                }
            }

            if (AudiobookModule_isActive() && ModuleCommon_checkAutoScreenOffTimeout()) {
                clear_and_show_screen_off_hint(screen);
                continue;
            }
        }

        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            ModuleCommon_PWR_update(&dirty, &show_setting);
        }

        // Render
        if (dirty && !screen_off) {
            if (ModuleCommon_isScreenOffHintActive()) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
            } else {
                switch (state) {
                    case AUDIOBOOK_STATE_LIBRARY:
                        render_audiobook_library(screen, show_setting, library_selected,
                                                 &library_scroll);
                        break;
                    case AUDIOBOOK_STATE_CHAPTERS:
                        render_audiobook_chapters(screen, show_setting, current_book(),
                                                  chapters_selected, &chapters_scroll,
                                                  playback_started ? current_chapter : -1);
                        break;
                    case AUDIOBOOK_STATE_SEEKING:
                        render_audiobook_playing(screen, show_setting, current_book(),
                                                 current_chapter, NULL);
                        break;
                    case AUDIOBOOK_STATE_PLAYING: {
                        char sleep_buffer[32];
                        render_audiobook_playing(screen, show_setting, current_book(),
                                                 current_chapter,
                                                 sleep_timer_label(sleep_buffer, sizeof(sleep_buffer)));
                        break;
                    }
                }
            }

            if (show_setting && state != AUDIOBOOK_STATE_PLAYING) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        } else if (!screen_off) {
            GFX_sync();
        }
    }
}

bool AudiobookModule_isActive(void) {
    return playback_started && Player_getState() != PLAYER_STATE_STOPPED;
}

void AudiobookModule_backgroundTick(void) {
    audiobook_tick();
}

void AudiobookModule_stop(void) {
    if (!playback_started) return;
    save_progress_now();
    Player_stop();
    cleanup_album_art_background();
    playback_started = false;
    playback_confirmed = false;
    sleep_deadline = 0;
    sleep_at_chapter_end = false;
    sleep_option = 0;
}
