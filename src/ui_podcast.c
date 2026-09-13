#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "podcast.h"
#include "ui_podcast.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_layers.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "ui_album_art.h"
#include "wget_fetch.h"
#include "module_common.h"
#include "podcast_episode_layout.h"
#include "utf8.h"

// Scroll state for selected item title in lists
static ScrollTextState podcast_title_scroll = {0};

SDL_Surface* Podcast_surfaceToArgb8888(SDL_Surface* src) {
    if (!src) return NULL;

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(src);
    return converted;
}

// JPEG: ends with FF D9, PNG: ends with IEND chunk
bool Podcast_imageIsComplete(const uint8_t* data, int size) {
    if (size < 4) return false;
    // JPEG: starts with FF D8, ends with FF D9
    if (data[0] == 0xFF && data[1] == 0xD8) {
        return (data[size - 2] == 0xFF && data[size - 1] == 0xD9);
    }
    // PNG: starts with 89 50 4E 47, ends with IEND chunk (AE 42 60 82)
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        return (size >= 8 &&
                data[size - 4] == 0xAE && data[size - 3] == 0x42 &&
                data[size - 2] == 0x60 && data[size - 1] == 0x82);
    }
    // Unknown format — assume complete
    return true;
}

// Thumbnail cache for subscription artwork on main page
#define THUMBNAIL_CACHE_SIZE 64
typedef struct {
    char feed_id[17];
    SDL_Surface* thumbnail;
} ThumbnailCacheEntry;
static ThumbnailCacheEntry thumbnail_cache[THUMBNAIL_CACHE_SIZE];
static int thumbnail_cache_count = 0;

// Scale surface to size x size and apply circular mask
static SDL_Surface* load_circular_thumbnail_from_surface(SDL_Surface* raw, int size) {
    if (!raw) return NULL;

    // Convert to ARGB8888 for proper scaling
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!converted) return NULL;

    // Scale to size x size
    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!scaled) { SDL_FreeSurface(converted); return NULL; }
    SDL_Rect src = {0, 0, converted->w, converted->h};
    SDL_Rect dst = {0, 0, size, size};
    SDL_BlitScaled(converted, &src, scaled, &dst);
    SDL_FreeSurface(converted);

    // Apply circular mask
    int radius = size / 2;
    uint32_t* pixels = (uint32_t*)scaled->pixels;
    int pitch = scaled->pitch / 4;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int dx = x - radius;
            int dy = y - radius;
            if (dx * dx + dy * dy > radius * radius) {
                pixels[y * pitch + x] = 0;  // Fully transparent
            }
        }
    }

    return scaled;
}

// Load image file from disk path, scale to size x size, apply circular mask
// Deletes corrupt/incomplete files so they get re-fetched
static SDL_Surface* load_circular_thumbnail(const char* path, int size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 1024 * 1024) { fclose(f); return NULL; }

    uint8_t* data = (uint8_t*)malloc(fsize);
    if (!data) { fclose(f); return NULL; }
    if ((long)fread(data, 1, fsize, f) != fsize) { free(data); fclose(f); return NULL; }
    fclose(f);

    // Validate image completeness
    if (!Podcast_imageIsComplete(data, fsize)) {
        free(data);
        remove(path);  // Delete corrupt file so it gets re-fetched
        return NULL;
    }

    SDL_RWops* rw = SDL_RWFromConstMem(data, fsize);
    SDL_Surface* raw = NULL;
    if (rw) raw = IMG_Load_RW(rw, 1);
    free(data);
    if (!raw) { remove(path); return NULL; }

    SDL_Surface* result = load_circular_thumbnail_from_surface(raw, size);
    SDL_FreeSurface(raw);
    return result;
}

// Add thumbnail to in-memory cache (FIFO eviction)
static void cache_thumbnail(const char* cache_key, SDL_Surface* surface) {
    if (thumbnail_cache_count >= THUMBNAIL_CACHE_SIZE) {
        SDL_FreeSurface(thumbnail_cache[0].thumbnail);
        for (int i = 0; i < THUMBNAIL_CACHE_SIZE - 1; i++) {
            thumbnail_cache[i] = thumbnail_cache[i + 1];
        }
        thumbnail_cache_count = THUMBNAIL_CACHE_SIZE - 1;
    }
    ThumbnailCacheEntry* entry = &thumbnail_cache[thumbnail_cache_count];
    strncpy(entry->feed_id, cache_key, sizeof(entry->feed_id) - 1);
    entry->feed_id[sizeof(entry->feed_id) - 1] = '\0';
    entry->thumbnail = surface;
    thumbnail_cache_count++;
}

// Look up thumbnail in memory cache
static SDL_Surface* find_cached_thumbnail(const char* cache_key) {
    for (int i = 0; i < thumbnail_cache_count; i++) {
        if (strcmp(thumbnail_cache[i].feed_id, cache_key) == 0 && thumbnail_cache[i].thumbnail) {
            return thumbnail_cache[i].thumbnail;
        }
    }
    return NULL;
}

// Lazy load one subscription thumbnail from disk (call once per frame)
// Returns true if a thumbnail was loaded (caller should redraw)
static bool subscription_thumb_load_one(const char* feed_id, int size) {
    if (!feed_id || !feed_id[0] || size <= 0) return false;

    // Already in memory cache
    if (find_cached_thumbnail(feed_id)) return false;

    // Load from disk: <feed_data_dir>/artwork.jpg
    char feed_dir[512];
    Podcast_getFeedDataPath(feed_id, feed_dir, sizeof(feed_dir));
    char art_path[768];
    snprintf(art_path, sizeof(art_path), "%s/artwork.jpg", feed_dir);

    SDL_Surface* thumb = load_circular_thumbnail(art_path, size);
    if (thumb) {
        cache_thumbnail(feed_id, thumb);
        return true;
    }
    return false;
}

// Disk cache path for artwork by itunes_id
#define PODCAST_CACHE_PARENT SDCARD_PATH "/.cache"
#define PODCAST_CACHE_DIR    SDCARD_PATH "/.cache/podcast"

static void get_artwork_cache_path(const char* itunes_id, char* path, int path_size) {
    snprintf(path, path_size, PODCAST_CACHE_DIR "/%s.jpg", itunes_id);
}

// Get artwork thumbnail: memory cache -> disk cache -> NULL (non-blocking)
static SDL_Surface* get_artwork_thumbnail(const char* itunes_id, int size) {
    if (!itunes_id || !itunes_id[0] || size <= 0) return NULL;

    // Check memory cache
    SDL_Surface* cached = find_cached_thumbnail(itunes_id);
    if (cached) return cached;

    // Check disk cache
    char cache_path[768];
    get_artwork_cache_path(itunes_id, cache_path, sizeof(cache_path));
    SDL_Surface* thumb = load_circular_thumbnail(cache_path, size);
    if (thumb) {
        cache_thumbnail(itunes_id, thumb);
        return thumb;
    }

    return NULL;
}

// Lazy fetch: download one artwork from network, save to disk, cache in memory
// Returns true if an image was fetched (caller should break to limit one per frame)
static bool artwork_fetch_one(const char* itunes_id, const char* artwork_url, int size) {
    if (!itunes_id || !itunes_id[0] || !artwork_url || !artwork_url[0] || size <= 0) return false;

    // Already in memory
    if (find_cached_thumbnail(itunes_id)) return false;

    // Already on disk
    char cache_path[768];
    get_artwork_cache_path(itunes_id, cache_path, sizeof(cache_path));
    SDL_Surface* thumb = load_circular_thumbnail(cache_path, size);
    if (thumb) {
        cache_thumbnail(itunes_id, thumb);
        return true;
    }

    // Fetch from network
    static uint8_t art_buf[PODCAST_ARTWORK_MAX_SIZE];
    int dl_size = wget_fetch_bytes(artwork_url, art_buf, PODCAST_ARTWORK_MAX_SIZE);
    if (dl_size <= 0 || !Podcast_imageIsComplete(art_buf, dl_size)) return false;

    // Save to disk cache
    mkdir(PODCAST_CACHE_PARENT, 0755);
    mkdir(PODCAST_CACHE_DIR, 0755);
    FILE* f = fopen(cache_path, "wb");
    if (f) {
        fwrite(art_buf, 1, dl_size, f);
        fclose(f);
    }

    // Load into surface from memory
    SDL_RWops* rw = SDL_RWFromConstMem(art_buf, dl_size);
    if (!rw) return false;
    SDL_Surface* raw = IMG_Load_RW(rw, 1);
    if (!raw) return false;

    thumb = load_circular_thumbnail_from_surface(raw, size);
    SDL_FreeSurface(raw);
    if (!thumb) return false;

    cache_thumbnail(itunes_id, thumb);
    return true;
}

void Podcast_clearThumbnailCache(void) {
    for (int i = 0; i < thumbnail_cache_count; i++) {
        if (thumbnail_cache[i].thumbnail) {
            SDL_FreeSurface(thumbnail_cache[i].thumbnail);
            thumbnail_cache[i].thumbnail = NULL;
        }
        thumbnail_cache[i].feed_id[0] = '\0';
    }
    thumbnail_cache_count = 0;
}

bool Podcast_loadPendingThumbnails(void) {
    int sub_count = Podcast_getSubscriptionCount();
    if (sub_count == 0) return false;

    PodcastFeed* feeds = Podcast_getSubscriptions(NULL);
    int thumb_size = SCALE1(PILL_SIZE) * 3 / 2 - SCALE1(4) * 2;

    for (int i = 0; i < sub_count; i++) {
        if (subscription_thumb_load_one(feeds[i].feed_id, thumb_size)) return true;
    }
    return false;
}

// Episode header artwork (square with rounded corners, cached per feed)
static SDL_Surface* episode_header_art = NULL;
static char episode_header_feed_id[17] = {0};
static int episode_header_art_size = 0;

// Load image file, scale to size x size, apply rounded corner mask
// Deletes corrupt/incomplete files so they get re-fetched
static SDL_Surface* load_rounded_thumbnail(const char* path, int size, int radius) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 1024 * 1024) { fclose(f); return NULL; }

    uint8_t* data = (uint8_t*)malloc(fsize);
    if (!data) { fclose(f); return NULL; }
    if ((long)fread(data, 1, fsize, f) != fsize) { free(data); fclose(f); return NULL; }
    fclose(f);

    // Validate image completeness
    if (!Podcast_imageIsComplete(data, fsize)) {
        free(data);
        remove(path);
        return NULL;
    }

    SDL_RWops* rw = SDL_RWFromConstMem(data, fsize);
    SDL_Surface* raw = NULL;
    if (rw) raw = IMG_Load_RW(rw, 1);
    free(data);
    if (!raw) { remove(path); return NULL; }

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(raw);
    if (!converted) return NULL;

    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!scaled) { SDL_FreeSurface(converted); return NULL; }
    SDL_Rect src_r = {0, 0, converted->w, converted->h};
    SDL_Rect dst_r = {0, 0, size, size};
    SDL_BlitScaled(converted, &src_r, scaled, &dst_r);
    SDL_FreeSurface(converted);

    if (radius > 0) {
        uint32_t* pixels = (uint32_t*)scaled->pixels;
        int pitch_px = scaled->pitch / 4;
        for (int py = 0; py < size; py++) {
            for (int px = 0; px < size; px++) {
                int cx = -1, cy = -1;
                if (px < radius && py < radius) { cx = radius; cy = radius; }
                else if (px >= size - radius && py < radius) { cx = size - 1 - radius; cy = radius; }
                else if (px < radius && py >= size - radius) { cx = radius; cy = size - 1 - radius; }
                else if (px >= size - radius && py >= size - radius) { cx = size - 1 - radius; cy = size - 1 - radius; }
                if (cx >= 0 && (px - cx) * (px - cx) + (py - cy) * (py - cy) > radius * radius) {
                    pixels[py * pitch_px + px] = 0;
                }
            }
        }
    }

    return scaled;
}

// Get or load episode header artwork (cached by feed_id and size)
static SDL_Surface* get_episode_header_art(const char* feed_id, int size) {
    if (episode_header_art && strcmp(episode_header_feed_id, feed_id) == 0
        && episode_header_art_size == size)
        return episode_header_art;

    if (episode_header_art) {
        SDL_FreeSurface(episode_header_art);
        episode_header_art = NULL;
    }
    episode_header_art_size = 0;
    episode_header_feed_id[0] = '\0';

    char feed_dir[512];
    Podcast_getFeedDataPath(feed_id, feed_dir, sizeof(feed_dir));
    char art_path[768];
    snprintf(art_path, sizeof(art_path), "%s/artwork.jpg", feed_dir);

    episode_header_art = load_rounded_thumbnail(art_path, size, SCALE1(8));
    if (episode_header_art) {
        strncpy(episode_header_feed_id, feed_id, sizeof(episode_header_feed_id) - 1);
        episode_header_feed_id[sizeof(episode_header_feed_id) - 1] = '\0';
        episode_header_art_size = size;
    }
    return episode_header_art;
}

// Management menu item labels (Y button menu)
static const char* podcast_manage_items[] = {
    "Search",
    "Top Shows"
};

// Format duration as HH:MM:SS or MM:SS
void Podcast_formatDuration(char* buf, int seconds) {
    if (seconds <= 0) {
        strcpy(buf, "--:--");
        return;
    }
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        sprintf(buf, "%d:%02d:%02d", h, m, s);
    } else {
        sprintf(buf, "%02d:%02d", m, s);
    }
}

// Format progress/duration pair as "MM:SS/MM:SS" or "H:MM:SS/H:MM:SS"
// When hours is 0, omits hours prefix (e.g., "01:52" not "0:01:52")
static void format_duration_pair(char* buf, int progress_sec, int duration_sec) {
    int p_h = progress_sec / 3600, p_m = (progress_sec % 3600) / 60, p_s = progress_sec % 60;
    int d_h = duration_sec / 3600, d_m = (duration_sec % 3600) / 60, d_s = duration_sec % 60;
    char p_buf[16], d_buf[16];
    if (p_h > 0)
        snprintf(p_buf, 16, "%d:%02d:%02d", p_h, p_m, p_s);
    else
        snprintf(p_buf, 16, "%02d:%02d", p_m, p_s);
    if (d_h > 0)
        snprintf(d_buf, 16, "%d:%02d:%02d", d_h, d_m, d_s);
    else
        snprintf(d_buf, 16, "%02d:%02d", d_m, d_s);
    snprintf(buf, 32, "%s/%s", p_buf, d_buf);
}

// Format date as relative time or date string
static void format_date(char* buf, uint32_t timestamp) {
    if (timestamp == 0) {
        strcpy(buf, "");
        return;
    }

    time_t now = time(NULL);
    time_t pub = (time_t)timestamp;
    int days = (now - pub) / (24 * 3600);

    if (days == 0) {
        strcpy(buf, "Today");
    } else if (days == 1) {
        strcpy(buf, "Yesterday");
    } else if (days < 7) {
        sprintf(buf, "%d days ago", days);
    } else if (days < 30) {
        sprintf(buf, "%d weeks ago", days / 7);
    } else {
        struct tm* tm = localtime(&pub);
        strftime(buf, 32, "%b %d", tm);
    }
}

// --- Section header helper ---
static void render_section_header(SDL_Surface* screen, const char* text, int y) {
    SDL_Surface* surf = TTF_RenderUTF8_Blended(
        Fonts_getSmall(), text, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (surf) {
        SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), y});
        SDL_FreeSurface(surf);
    }
}

// --- Rich list item renderer (artwork + title + subtitle using rich pill) ---
// Generic: works for subscriptions, search results, top shows
// Thumbnails are memory-cache only (non-blocking). Lazy loading done by caller.
static ListItemRichPos render_rich_list_item(SDL_Surface* screen, ListLayout* layout,
    const char* title, const char* subtitle,
    const char* feed_id, const char* itunes_id,
    int y, bool selected, int extra_subtitle_width) {
    char truncated[256];

    // Check memory cache only (non-blocking)
    const char* cache_key = (feed_id && feed_id[0]) ? feed_id :
                            (itunes_id && itunes_id[0]) ? itunes_id : NULL;
    SDL_Surface* thumb = cache_key ? find_cached_thumbnail(cache_key) : NULL;
    bool has_image = (thumb != NULL);

    ListItemRichPos pos = render_list_item_pill_rich(screen, layout, title, subtitle, truncated, y, selected, has_image, extra_subtitle_width);

    if (thumb) {
        SDL_Rect dst = {pos.image_x, pos.image_y, pos.image_size, pos.image_size};
        SDL_BlitScaled(thumb, NULL, screen, &dst);
    }

    // Title (row 1, scrollable when selected) — pass full title, not truncated
    render_list_item_text(screen, selected ? &podcast_title_scroll : NULL,
                          title, Fonts_getMedium(),
                          pos.title_x, pos.title_y, pos.text_max_width, selected);

    // Subtitle (row 2)
    if (subtitle && subtitle[0]) {
        SDL_Surface* s = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), subtitle,
            Theme_getColor(THEME_ROLE_SECONDARY, selected));
        if (s) {
            SDL_Rect src = {0, 0, s->w > pos.text_max_width ? pos.text_max_width : s->w, s->h};
            SDL_BlitSurface(s, &src, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
            SDL_FreeSurface(s);
        }
    }

    return pos;
}

// Render redesigned podcast main page (continue listening + subscriptions with artwork)
void render_podcast_main_page(SDL_Surface* screen, int show_setting,
    int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;

    render_screen_header(screen, "Podcasts", show_setting);

    int cl_count_raw = Podcast_getContinueListeningCount();
    int cl_count = (cl_count_raw > PODCAST_CONTINUE_LISTENING_DISPLAY) ? PODCAST_CONTINUE_LISTENING_DISPLAY : cl_count_raw;
    int sub_count = Podcast_getSubscriptionCount();
    // "Downloads" menu item at the bottom (only when queue is non-empty)
    int dl_queue_count = 0;
    Podcast_getDownloadQueue(&dl_queue_count);
    int has_downloads_item = (dl_queue_count > 0) ? 1 : 0;
    int total = cl_count + sub_count + has_downloads_item;

    // Empty state
    if (total == 0) {
        render_empty_state(screen, "No podcasts subscribed", "Press Y to manage podcasts", "MANAGE");
        return;
    }

    ListLayout layout = calc_list_layout(screen);
    ListLayout pill_layout = layout;  // Keep original PILL_SIZE item_h for pill rendering

    // Item dimensions (both sections use rich pill height)
    int sub_item_h = layout.rich_item_h;
    int cl_item_h = sub_item_h;
    int section_header_h = SCALE1(16);
    // The gap between a section header and its first row. A row keeps its own
    // room under its pill, thus the next section needs no more than this.
    int section_gap = SCALE1(4);

    // Calculate total content height and per-item Y positions
    // We'll compute item positions in a flat array
    int item_y[PODCAST_MAX_CONTINUE_LISTENING + PODCAST_MAX_SUBSCRIPTIONS + 4];  // generous
    int content_y = 0;
    // The stream takes the room of a list: under the top pill row, and one scroll
    // indicator above the bottom pill row
    int base_y = layout.list_y;
    int viewport_h = layout.list_h;

    content_y = 0;

    // Continue Listening section
    if (cl_count > 0) {
        content_y += section_header_h + section_gap;
        for (int i = 0; i < cl_count; i++) {
            item_y[i] = content_y;
            content_y += cl_item_h;
        }
    }

    // Subscriptions section
    if (sub_count > 0) {
        if (cl_count > 0) content_y += section_gap;
        content_y += section_header_h + section_gap;
        for (int i = 0; i < sub_count; i++) {
            item_y[cl_count + i] = content_y;
            content_y += sub_item_h;
        }
    }

    // Downloads item (at the bottom)
    if (has_downloads_item) {
        if (cl_count > 0 || sub_count > 0) content_y += section_gap;
        item_y[cl_count + sub_count] = content_y;
        content_y += sub_item_h;
    }

    int total_content_h = content_y;

    // Adjust scroll to keep selected item visible
    if (selected >= 0 && selected < total) {
        int sel_y = item_y[selected];
        int sel_h = (selected < cl_count) ? cl_item_h : sub_item_h;

        // For first item in a section, include the section header
        int sel_top = sel_y;
        if (selected == 0 && cl_count > 0) {
            sel_top = 0;  // include "Continue Listening" header
        } else if (selected == cl_count && sub_count > 0) {
            sel_top = sel_y - section_header_h - section_gap;
            if (cl_count > 0) sel_top -= section_gap;
        }

        if (sel_top - *scroll < 0) {
            *scroll = sel_top;
        } else if (sel_y + sel_h - *scroll > viewport_h) {
            *scroll = sel_y + sel_h - viewport_h;
        }
    }

    // Clamp scroll
    if (*scroll < 0) *scroll = 0;
    if (total_content_h > viewport_h) {
        if (*scroll > total_content_h - viewport_h)
            *scroll = total_content_h - viewport_h;
    } else {
        *scroll = 0;
    }

    // Set clip rect for list area
    SDL_Rect clip = {0, base_y, hw, viewport_h};
    SDL_SetClipRect(screen, &clip);

    int draw_offset = base_y - *scroll;

    // --- Continue Listening section ---
    int cy = 0;
    if (cl_count > 0) {
        int header_screen_y = draw_offset + cy;
        if (header_screen_y + section_header_h > base_y && header_screen_y < base_y + viewport_h) {
            render_section_header(screen, "Continue Listening", header_screen_y);
        }
        cy += section_header_h + section_gap;

        for (int i = 0; i < cl_count; i++) {
            bool is_selected = (i == selected);
            int y = draw_offset + cy;

            if (y + cl_item_h > base_y && y < base_y + viewport_h) {
                ContinueListeningEntry* entry = Podcast_getContinueListening(i);
                if (entry) {
                    render_rich_list_item(screen, &pill_layout, entry->episode_title, entry->feed_title,
                                          NULL, NULL, y, is_selected, 0);
                }
            }
            cy += cl_item_h;
        }
    }

    // --- Subscriptions section ---
    if (sub_count > 0) {
        if (cl_count > 0) cy += section_gap;
        int header_screen_y = draw_offset + cy;
        if (header_screen_y + section_header_h > base_y && header_screen_y < base_y + viewport_h) {
            render_section_header(screen, "Subscriptions", header_screen_y);
        }
        cy += section_header_h + section_gap;

        PodcastFeed* feeds = Podcast_getSubscriptions(NULL);
        for (int i = 0; i < sub_count; i++) {
            bool is_selected = (cl_count + i == selected);
            int y = draw_offset + cy;

            if (y + sub_item_h > base_y && y < base_y + viewport_h) {
                char ep_str[64];
                snprintf(ep_str, sizeof(ep_str), "%d Episodes", feeds[i].episode_count);

                // Pre-calculate badge width so pill can account for it
                char new_label[16];
                int badge_extra = 0;
                if (feeds[i].new_episode_count > 0) {
                    snprintf(new_label, sizeof(new_label), "%d New", feeds[i].new_episode_count);
                    int label_w = 0;
                    TTF_SizeUTF8(Fonts_getTiny(), new_label, &label_w, NULL);
                    badge_extra = SCALE1(4) + label_w + SCALE1(6);  // gap + text + pill padding
                }

                ListItemRichPos rpos = render_rich_list_item(screen, &pill_layout, feeds[i].title, ep_str, feeds[i].feed_id, NULL, y, is_selected, badge_extra);

                // Render "N New" badge after subtitle text
                if (feeds[i].new_episode_count > 0) {
                    int sub_tw = 0;
                    TTF_SizeUTF8(Fonts_getSmall(), ep_str, &sub_tw, NULL);
                    int small_h = TTF_FontHeight(Fonts_getSmall());

                    SDL_Surface* new_surf = TTF_RenderUTF8_Blended(
                        Fonts_getTiny(), new_label, Theme_getColor(THEME_ROLE_PRIMARY, is_selected));
                    if (new_surf) {
                        int badge_x = rpos.subtitle_x + sub_tw + SCALE1(4);
                        int badge_y = rpos.subtitle_y + (small_h - new_surf->h) / 2;
                        SDL_BlitSurface(new_surf, NULL, screen,
                                        &(SDL_Rect){badge_x, badge_y});
                        SDL_FreeSurface(new_surf);
                    }
                }
            }
            cy += sub_item_h;
        }
    }

    // --- Downloads item ---
    if (has_downloads_item) {
        if (cl_count > 0 || sub_count > 0) cy += section_gap;
        int dl_idx = cl_count + sub_count;
        bool dl_selected = (dl_idx == selected);
        int y = draw_offset + cy;

        if (y + sub_item_h > base_y && y < base_y + viewport_h) {
            char dl_subtitle[64];
            snprintf(dl_subtitle, sizeof(dl_subtitle), "%d Episode%s", dl_queue_count, dl_queue_count != 1 ? "s" : "");

            char truncated_dl[256];
            ListItemBadgedPos pos = render_list_item_pill_badged(screen, &pill_layout, "Downloads", dl_subtitle, truncated_dl, y, dl_selected, 0, 0);

            render_list_item_text(screen, dl_selected ? &podcast_title_scroll : NULL,
                                  "Downloads", Fonts_getMedium(),
                                  pos.text_x, pos.text_y, pos.text_max_width, dl_selected);

            SDL_Surface* sub_surf = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), dl_subtitle,
                Theme_getColor(THEME_ROLE_SECONDARY, dl_selected));
            if (sub_surf) {
                int avail_w = pos.text_max_width;
                SDL_Rect src = {0, 0, sub_surf->w > avail_w ? avail_w : sub_surf->w, sub_surf->h};
                SDL_BlitSurface(sub_surf, &src, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                SDL_FreeSurface(sub_surf);
            }
        }
        cy += sub_item_h;
    }

    SDL_SetClipRect(screen, NULL);
    render_stream_scroll_indicators(screen, &layout, *scroll, total_content_h);

    // Button hints — context dependent
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (has_downloads_item && selected == cl_count + sub_count) {
        // Downloads item selected
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "OPEN", "Y", "MANAGE", NULL}, 1, screen, 1);
    } else if (selected < cl_count) {
        // Continue listening item selected
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "PLAY", "Y", "MANAGE", NULL}, 1, screen, 1);
    } else {
        // Subscription item selected
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SELECT", "Y", "MANAGE", NULL}, 1, screen, 1);
    }

    // Toast
}

// Render the podcast management menu (Y button opens this)
void render_podcast_manage(SDL_Surface* screen, int show_setting,
                           int menu_selected, int menu_scroll, int subscription_count) {
    GFX_clear(screen);

    char truncated[256];
    char label_buf[128];

    render_screen_header(screen, "Manage Podcasts", show_setting);

    // Use common list layout
    ListLayout layout = calc_list_layout(screen);

    adjust_list_scroll(menu_selected, &menu_scroll, layout.items_per_page);

    for (int i = menu_scroll;
         i < PODCAST_MANAGE_COUNT && i - menu_scroll < layout.items_per_page;
         i++) {
        bool selected = (i == menu_selected);
        const char* item_label = podcast_manage_items[i];

        // Render menu item pill
        MenuItemPos pos = render_menu_item_pill(screen, &layout, item_label, truncated,
                                                i - menu_scroll, selected, 0);

        // Render text using standard list item text (consistent colors and font)
        render_list_item_text(screen, NULL, truncated, Fonts_getLarge(),
                              pos.text_x, pos.text_y, layout.max_width, selected);
    }

    render_scroll_indicators(screen, &layout, menu_scroll, PODCAST_MANAGE_COUNT);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SELECT", NULL}, 1, screen, 1);
}

// Render Top Shows list
void render_podcast_top_shows(SDL_Surface* screen, int show_setting,
                               int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;

    render_screen_header(screen, "Top Shows", show_setting);

    const PodcastChartsStatus* status = Podcast_getChartsStatus();

    // Loading state
    if (status->loading) {
        int center_y = screen->h / 2;
        const char* msg = "Loading...";
        SDL_Surface* text = TTF_RenderUTF8_Blended(
            Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        return;
    }

    int count = 0;
    PodcastChartItem* items = Podcast_getTopShows(&count);

    // Empty state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);
        const char* msg = status->error_message[0] ? status->error_message : "No shows available";
        SDL_Surface* text = TTF_RenderUTF8_Blended(
            Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout (rich 2-row items)
    ListLayout layout = calc_list_layout(screen);
    layout.item_h = layout.rich_item_h;
    layout.items_per_page = layout.rich_items_per_page;
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    int thumb_size = SCALE1(PILL_SIZE) * 3 / 2 - SCALE1(4) * 2;  // same as image_size in pill_rich
    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastChartItem* item = &items[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        render_rich_list_item(screen, &layout, item->title, item->author,
                              NULL, item->itunes_id, y, is_selected, 0);
    }

    // Lazy fetch: fetch one uncached artwork per frame for visible items
    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        PodcastChartItem* item = &items[*scroll + i];
        if (artwork_fetch_one(item->itunes_id, item->artwork_url, thumb_size)) break;
    }

    render_scroll_indicators(screen, &layout, *scroll, count);

    // Check if selected item is already subscribed (by iTunes ID)
    bool selected_is_subscribed = false;
    if (selected < count && items[selected].itunes_id[0]) {
        selected_is_subscribed = Podcast_isSubscribedByItunesId(items[selected].itunes_id);
    }

    // Show subscribe button only if not already subscribed, always show refresh
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (selected_is_subscribed) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "UNSUBSCRIBE", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SUBSCRIBE", NULL}, 1, screen, 1);
    }

    // Toast notification
}

// Render search results
void render_podcast_search_results(SDL_Surface* screen, int show_setting,
                                    int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;

    render_screen_header(screen, "Search Results", show_setting);

    const PodcastSearchStatus* status = Podcast_getSearchStatus();

    // Searching state
    if (status->searching) {
        int center_y = screen->h / 2;
        const char* msg = "Searching...";
        SDL_Surface* text = TTF_RenderUTF8_Blended(
            Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        return;
    }

    int count = 0;
    PodcastSearchResult* results = Podcast_getSearchResults(&count);

    // Empty/error state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);
        const char* msg = status->error_message[0] ? status->error_message : "No results found";
        SDL_Surface* text = TTF_RenderUTF8_Blended(
            Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout (rich 2-row items)
    ListLayout layout = calc_list_layout(screen);
    layout.item_h = layout.rich_item_h;
    layout.items_per_page = layout.rich_items_per_page;
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    // Check if selected item is already subscribed
    bool selected_is_subscribed = false;
    if (selected < count && results[selected].feed_url[0]) {
        selected_is_subscribed = Podcast_isSubscribed(results[selected].feed_url);
    }

    int thumb_size = SCALE1(PILL_SIZE) * 3 / 2 - SCALE1(4) * 2;
    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastSearchResult* result = &results[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        render_rich_list_item(screen, &layout, result->title, result->author,
                              NULL, result->itunes_id, y, is_selected, 0);
    }

    // Lazy fetch: fetch one uncached artwork per frame for visible items
    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        PodcastSearchResult* result = &results[*scroll + i];
        if (artwork_fetch_one(result->itunes_id, result->artwork_url, thumb_size)) break;
    }

    render_scroll_indicators(screen, &layout, *scroll, count);

    // Show subscribe/unsubscribe button based on subscription status
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (selected_is_subscribed) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "UNSUBSCRIBE", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SUBSCRIBE", NULL}, 1, screen, 1);
    }

    // Toast notification
}

// Render episode list for a feed
// One line of the wrapped description: a slice of the text of the feed, not a copy.
typedef struct {
    const char* start;
    int         len;
    bool        cut;   // the text goes on after this line, and the line ends in "..."
} SummaryLine;

#define SUMMARY_DESC_LINES 3

// The feed summary as one measurement: the renderer draws what it measured,
// thus the height that the rows follow is the height of what is on the screen.
typedef struct {
    int         art_size;       // 0 where the feed has no artwork
    int         text_x;
    int         text_max_w;
    int         title_h;
    int         author_h;       // 0 where the feed has no author
    int         desc_line_h;
    int         desc_count;
    SummaryLine desc[SUMMARY_DESC_LINES];
    int         height;         // the block and its padding
} FeedSummary;

// The space above and below the summary, and between its lines.
#define SUMMARY_PAD_Y      2
#define SUMMARY_ART_GAP_X  8
#define SUMMARY_TITLE_GAP  1
#define SUMMARY_AUTHOR_GAP 2

// Copies at most `len` bytes of `text` into `buf`, whole characters only, thus
// a slice that a buffer cuts short stays valid UTF-8.
static int copy_slice(char* buf, size_t buf_size, const char* text, int len) {
    if (len >= (int)buf_size) len = buf_size - 1;
    int n = 0;
    while (n < len) {
        int bytes = UTF8_charBytes(text + n);
        if (bytes <= 0 || n + bytes > len) break;
        n += bytes;
    }
    memcpy(buf, text, n);
    buf[n] = '\0';
    return n;
}

static int text_width(TTF_Font* font, const char* text, int len) {
    // The measure takes a bounded copy, thus a slice of any length is safe.
    char buf[512];
    copy_slice(buf, sizeof(buf), text, len);
    int w = 0;
    TTF_SizeUTF8(font, buf, &w, NULL);
    return w;
}

// Wraps the description into at most SUMMARY_DESC_LINES lines of `max_w`. The
// first line break of the text ends it, as before. Each line is a slice of the
// text. The wrap of the platform is not used: it copies the text with no bound,
// and its cut of the last line can split a UTF-8 sequence.
static int wrap_description(TTF_Font* font, const char* text, int max_w, SummaryLine* lines) {
    const char* end = text;
    while (*end && *end != '\n' && *end != '\r') end++;

    int count = 0;
    const char* p = text;
    while (p < end && count < SUMMARY_DESC_LINES) {
        // The longest run of words that fits
        const char* last_fit = NULL;
        const char* q = p;
        while (q < end) {
            const char* word_end = q;
            while (word_end < end && *word_end != ' ') word_end++;
            if (text_width(font, p, (int)(word_end - p)) > max_w) break;
            last_fit = word_end;
            q = word_end;
            while (q < end && *q == ' ') q++;
        }
        if (!last_fit) {
            // One word is wider than the line: the line takes it, and it is cut
            last_fit = q;
            while (last_fit < end && *last_fit != ' ') last_fit++;
        }

        lines[count].start = p;
        lines[count].len   = (int)(last_fit - p);
        lines[count].cut   = false;
        count++;

        p = last_fit;
        while (p < end && *p == ' ') p++;
    }

    if (p < end && count > 0) {
        // The text goes on past the last line, thus the line ends in "..."
        SummaryLine* last = &lines[count - 1];
        int dots = text_width(font, "...", 3);
        while (last->len > 0 && text_width(font, last->start, last->len) + dots > max_w) {
            last->len--;
            // Do not cut a UTF-8 sequence
            while (last->len > 0 && ((unsigned char)last->start[last->len] & 0xC0) == 0x80) last->len--;
        }
        last->cut = true;
    }
    return count;
}

// The height of a full text block of the summary: the title, an author and
// each description line. The artwork takes this height, thus the summary is as
// high as its text and no higher.
static int summary_text_block_height(void) {
    return TTF_FontHeight(Fonts_getMedium()) + SCALE1(SUMMARY_TITLE_GAP)
         + TTF_FontHeight(Fonts_getSmall()) + SCALE1(SUMMARY_AUTHOR_GAP)
         + SUMMARY_DESC_LINES * TTF_FontHeight(Fonts_getTiny());
}

// The summary of the feed on the screen, as measured on its last frame. A
// download in progress redraws the screen each frame, thus the measure is kept.
static FeedSummary summary_cache;
static const PodcastFeed* summary_cache_ptr = NULL;  // the lines point into it
static char        summary_cache_feed[17];
static char        summary_cache_desc[PODCAST_MAX_DESCRIPTION];  // a refresh can change it
static int         summary_cache_w = 0;
static bool        summary_cache_art = false;
static bool        summary_cache_author = false;  // a refresh can add or drop it

static FeedSummary measure_feed_summary(SDL_Surface* screen, const PodcastFeed* feed,
                                        SDL_Surface* art) {
    if (summary_cache_ptr == feed && summary_cache_w == screen->w
        && summary_cache_art == (art != NULL)
        && summary_cache_author == (feed->author[0] != '\0')
        && strcmp(summary_cache_feed, feed->feed_id) == 0
        && strcmp(summary_cache_desc, feed->description) == 0) {
        return summary_cache;
    }

    FeedSummary s = {0};
    int pad = SCALE1(PADDING);

    s.art_size = art ? art->w : 0;

    s.text_x     = art ? pad + s.art_size + SCALE1(SUMMARY_ART_GAP_X) : pad;
    s.text_max_w = screen->w - s.text_x - pad;

    s.title_h  = TTF_FontHeight(Fonts_getMedium());
    s.author_h = feed->author[0] ? TTF_FontHeight(Fonts_getSmall()) : 0;

    s.desc_line_h = TTF_FontHeight(Fonts_getTiny());
    s.desc_count  = feed->description[0]
                  ? wrap_description(Fonts_getTiny(), feed->description, s.text_max_w, s.desc)
                  : 0;

    int text_h = s.title_h + SCALE1(SUMMARY_TITLE_GAP);
    if (s.author_h) text_h += s.author_h + SCALE1(SUMMARY_AUTHOR_GAP);
    text_h += s.desc_count * s.desc_line_h;

    int block = text_h > s.art_size ? text_h : s.art_size;
    s.height = block + 2 * SCALE1(SUMMARY_PAD_Y);

    summary_cache     = s;
    summary_cache_ptr = feed;
    snprintf(summary_cache_feed, sizeof(summary_cache_feed), "%s", feed->feed_id);
    snprintf(summary_cache_desc, sizeof(summary_cache_desc), "%s", feed->description);
    summary_cache_w      = screen->w;
    summary_cache_art    = art != NULL;
    summary_cache_author = feed->author[0] != '\0';
    return s;
}

// Draws the summary with its top at `y`. Draws only what measure_feed_summary()
// counted, thus the two never differ.
static void draw_feed_summary(SDL_Surface* screen, const PodcastFeed* feed, SDL_Surface* art,
                              const FeedSummary* s, int y) {
    int pad = SCALE1(PADDING);
    int top = y + SCALE1(SUMMARY_PAD_Y);
    char truncated[256];

    if (art) {
        SDL_Rect art_dst = {pad, top, s->art_size, s->art_size};
        SDL_BlitScaled(art, NULL, screen, &art_dst);
    }

    int ty = top;

    // Title (medium font, white). The title of a feed fits `truncated`.
    GFX_truncateText(Fonts_getMedium(), feed->title, truncated, s->text_max_w, 0);
    SDL_Surface* t = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), truncated, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (t) {
        SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){s->text_x, ty});
        SDL_FreeSurface(t);
    }
    ty += s->title_h + SCALE1(SUMMARY_TITLE_GAP);

    // Author (small font, gray)
    if (s->author_h) {
        GFX_truncateText(Fonts_getSmall(), feed->author, truncated, s->text_max_w, 0);
        SDL_Surface* a = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (a) {
            SDL_BlitSurface(a, NULL, screen, &(SDL_Rect){s->text_x, ty});
            SDL_FreeSurface(a);
        }
        ty += s->author_h + SCALE1(SUMMARY_AUTHOR_GAP);
    }

    // Description (tiny font, gray), the lines that the measure gave
    for (int i = 0; i < s->desc_count; i++) {
        char line[512];
        copy_slice(line, sizeof(line) - 3, s->desc[i].start, s->desc[i].len);
        if (s->desc[i].cut) strcat(line, "...");
        SDL_Surface* d = TTF_RenderUTF8_Blended(
            Fonts_getTiny(), line, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (d) {
            // One word wider than the line is a line of its own: it stops at the
            // right margin
            SDL_Rect src = {0, 0, d->w > s->text_max_w ? s->text_max_w : d->w, d->h};
            SDL_BlitSurface(d, &src, screen, &(SDL_Rect){s->text_x, ty});
            SDL_FreeSurface(d);
        }
        ty += s->desc_line_h;
    }
}

void render_podcast_episodes(SDL_Surface* screen, int show_setting,
                              int feed_index, int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    PodcastFeed* feed = Podcast_getSubscription(feed_index);
    if (!feed) {
        render_screen_header(screen, "Episodes", show_setting);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    int count = feed->episode_count;

    render_screen_header(screen, feed->title, show_setting);

    // The stream takes the room of a list: under the top pill row, and one scroll
    // indicator above the bottom pill row
    ListLayout list = calc_list_layout(screen);
    int base_y = list.list_y;
    int viewport_h = list.list_h;
    int item_h = list.rich_item_h;

    // The summary and the rows are one stream of content. The summary takes the
    // height of what it draws, and the first row follows it.
    SDL_Surface* art = get_episode_header_art(feed->feed_id, summary_text_block_height());
    FeedSummary summary = measure_feed_summary(screen, feed, art);
    // The summary keeps its own padding, thus the first row follows it with no gap
    PodcastEpisodeLayout geometry = PodcastEpisodeLayout_compute(
        summary.height, 0, viewport_h, item_h, count, selected, *scroll);
    *scroll = geometry.scroll;
    int episodes_start = geometry.first_row_y;
    int draw_offset = base_y - *scroll;

    // Set clip rect for scrollable area
    SDL_Rect clip = {0, base_y, hw, viewport_h};
    SDL_SetClipRect(screen, &clip);

    if (draw_offset + summary.height > base_y && draw_offset < base_y + viewport_h) {
        draw_feed_summary(screen, feed, art, &summary, draw_offset);
    }

    // Empty state: the summary, and the message in the room under it
    if (count == 0) {
        int center_y = draw_offset + summary.height
                     + (viewport_h - summary.height) / 2;
        const char* msg = "No episodes available";
        SDL_Surface* text = TTF_RenderUTF8_Blended(
            Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        SDL_SetClipRect(screen, NULL);
        render_stream_scroll_indicators(screen, &list, *scroll, geometry.content_h);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // === Episodes (pixel-based positioning) ===
    ListLayout layout;
    layout.item_h = item_h;
    layout.max_width = hw - SCALE1(PADDING * 2);

    // Check download status of selected episode for button hints
    int selected_download_status = -1;
    int selected_progress = 0;
    bool selected_is_downloaded = false;
    bool selected_is_resumable = false;
    if (selected < count) {
        PodcastEpisode* sel_ep = Podcast_getEpisode(feed_index, selected);
        if (sel_ep) {
            selected_download_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, sel_ep->guid, &selected_progress);
            selected_is_downloaded = Podcast_episodeFileExists(feed, selected);
            selected_is_resumable = (sel_ep->progress_sec > 0);
        }
    }

    for (int i = 0; i < count; i++) {
        int ep_cy = episodes_start + i * item_h;  // content y
        int y = draw_offset + ep_cy;                  // screen y

        // Skip if entirely outside viewport
        if (y + item_h <= base_y || y >= base_y + viewport_h) continue;

        PodcastEpisode* ep = Podcast_getEpisode(feed_index, i);
        if (!ep) continue;

        bool is_selected = (i == selected);

        // Check episode download status
        int dl_progress = 0;
        int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);

        // Determine badge info
        bool is_downloaded = Podcast_episodeFileExists(feed, i);
        bool is_played = (ep->progress_sec == -1);
        bool has_progress = (ep->progress_sec > 0);

        // Badge icons: complete icon if played, download icon if not downloaded
        int badge_icon_size = SCALE1(14);
        int num_badges = 0;
        if (is_played) num_badges++;
        if (!is_downloaded) num_badges++;
        int badge_width = num_badges > 0 ? num_badges * badge_icon_size : 0;

        // Two-layer capsule pill with subtitle inside
        ListItemBadgedPos pos = render_list_item_pill_badged(screen, &layout, ep->title, NULL, truncated, y, is_selected, badge_width, 0);

        // Title text (row 1)
        render_list_item_text(screen, is_selected ? &podcast_title_scroll : NULL,
                              ep->title, Fonts_getMedium(),
                              pos.text_x, pos.text_y,
                              pos.text_max_width, is_selected);

        // Render badge icons
        if (num_badges > 0) {
            // The badge sits on the accent capsule and not on the selection
            // pill, thus it keeps the colors of an unselected row.
            int bx = pos.badge_x;
            int by = y + (layout.item_h - badge_icon_size) / 2;
            if (is_played) {
                SDL_Surface* icon = Icons_getComplete(THEME_ROLE_PRIMARY, false);
                if (icon) {
                    SDL_Rect src = {0, 0, icon->w, icon->h};
                    SDL_Rect dst = {bx, by, badge_icon_size, badge_icon_size};
                    SDL_BlitScaled(icon, &src, screen, &dst);
                    bx += badge_icon_size + SCALE1(2);
                }
            }
            if (!is_downloaded) {
                SDL_Surface* icon = Icons_getDownload(THEME_ROLE_PRIMARY, false);
                if (icon) {
                    SDL_Rect src = {0, 0, icon->w, icon->h};
                    SDL_Rect dst = {bx, by, badge_icon_size, badge_icon_size};
                    SDL_BlitScaled(icon, &src, screen, &dst);
                }
            }
        }

        // Subtitle (row 2)
        int small_h = TTF_FontHeight(Fonts_getSmall());
        int subtitle_x_offset = 0;

        // Render "New" badge pill if episode is new
        if (ep->is_new) {
            SDL_Surface* new_surf = TTF_RenderUTF8_Blended(
                Fonts_getTiny(), "New", Theme_getColor(THEME_ROLE_PRIMARY, is_selected));
            if (new_surf) {
                int badge_y = pos.subtitle_y + (small_h - new_surf->h) / 2;
                SDL_BlitSurface(new_surf, NULL, screen,
                                &(SDL_Rect){pos.subtitle_x, badge_y});
                subtitle_x_offset = new_surf->w + SCALE1(4);
                SDL_FreeSurface(new_surf);
            }
        }

        if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING) {
            int bar_w = SCALE1(50);
            int bar_h = SCALE1(4);
            int bar_x = pos.subtitle_x + subtitle_x_offset;
            int bar_y = pos.subtitle_y + (small_h - bar_h) / 2;
            SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
            SDL_FillRect(screen, &bar_bg,
                         Theme_getPackedColor(THEME_ROLE_PROGRESS_TRACK, is_selected));
            int fill_w = (bar_w * dl_progress) / 100;
            if (fill_w > 0) {
                SDL_Rect bar_fill = {bar_x, bar_y, fill_w, bar_h};
                SDL_FillRect(screen, &bar_fill,
                             Theme_getPackedColor(THEME_ROLE_PROGRESS_FILL, is_selected));
            }
        } else if (dl_status == PODCAST_DOWNLOAD_PENDING) {
            SDL_Surface* queued_surf = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), "Queued",
                Theme_getColor(THEME_ROLE_SECONDARY, is_selected));
            if (queued_surf) {
                int avail_w = pos.text_max_width - subtitle_x_offset;
                SDL_Rect src = {0, 0, queued_surf->w > avail_w ? avail_w : queued_surf->w, queued_surf->h};
                SDL_BlitSurface(queued_surf, &src, screen,
                                &(SDL_Rect){pos.subtitle_x + subtitle_x_offset, pos.subtitle_y});
                SDL_FreeSurface(queued_surf);
            }
        } else if (has_progress && ep->duration_sec > 0) {
            char progress_str[64];
            format_duration_pair(progress_str, ep->progress_sec, ep->duration_sec);
            char date_str[32];
            format_date(date_str, ep->pub_date);
            if (date_str[0]) {
                char combined[96];
                snprintf(combined, sizeof(combined), "%s | %s", progress_str, date_str);
                strcpy(progress_str, combined);
            }
            SDL_Surface* s = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), progress_str,
                Theme_getColor(THEME_ROLE_SECONDARY, is_selected));
            if (s) {
                int avail_w = pos.text_max_width - subtitle_x_offset;
                SDL_Rect src = {0, 0, s->w > avail_w ? avail_w : s->w, s->h};
                SDL_BlitSurface(s, &src, screen,
                                &(SDL_Rect){pos.subtitle_x + subtitle_x_offset, pos.subtitle_y});
                SDL_FreeSurface(s);
            }
        } else {
            char subtitle_str[64] = {0};
            if (ep->duration_sec > 0) {
                Podcast_formatDuration(subtitle_str, ep->duration_sec);
            }
            char date_str[32];
            format_date(date_str, ep->pub_date);
            if (date_str[0]) {
                if (subtitle_str[0]) {
                    char combined[96];
                    snprintf(combined, sizeof(combined), "%s | %s", subtitle_str, date_str);
                    strcpy(subtitle_str, combined);
                } else {
                    strcpy(subtitle_str, date_str);
                }
            }
            if (subtitle_str[0]) {
                SDL_Surface* sub_surf = TTF_RenderUTF8_Blended(
                    Fonts_getSmall(), subtitle_str,
                    Theme_getColor(THEME_ROLE_SECONDARY, is_selected));
                if (sub_surf) {
                    int avail_w = pos.text_max_width - subtitle_x_offset;
                    SDL_Rect src = {0, 0, sub_surf->w > avail_w ? avail_w : sub_surf->w, sub_surf->h};
                    SDL_BlitSurface(sub_surf, &src, screen,
                                    &(SDL_Rect){pos.subtitle_x + subtitle_x_offset, pos.subtitle_y});
                    SDL_FreeSurface(sub_surf);
                }
            }
        }
    }

    SDL_SetClipRect(screen, NULL);
    render_stream_scroll_indicators(screen, &list, *scroll, geometry.content_h);

    // Dynamic button hints based on selected episode's state
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (selected_download_status == PODCAST_DOWNLOAD_DOWNLOADING ||
        selected_download_status == PODCAST_DOWNLOAD_PENDING) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "CANCEL", "Y", "REFRESH", NULL}, 1, screen, 1);
    } else if (selected_is_downloaded) {
        const char* play_label = selected_is_resumable ? "RESUME" : "PLAY";
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", (char*)play_label, "Y", "REFRESH", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "DOWNLOAD", "Y", "REFRESH", NULL}, 1, screen, 1);
    }

    // Toast notification
}

// Format download speed for display
static void format_speed(char* buf, int buf_size, int bytes_per_sec) {
    if (bytes_per_sec <= 0) {
        snprintf(buf, buf_size, "0 B/s");
    } else if (bytes_per_sec < 1024) {
        snprintf(buf, buf_size, "%d B/s", bytes_per_sec);
    } else if (bytes_per_sec < 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f KB/s", bytes_per_sec / 1024.0);
    } else {
        snprintf(buf, buf_size, "%.1f MB/s", bytes_per_sec / (1024.0 * 1024.0));
    }
}

// Format ETA for display
static void format_eta(char* buf, int buf_size, int seconds) {
    if (seconds <= 0) {
        buf[0] = '\0';
    } else if (seconds < 60) {
        snprintf(buf, buf_size, "%ds", seconds);
    } else if (seconds < 3600) {
        snprintf(buf, buf_size, "%dm%ds", seconds / 60, seconds % 60);
    } else {
        snprintf(buf, buf_size, "%dh%dm", seconds / 3600, (seconds % 3600) / 60);
    }
}

// Render download queue view
void render_podcast_download_queue(SDL_Surface* screen, int show_setting,
                                    int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    int queue_count = 0;
    PodcastDownloadItem* queue = Podcast_getDownloadQueue(&queue_count);
    const PodcastDownloadProgress* progress = Podcast_getDownloadProgress();

    // Title with completion count
    char title[64];
    if (queue_count > 0) {
        snprintf(title, sizeof(title), "Downloads (%d/%d)",
                 progress->completed_count, progress->total_items);
    } else {
        snprintf(title, sizeof(title), "Downloads");
    }
    render_screen_header(screen, title, show_setting);

    // Empty state
    if (queue_count == 0) {
        int center_y = screen->h / 2;
        const char* msg = "No downloads";
        SDL_Surface* text = TTF_RenderUTF8_Blended(
            Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y - text->h / 2});
            SDL_FreeSurface(text);
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout — use taller item height for two-row pills (title + subtitle)
    ListLayout layout = calc_list_layout(screen);
    layout.item_h = layout.rich_item_h;
    layout.items_per_page = layout.rich_items_per_page;
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *scroll + i < queue_count; i++) {
        int idx = *scroll + i;
        PodcastDownloadItem* item = &queue[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        // Two-layer pill with subtitle
        int badge_width = 0;
        ListItemBadgedPos pos = render_list_item_pill_badged(screen, &layout, item->episode_title, item->feed_title, truncated, y, is_selected, badge_width, 0);

        // Title text (row 1)
        render_list_item_text(screen, is_selected ? &podcast_title_scroll : NULL,
                              item->episode_title, Fonts_getMedium(),
                              pos.text_x, pos.text_y,
                              pos.text_max_width, is_selected);

        // Subtitle (row 2) — status-dependent
        int small_h = TTF_FontHeight(Fonts_getSmall());
        (void)small_h;

        if (item->status == PODCAST_DOWNLOAD_DOWNLOADING) {
            // Progress bar + speed + ETA
            int bar_w = SCALE1(50);
            int bar_h = SCALE1(4);
            int bar_x = pos.subtitle_x;
            int bar_y = pos.subtitle_y + (TTF_FontHeight(Fonts_getSmall()) - bar_h) / 2;

            SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
            SDL_FillRect(screen, &bar_bg,
                         Theme_getPackedColor(THEME_ROLE_PROGRESS_TRACK, is_selected));

            // Bar fill
            int fill_w = (bar_w * item->progress_percent) / 100;
            if (fill_w > 0) {
                SDL_Rect bar_fill = {bar_x, bar_y, fill_w, bar_h};
                SDL_FillRect(screen, &bar_fill,
                             Theme_getPackedColor(THEME_ROLE_PROGRESS_FILL, is_selected));
            }

            // Speed and ETA text after bar
            char info_str[64];
            char speed_str[32];
            char eta_str[32];
            format_speed(speed_str, sizeof(speed_str), progress->speed_bps);
            format_eta(eta_str, sizeof(eta_str), progress->eta_sec);

            if (eta_str[0]) {
                snprintf(info_str, sizeof(info_str), "%d%%  %s  ETA %s",
                         item->progress_percent, speed_str, eta_str);
            } else {
                snprintf(info_str, sizeof(info_str), "%d%%  %s",
                         item->progress_percent, speed_str);
            }

            SDL_Surface* info_surf = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), info_str,
                Theme_getColor(THEME_ROLE_SECONDARY, is_selected));
            if (info_surf) {
                int info_x = bar_x + bar_w + SCALE1(6);
                int avail_w = pos.text_max_width - bar_w - SCALE1(6);
                SDL_Rect src = {0, 0, info_surf->w > avail_w ? avail_w : info_surf->w, info_surf->h};
                SDL_BlitSurface(info_surf, &src, screen,
                                &(SDL_Rect){info_x, pos.subtitle_y});
                SDL_FreeSurface(info_surf);
            }
        } else if (item->status == PODCAST_DOWNLOAD_PENDING) {
            const char* label = "Queued";
            SDL_Surface* s = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), label,
                Theme_getColor(THEME_ROLE_SECONDARY, is_selected));
            if (s) {
                SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                SDL_FreeSurface(s);
            }
        } else if (item->status == PODCAST_DOWNLOAD_FAILED) {
            const char* label = "[Failed]";
            if (item->retry_count > 0) {
                char fail_str[64];
                snprintf(fail_str, sizeof(fail_str), "[Failed after %d retries]", item->retry_count);
                SDL_Surface* s = TTF_RenderUTF8_Blended(
                    Fonts_getSmall(), fail_str, Theme_getColor(THEME_ROLE_STATUS_ERROR, is_selected));
                if (s) {
                    int avail_w = pos.text_max_width;
                    SDL_Rect src = {0, 0, s->w > avail_w ? avail_w : s->w, s->h};
                    SDL_BlitSurface(s, &src, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                    SDL_FreeSurface(s);
                }
            } else {
                SDL_Surface* s = TTF_RenderUTF8_Blended(
                    Fonts_getSmall(), label, Theme_getColor(THEME_ROLE_STATUS_ERROR, is_selected));
                if (s) {
                    SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                    SDL_FreeSurface(s);
                }
            }
        } else if (item->status == PODCAST_DOWNLOAD_COMPLETE) {
            const char* label = "Complete";
            SDL_Surface* s = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), label, Theme_getColor(THEME_ROLE_STATUS_SUCCESS, is_selected));
            if (s) {
                SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                SDL_FreeSurface(s);
            }
        }

        // Feed title as secondary info (tiny font, below subtitle)
        if (item->feed_title[0]) {
            GFX_truncateText(Fonts_getTiny(), item->feed_title, truncated, pos.text_max_width, 0);
            // Render as part of subtitle line if space allows
        }
    }

    // Scroll indicators
    render_scroll_indicators(screen, &layout, *scroll, queue_count);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (queue_count > 0) {
        GFX_blitButtonGroup((char*[]){"X", "REMOVE", "B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }

    // Toast notification
}

// Render now playing screen for podcast (matches radio/music player style)
// Render loading screen
void render_podcast_loading(SDL_Surface* screen, const char* message) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    const char* msg = message ? message : "Loading...";
    SDL_Surface* text = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), msg, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (text) {
        SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, hh / 2});
        SDL_FreeSurface(text);
    }
}

// Check if the title of a row of a list is currently scrolling
bool Podcast_isTitleScrolling(void) {
    return ScrollText_isScrolling(&podcast_title_scroll);
}

// Check if title scroll needs a render to transition from delay to active scrolling
// Returns true during the delay phase when text is wider than max_width but scrolling hasn't started
bool Podcast_titleScrollNeedsRender(void) {
    return ScrollText_needsRender(&podcast_title_scroll);
}

// Animate the title of a list of this module. `ScrollText_animateOnly()` paints
// the animation layer itself, thus it serves a screen where nothing else of
// this module is on that layer.
void Podcast_animateTitleScroll(void) {
    if (ScrollText_isScrolling(&podcast_title_scroll)) {
        ScrollText_animateOnly(&podcast_title_scroll);
    }
}

// Clear podcast title scroll state (call when selection changes or leaving page)
void Podcast_clearTitleScroll(void) {
    ScrollText_forget(&podcast_title_scroll);
    UiLayer_clear(UI_LAYER_ANIMATION);
}
