#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_album_art.h"
#include "ui_audiobook.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_utils.h"
#include "module_common.h"
#include "player.h"

// Marquee state: one for the selected list row, one for the playing title
static ScrollTextState list_scroll = {0};
static ScrollTextState playing_title_scroll = {0};

// Progress bar geometry + context, captured during the full redraw and reused
// by the once-a-second GPU refresh.
static bool progress_ready = false;
static int progress_bar_x, progress_bar_y, progress_bar_w, progress_bar_h;
static int progress_screen_w;
static int progress_chapter_duration_ms;
static int progress_book_offset_ms;   // Start of this chapter within the book
static int progress_book_total_ms;
static bool progress_single_file;     // Player position is book-absolute, not chapter-relative
static int progress_last_second = -1;

// Format as H:MM:SS, dropping the hour field for short spans
static void format_hms(char* buf, size_t size, int ms) {
    if (ms < 0) ms = 0;
    int total = ms / 1000;
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    if (h > 0) snprintf(buf, size, "%d:%02d:%02d", h, m, s);
    else       snprintf(buf, size, "%02d:%02d", m, s);
}

// ---------------------------------------------------------------------------
// Library list
// ---------------------------------------------------------------------------

// Draw one two-row pill: bold title on row 1, grey detail on row 2
static void render_book_row(SDL_Surface* screen, ListLayout* layout, int y, bool selected,
                            const char* title, const char* subtitle) {
    char truncated[256];
    ListItemRichPos pos = render_list_item_pill_rich(screen, layout, title, subtitle,
                                                     truncated, y, selected, false, 0);

    render_list_item_text(screen, selected ? &list_scroll : NULL, title,
                          Fonts_getMedium(), pos.title_x, pos.title_y,
                          pos.text_max_width, selected);

    GFX_truncateText(Fonts_getSmall(), subtitle, truncated, pos.text_max_width, 0);
    SDL_Surface* sub = TTF_RenderUTF8_Blended(Fonts_getSmall(), truncated,
                                              Fonts_getListTextColor(selected));
    if (sub) {
        SDL_BlitSurface(sub, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
        SDL_FreeSurface(sub);
    }
}

// State of a book, for the second row of its list entry
static void describe_book(const Audiobook* book, char* out, size_t size) {
    if (book->finished) {
        snprintf(out, size, "Finished");
    } else if (book->last_played > 0) {
        char position[32];
        format_hms(position, sizeof(position), book->position_ms);
        snprintf(out, size, "Chapter %d of %d  ·  %s",
                 book->current_chapter + 1, book->chapter_count, position);
    } else {
        char total[32];
        format_hms(total, sizeof(total), book->total_duration_ms);
        snprintf(out, size, "%d chapters  ·  %s", book->chapter_count, total);
    }
}

void render_audiobook_library(SDL_Surface* screen, int show_setting,
                              int selected, int* scroll) {
    GFX_clear(screen);
    render_screen_header(screen, "Audiobook", show_setting);

    int count = Audiobook_getCount();
    if (count == 0) {
        render_empty_state(screen, "No audiobooks found",
                           "Copy books into /Audiobook on the SD card", NULL);
        return;
    }

    // Rich pills are 1.5x the standard row height, so the list steps by that —
    // calc_list_layout()'s item_h is sized for plain single-row pills.
    ListLayout layout = calc_list_layout(screen);
    int item_h = SCALE1(PILL_SIZE) * 3 / 2;
    int items_per_page = layout.list_h / item_h;
    if (items_per_page < 1) items_per_page = 1;

    int resume_index = Audiobook_getResumeIndex();
    int has_resume = (resume_index >= 0) ? 1 : 0;
    int total = has_resume + count;

    adjust_list_scroll(selected, scroll, items_per_page);

    for (int i = 0; i < items_per_page && (*scroll + i) < total; i++) {
        int idx = *scroll + i;
        bool is_selected = (idx == selected);
        int y = layout.list_y + i * item_h;

        // Pinned resume row, mirroring the podcast module's Continue Listening
        if (has_resume && idx == 0) {
            Audiobook* book = Audiobook_get(resume_index);
            if (!book) continue;
            char position[32];
            char subtitle[320];
            format_hms(position, sizeof(position), book->position_ms);
            snprintf(subtitle, sizeof(subtitle), "%s  ·  Ch. %d  ·  %s",
                     book->title, book->current_chapter + 1, position);
            render_book_row(screen, &layout, y, is_selected, "Continue", subtitle);
            continue;
        }

        Audiobook* book = Audiobook_get(idx - has_resume);
        if (!book) continue;

        char subtitle[128];
        describe_book(book, subtitle, sizeof(subtitle));
        render_book_row(screen, &layout, y, is_selected, book->title, subtitle);
    }

    render_scroll_indicators(screen, *scroll, items_per_page, total);
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "PLAY", NULL}, 1, screen, 1);
}

// ---------------------------------------------------------------------------
// Chapter list
// ---------------------------------------------------------------------------

void render_audiobook_chapters(SDL_Surface* screen, int show_setting,
                               const Audiobook* book, int selected, int* scroll,
                               int playing_chapter) {
    GFX_clear(screen);
    render_screen_header(screen, book ? book->title : "Chapters", show_setting);

    int count = Audiobook_getChapterCount();
    if (count == 0) {
        render_empty_state(screen, "No chapters", "This book has no playable files", NULL);
        return;
    }

    ListLayout layout = calc_list_layout(screen);
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    char truncated[256];
    for (int i = 0; i < layout.items_per_page && (*scroll + i) < count; i++) {
        int idx = *scroll + i;
        bool is_selected = (idx == selected);
        int y = layout.list_y + i * layout.item_h;

        AudiobookChapter* chapter = Audiobook_getChapter(idx);
        if (!chapter) continue;

        char label[320];
        char duration[32];
        format_hms(duration, sizeof(duration), chapter->duration_ms);
        snprintf(label, sizeof(label), "%s%s  (%s)",
                 idx == playing_chapter ? "> " : "", chapter->title, duration);

        ListItemPos pos = render_list_item_pill(screen, &layout, label, truncated, y, is_selected, 0);
        int available = pos.pill_width - SCALE1(BUTTON_PADDING * 2);
        render_list_item_text(screen, is_selected ? &list_scroll : NULL, label,
                              Fonts_getMedium(), pos.text_x, pos.text_y, available, is_selected);
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, count);
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "PLAY", NULL}, 1, screen, 1);
}

// ---------------------------------------------------------------------------
// Now playing
// ---------------------------------------------------------------------------

void render_audiobook_playing(SDL_Surface* screen, int show_setting,
                              const Audiobook* book, int chapter_index,
                              const char* sleep_label) {
    GFX_clear(screen);

    // Cover art (embedded in the chapter file) as a triangular background,
    // matching the Music now-playing screen
    SDL_Surface* cover = Player_getAlbumArt();
    if (cover && cover->w > 0 && cover->h > 0) {
        render_album_art_background(screen, cover);
    }

    int hw = screen->w;
    int hh = screen->h;
    int max_text_w = hw - SCALE1(PADDING) * 2;
    char truncated[320];

    render_screen_header(screen, book ? book->title : "Audiobook", show_setting);

    AudiobookChapter* chapter = Audiobook_getChapter(chapter_index);
    int y = SCALE1(PADDING) + SCALE1(PILL_SIZE) + SCALE1(16);

    // Author
    if (book && book->author[0]) {
        GFX_truncateText(Fonts_getSmall(), book->author, truncated, max_text_w, 0);
        SDL_Surface* author = TTF_RenderUTF8_Blended(Fonts_getSmall(), truncated, COLOR_GRAY);
        if (author) {
            SDL_BlitSurface(author, NULL, screen, &(SDL_Rect){SCALE1(PADDING), y});
            y += author->h + SCALE1(6);
            SDL_FreeSurface(author);
        }
    }

    // Chapter title, marquee'd on the GPU layer when it overflows
    const char* chapter_title = chapter ? chapter->title : "";
    if (strcmp(playing_title_scroll.text, chapter_title) != 0) {
        ScrollText_reset(&playing_title_scroll, chapter_title, Fonts_getTitle(), max_text_w, true);
    }
    ScrollText_activateAfterDelay(&playing_title_scroll);
    if (playing_title_scroll.needs_scroll) {
        playing_title_scroll.last_x     = SCALE1(PADDING);
        playing_title_scroll.last_y     = y;
        playing_title_scroll.last_font  = Fonts_getTitle();
        playing_title_scroll.last_color = COLOR_WHITE;
        ScrollText_paintGPU(&playing_title_scroll, Fonts_getTitle(), COLOR_WHITE,
                            SCALE1(PADDING), y, LAYER_SCROLLTEXT);
    } else {
        PLAT_clearLayers(LAYER_SCROLLTEXT);
        SDL_Surface* title = TTF_RenderUTF8_Blended(Fonts_getTitle(), chapter_title, COLOR_WHITE);
        if (title) {
            SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){SCALE1(PADDING), y});
            SDL_FreeSurface(title);
        }
    }
    y += TTF_FontHeight(Fonts_getTitle()) + SCALE1(6);

    // "Chapter 3 of 24" + paused / sleep-timer state on one line
    {
        char status[160];
        int written = snprintf(status, sizeof(status), "Chapter %d of %d",
                               chapter_index + 1, Audiobook_getChapterCount());
        if (Player_getState() == PLAYER_STATE_PAUSED) {
            written += snprintf(status + written, sizeof(status) - written, "  ·  Paused");
        }
        if (sleep_label && sleep_label[0]) {
            snprintf(status + written, sizeof(status) - written, "  ·  Sleep %s", sleep_label);
        }
        SDL_Surface* surf = TTF_RenderUTF8_Blended(Fonts_getSmall(), status, COLOR_GRAY);
        if (surf) {
            SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), y});
            SDL_FreeSurface(surf);
        }
    }

    // Progress bar geometry — the bar itself is drawn on the GPU layer
    progress_bar_x = SCALE1(PADDING);
    progress_bar_y = hh - SCALE1(35);
    progress_bar_h = SCALE1(4);
    progress_bar_w = hw - progress_bar_x * 2;
    progress_screen_w = hw;
    progress_chapter_duration_ms = chapter && chapter->duration_ms > 0
                                 ? chapter->duration_ms : Player_getDuration();
    progress_book_offset_ms = Audiobook_getChapterBookOffsetMs(chapter_index);
    progress_book_total_ms = book ? book->total_duration_ms : 0;
    progress_single_file = book ? book->single_file : false;
    progress_ready = true;
    progress_last_second = -1;  // Force a repaint on the next tick
}

// ---------------------------------------------------------------------------
// GPU progress bar
// ---------------------------------------------------------------------------

bool AudiobookProgress_needsRefresh(void) {
    if (!progress_ready) return false;
    if (Player_getState() != PLAYER_STATE_PLAYING) return false;
    return (Player_getPosition() / 1000) != progress_last_second;
}

void AudiobookProgress_renderGPU(void) {
    if (!progress_ready) return;

    int player_pos_ms = Player_getPosition();
    int second = player_pos_ms / 1000;
    if (second == progress_last_second) return;
    progress_last_second = second;

    // A single-file book plays one long file, so the player position is already
    // measured from the start of the book. Multi-file books restart at zero on
    // every chapter.
    int chapter_elapsed_ms = progress_single_file
                           ? player_pos_ms - progress_book_offset_ms
                           : player_pos_ms;
    int book_elapsed_ms = progress_single_file
                        ? player_pos_ms
                        : progress_book_offset_ms + player_pos_ms;

    if (chapter_elapsed_ms < 0) chapter_elapsed_ms = 0;
    if (progress_chapter_duration_ms > 0 && chapter_elapsed_ms > progress_chapter_duration_ms) {
        chapter_elapsed_ms = progress_chapter_duration_ms;
    }
    if (progress_book_total_ms > 0 && book_elapsed_ms > progress_book_total_ms) {
        book_elapsed_ms = progress_book_total_ms;
    }

    int fill_w = 0;
    if (progress_chapter_duration_ms > 0) {
        fill_w = (int)(((int64_t)progress_bar_w * chapter_elapsed_ms) / progress_chapter_duration_ms);
        if (fill_w > progress_bar_w) fill_w = progress_bar_w;
        if (fill_w < 0) fill_w = 0;
    }

    int gap = SCALE1(8);
    int text_h = TTF_FontHeight(Fonts_getTiny());
    int total_h = progress_bar_h + gap + text_h;

    SDL_Surface* combined = SDL_CreateRGBSurfaceWithFormat(0, progress_screen_w, total_h, 32,
                                                           SDL_PIXELFORMAT_ARGB8888);
    if (!combined) return;
    SDL_FillRect(combined, NULL, 0);

    SDL_FillRect(combined, &(SDL_Rect){progress_bar_x, 0, progress_bar_w, progress_bar_h},
                 SDL_MapRGBA(combined->format, 60, 60, 60, 255));
    if (fill_w > 0) {
        SDL_FillRect(combined, &(SDL_Rect){progress_bar_x, 0, fill_w, progress_bar_h},
                     SDL_MapRGBA(combined->format, 255, 255, 255, 255));
    }

    // Left: time left in this chapter. Right: progress through the whole book.
    char left[32];
    char right[48];
    int remaining = progress_chapter_duration_ms - chapter_elapsed_ms;
    char remaining_text[32];
    format_hms(remaining_text, sizeof(remaining_text), remaining > 0 ? remaining : 0);
    snprintf(left, sizeof(left), "-%s", remaining_text);

    if (progress_book_total_ms > 0) {
        char elapsed_text[32], total_text[32];
        format_hms(elapsed_text, sizeof(elapsed_text), book_elapsed_ms);
        format_hms(total_text, sizeof(total_text), progress_book_total_ms);
        snprintf(right, sizeof(right), "%s / %s", elapsed_text, total_text);
    } else {
        format_hms(right, sizeof(right), chapter_elapsed_ms);
    }

    SDL_Surface* left_surf = TTF_RenderUTF8_Blended(Fonts_getTiny(), left, COLOR_GRAY);
    if (left_surf) {
        SDL_BlitSurface(left_surf, NULL, combined,
                        &(SDL_Rect){progress_bar_x, progress_bar_h + gap});
        SDL_FreeSurface(left_surf);
    }
    SDL_Surface* right_surf = TTF_RenderUTF8_Blended(Fonts_getTiny(), right, COLOR_GRAY);
    if (right_surf) {
        SDL_BlitSurface(right_surf, NULL, combined,
                        &(SDL_Rect){progress_screen_w - progress_bar_x - right_surf->w,
                                    progress_bar_h + gap});
        SDL_FreeSurface(right_surf);
    }

    PLAT_clearLayers(LAYER_AUDIOBOOK_PROGRESS);
    PLAT_drawOnLayer(combined, 0, progress_bar_y, progress_screen_w, total_h, 1.0f, false,
                     LAYER_AUDIOBOOK_PROGRESS);
    SDL_FreeSurface(combined);
    PLAT_GPU_Flip();
}

void AudiobookProgress_clear(void) {
    progress_ready = false;
    progress_last_second = -1;
    PLAT_clearLayers(LAYER_AUDIOBOOK_PROGRESS);
}

// ---------------------------------------------------------------------------
// Marquee plumbing
// ---------------------------------------------------------------------------

bool AudiobookUI_isTitleScrolling(void) {
    if (ScrollText_isScrolling(&list_scroll)) return true;
    if (Player_getState() != PLAYER_STATE_PLAYING) return false;
    return ScrollText_isScrolling(&playing_title_scroll);
}

bool AudiobookUI_titleScrollNeedsRender(void) {
    return ScrollText_needsRender(&list_scroll) || ScrollText_needsRender(&playing_title_scroll);
}

void AudiobookUI_animateTitleScroll(void) {
    if (ScrollText_isScrolling(&list_scroll)) {
        ScrollText_animateOnly(&list_scroll);
    }
    if (Player_getState() != PLAYER_STATE_PLAYING) return;
    if (ScrollText_isScrolling(&playing_title_scroll)) {
        ScrollText_paintGPU(&playing_title_scroll,
                            playing_title_scroll.last_font,
                            playing_title_scroll.last_color,
                            playing_title_scroll.last_x,
                            playing_title_scroll.last_y,
                            LAYER_SCROLLTEXT);
    }
}

void AudiobookUI_clearTitleScroll(void) {
    memset(&list_scroll, 0, sizeof(list_scroll));
    GFX_clearLayers(LAYER_SCROLLTEXT);
    GFX_resetScrollText();
    PLAT_GPU_Flip();
}

void AudiobookUI_clearPlayingScreen(void) {
    memset(&playing_title_scroll, 0, sizeof(playing_title_scroll));
    AudiobookProgress_clear();
    GFX_clearLayers(LAYER_SCROLLTEXT);
    GFX_resetScrollText();
    PLAT_GPU_Flip();
}
