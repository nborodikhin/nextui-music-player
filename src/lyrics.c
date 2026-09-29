#define _GNU_SOURCE
#include "lyrics.h"
#include "lyric_window.h"
#include "radio_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>

#include "defines.h"
#include "api.h"
#include "include/parson/parson.h"

// Lyrics cache directory path on SD card
#define LYRICS_CACHE_DIR SDCARD_PATH "/.cache/lyrics"
#define CACHE_PARENT_DIR SDCARD_PATH "/.cache"

// Lyrics state (main thread only - written by thread when done)
static LyricLine lyrics_lines[LYRICS_MAX_LINES];
static int lyrics_line_count = 0;
static bool lyrics_synced = false;  // false: plain text with estimated times
static bool lyrics_available = false;

// Dedup tracking
static char last_artist[256] = "";
static char last_title[256] = "";
static char last_path[512] = "";

// Background thread state - uses generation counter instead of pthread_join
// to avoid blocking the main thread on network timeouts
static volatile int fetch_generation = 0;

// Simple hash function for cache filename (DJB2)
static unsigned int simple_hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Ensure cache directory exists
static void ensure_cache_dir(void) {
    mkdir(CACHE_PARENT_DIR, 0755);
    mkdir(LYRICS_CACHE_DIR, 0755);
}

// Get cache file path for artist+title
static void get_cache_filepath(const char* artist, const char* title, char* path, int path_size) {
    char combined[512];
    snprintf(combined, sizeof(combined), "%s - %s", artist ? artist : "", title ? title : "");
    unsigned int hash = simple_hash(combined);
    snprintf(path, path_size, "%s/%08x.lrc", LYRICS_CACHE_DIR, hash);
}

// URL encode a string for use in query parameters
static void url_encode(const char* src, char* dst, int dst_size) {
    const char* hex = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

// Parse LRC text into a line array. Returns number of lines parsed.
static int parse_lrc_text(const char* lrc_text, LyricLine* lines, int max_lines) {
    int count = 0;
    const char* p = lrc_text;

    while (*p && count < max_lines) {
        // Skip whitespace/newlines
        while (*p == '\n' || *p == '\r' || *p == ' ') p++;
        if (!*p) break;

        // Expect '[' for timestamp
        if (*p != '[') {
            while (*p && *p != '\n') p++;
            continue;
        }
        p++; // skip '['

        // Parse mm:ss.xx
        int mm = 0, ss = 0, cs = 0;
        char* end;
        mm = (int)strtol(p, &end, 10);
        if (*end != ':') {
            // Not a timestamp line (metadata like [ar:Artist])
            while (*p && *p != '\n') p++;
            continue;
        }
        p = end + 1; // skip ':'
        ss = (int)strtol(p, &end, 10);
        if (*end == '.') {
            p = end + 1;
            cs = (int)strtol(p, &end, 10);
            // Handle both .xx (centiseconds) and .xxx (milliseconds)
            if (end - p == 3) {
                cs = cs / 10;
            }
        }
        p = end;

        // Skip to ']'
        while (*p && *p != ']') p++;
        if (*p == ']') p++;

        int time_ms = mm * 60000 + ss * 1000 + cs * 10;

        // Copy lyric text until end of line
        int len = 0;
        while (*p && *p != '\n' && *p != '\r' && len < 255) {
            lines[count].text[len++] = *p++;
        }
        lines[count].text[len] = '\0';

        // Skip empty lines
        if (len == 0) continue;

        lines[count].time_ms = time_ms;
        count++;
    }
    return count;
}

// Read a whole text file. Returns a string that the caller frees, or NULL.
static char* read_text_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 256 * 1024) {
        fclose(f);
        return NULL;
    }

    char* data = (char*)malloc(size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    return data;
}

// Weight of a plain line: its character count, with a floor so a short line
// ("Oh", "Yeah") still gets some time
#define PLAIN_MIN_WEIGHT 8
// The words of a song usually start after an intro and end before an outro
#define PLAIN_START_PERMILLE 80
#define PLAIN_END_PERMILLE   920
// Time per line where the duration is not known
#define PLAIN_LINE_MS 4000

// Parse plain lyrics text, and give each line an estimated time: the lines share
// the middle of the track by their length, and a blank line (a break between
// verses) takes the time of an average line. Lines like "[Chorus]" are skipped.
static int parse_plain_text(const char* text, int duration_ms, LyricLine* lines, int max_lines) {
    // The weight of the lines before each line, and the count of the breaks before it
    int* lines_before = (int*)malloc(sizeof(int) * max_lines);
    int* breaks_before = (int*)malloc(sizeof(int) * max_lines);
    if (!lines_before || !breaks_before) {
        free(lines_before);
        free(breaks_before);
        return 0;
    }

    int count = 0;
    int total = 0;
    int breaks = 0;
    bool pending_break = false;
    const char* p = text;

    while (*p && count < max_lines) {
        const char* end = p;
        while (*end && *end != '\n') end++;
        const char* next = *end ? end + 1 : end;

        // Trim the line
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        const char* e = end;
        while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
        int len = (int)(e - p);

        if (len == 0) {
            pending_break = (count > 0);
        } else if (p[0] == '[' && e[-1] == ']') {
            // A section label
        } else {
            if (pending_break) {
                breaks++;
                pending_break = false;
            }
            if (len > 255) len = 255;
            memcpy(lines[count].text, p, len);
            lines[count].text[len] = '\0';

            // Count characters, not bytes: skip UTF-8 continuation bytes
            int chars = 0;
            for (int i = 0; i < len; i++) {
                if (((unsigned char)p[i] & 0xC0) != 0x80) chars++;
            }
            lines_before[count] = total;
            breaks_before[count] = breaks;
            total += chars > PLAIN_MIN_WEIGHT ? chars : PLAIN_MIN_WEIGHT;
            count++;
        }
        p = next;
    }

    if (count > 0) {
        int break_weight = total / count;
        int64_t sum = (int64_t)total + (int64_t)breaks * break_weight;
        int64_t start = (int64_t)duration_ms * PLAIN_START_PERMILLE / 1000;
        int64_t span = (int64_t)duration_ms * (PLAIN_END_PERMILLE - PLAIN_START_PERMILLE) / 1000;
        for (int i = 0; i < count; i++) {
            int64_t before = lines_before[i] + (int64_t)breaks_before[i] * break_weight;
            lines[i].time_ms = duration_ms > 0 ? (int)(start + span * before / sum) : i * PLAIN_LINE_MS;
        }
    }

    free(lines_before);
    free(breaks_before);
    return count;
}

// Parse lyrics text of either kind: LRC if it has timestamps, else plain text.
// Sets *synced to tell which one it was.
static int parse_lyrics_text(const char* text, int duration_ms, LyricLine* lines, int max_lines,
                             bool* synced) {
    int count = parse_lrc_text(text, lines, max_lines);
    *synced = (count > 0);
    if (count == 0) count = parse_plain_text(text, duration_ms, lines, max_lines);
    return count;
}

// Read a lyrics file (a cache entry, or an .lrc file next to the track) into the line array.
// Returns line count, or 0 on failure.
static int load_lyrics_file(const char* path, int duration_ms, LyricLine* lines, int max_lines,
                            bool* synced) {
    char* data = read_text_file(path);
    if (!data) return 0;

    int count = parse_lyrics_text(data, duration_ms, lines, max_lines, synced);
    free(data);
    return count;
}

// Save LRC text to cache file
static void save_lyrics_to_cache(const char* cache_path, const char* lrc_text) {
    FILE* f = fopen(cache_path, "w");
    if (!f) return;
    fputs(lrc_text, f);
    fclose(f);
}

// Thread argument
typedef struct {
    char artist[256];
    char title[256];
    char track_path[512];
    char* embedded;  // lyrics from the tags of the file, owned (NULL if none)
    int duration_sec;
    int generation;  // to detect if this fetch is still current
} FetchArgs;

static void free_fetch_args(FetchArgs* args) {
    free(args->embedded);
    free(args);
}

// Put the parsed lines into the shared state, if this fetch is still current
static void publish_lines(const LyricLine* lines, int count, bool synced, int generation) {
    if (count <= 0 || fetch_generation != generation) return;
    memcpy(lyrics_lines, lines, sizeof(LyricLine) * count);
    lyrics_line_count = count;
    lyrics_synced = synced;
    lyrics_available = true;
}

// The path of the .lrc file next to a track: the extension of the track changed to .lrc
static void get_sidecar_filepath(const char* track_path, char* path, int path_size) {
    snprintf(path, path_size, "%s", track_path);
    char* dot = strrchr(path, '.');
    char* slash = strrchr(path, '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    size_t len = strlen(path);
    snprintf(path + len, path_size - len, ".lrc");
}

// Background fetch thread function (detached — must not touch shared state if stale)
static void* fetch_thread_func(void* arg) {
    PWR_pinToCores(CPU_CORE_EFFICIENCY);
    FetchArgs* args = (FetchArgs*)arg;
    int my_gen = args->generation;

    // Temporary buffer for parsing (thread-local, not shared)
    LyricLine* tmp_lines = (LyricLine*)malloc(sizeof(LyricLine) * LYRICS_MAX_LINES);
    if (!tmp_lines) {
        free_fetch_args(args);
        return NULL;
    }

    int duration_ms = args->duration_sec * 1000;
    bool synced = false;
    int count = 0;

    // The lyrics of the file come first: an .lrc file next to the track, then the tags
    if (args->track_path[0]) {
        char sidecar_path[768];
        get_sidecar_filepath(args->track_path, sidecar_path, sizeof(sidecar_path));
        count = load_lyrics_file(sidecar_path, duration_ms, tmp_lines, LYRICS_MAX_LINES, &synced);
    }
    if (count == 0 && args->embedded) {
        count = parse_lyrics_text(args->embedded, duration_ms, tmp_lines, LYRICS_MAX_LINES, &synced);
    }

    // The cache and LRCLIB find lyrics by artist and title, thus a track with neither stops here
    if (count == 0 && !args->artist[0] && !args->title[0]) {
        free(tmp_lines);
        free_fetch_args(args);
        return NULL;
    }

    ensure_cache_dir();

    char cache_path[768];
    get_cache_filepath(args->artist, args->title, cache_path, sizeof(cache_path));
    // Then the disk cache of earlier downloads
    if (count == 0) {
        count = load_lyrics_file(cache_path, duration_ms, tmp_lines, LYRICS_MAX_LINES, &synced);
    }
    if (count > 0) {
        publish_lines(tmp_lines, count, synced, my_gen);
        free(tmp_lines);
        free_fetch_args(args);
        return NULL;
    }

    // URL-encode artist and title separately
    char encoded_artist[512];
    char encoded_title[512];
    url_encode(args->artist, encoded_artist, sizeof(encoded_artist));
    url_encode(args->title, encoded_title, sizeof(encoded_title));

    // Build LRCLIB exact match URL: /api/get?artist_name=X&track_name=Y&duration=Z
    char url[2048];
    snprintf(url, sizeof(url),
        "https://lrclib.net/api/get?artist_name=%s&track_name=%s&duration=%d",
        encoded_artist, encoded_title, args->duration_sec);
    // Fetch LRCLIB API response
    uint8_t* response_buf = (uint8_t*)malloc(64 * 1024);
    if (!response_buf) {
        free(tmp_lines);
        free_fetch_args(args);
        return NULL;
    }

    const char* synced_lyrics = NULL;
    JSON_Value* root = NULL;

    // Try exact match first
    int bytes = radio_net_fetch(url, response_buf, 64 * 1024, NULL, 0);

    if (bytes > 0) {
        response_buf[bytes] = '\0';
        root = json_parse_string((const char*)response_buf);
        if (root) {
            JSON_Object* obj = json_value_get_object(root);
            if (obj) {
                synced_lyrics = json_object_get_string(obj, "syncedLyrics");
                if (!synced_lyrics || !synced_lyrics[0]) synced_lyrics = NULL;
            }
            if (!synced_lyrics) {
                json_value_free(root);
                root = NULL;
            }
        }
    }

    // Check if we've been superseded before doing fallback fetch
    if (fetch_generation != my_gen) {
        free(response_buf);
        if (root) json_value_free(root);
        free(tmp_lines);
        free_fetch_args(args);
        return NULL;
    }

    // Fallback: fuzzy search if exact match failed
    if (!synced_lyrics) {
        char query[512];
        snprintf(query, sizeof(query), "%s %s", args->artist, args->title);
        char encoded_query[1024];
        url_encode(query, encoded_query, sizeof(encoded_query));
        snprintf(url, sizeof(url), "https://lrclib.net/api/search?q=%s", encoded_query);

        bytes = radio_net_fetch(url, response_buf, 64 * 1024, NULL, 0);

        if (bytes > 0) {
            response_buf[bytes] = '\0';
            root = json_parse_string((const char*)response_buf);
            if (root) {
                JSON_Array* results = json_value_get_array(root);
                if (results && json_array_get_count(results) > 0) {
                    for (size_t i = 0; i < json_array_get_count(results); i++) {
                        JSON_Object* item = json_array_get_object(results, i);
                        if (!item) continue;
                        synced_lyrics = json_object_get_string(item, "syncedLyrics");
                        if (synced_lyrics && synced_lyrics[0]) break;
                        synced_lyrics = NULL;
                    }
                }
                if (!synced_lyrics) {
                    json_value_free(root);
                    root = NULL;
                }
            }
        }
    }

    free(response_buf);

    if (!synced_lyrics) {
        if (root) json_value_free(root);
        free(tmp_lines);
        free_fetch_args(args);
        return NULL;
    }

    // Save raw LRC text to cache
    save_lyrics_to_cache(cache_path, synced_lyrics);

    // Parse into temp buffer
    count = parse_lrc_text(synced_lyrics, tmp_lines, LYRICS_MAX_LINES);
    json_value_free(root);

    publish_lines(tmp_lines, count, true, my_gen);

    free(tmp_lines);
    free_fetch_args(args);
    return NULL;
}

void Lyrics_init(void) {
    lyrics_line_count = 0;
    lyrics_available = false;
    last_artist[0] = '\0';
    last_title[0] = '\0';
    last_path[0] = '\0';
    fetch_generation = 0;
}

void Lyrics_cleanup(void) {
    fetch_generation++;  // invalidate any running thread
    lyrics_line_count = 0;
    lyrics_available = false;
    last_artist[0] = '\0';
    last_title[0] = '\0';
    last_path[0] = '\0';
}

void Lyrics_clear(void) {
    fetch_generation++;  // invalidate any running thread
    lyrics_line_count = 0;
    lyrics_available = false;
    last_artist[0] = '\0';
    last_title[0] = '\0';
    last_path[0] = '\0';
}

void Lyrics_fetch(const char* artist, const char* title, int duration_sec,
                  const char* track_path, const char* embedded) {
    if (!artist) artist = "";
    if (!title) title = "";
    if (!track_path) track_path = "";

    // A track with no tags can still have lyrics in its file, or next to it
    bool has_tags = artist[0] || title[0];
    if (!has_tags && !track_path[0] && !embedded) {
        return;
    }

    // Dedup check
    if (strcmp(last_artist, artist) == 0 &&
        strcmp(last_title, title) == 0 &&
        strcmp(last_path, track_path) == 0) {
        return;
    }

    // Invalidate any previous fetch — old thread will discard its results
    fetch_generation++;

    // Reset state
    strncpy(last_artist, artist, sizeof(last_artist) - 1);
    last_artist[sizeof(last_artist) - 1] = '\0';
    strncpy(last_title, title, sizeof(last_title) - 1);
    last_title[sizeof(last_title) - 1] = '\0';
    snprintf(last_path, sizeof(last_path), "%s", track_path);
    lyrics_line_count = 0;
    lyrics_available = false;

    // Prepare thread args
    FetchArgs* args = (FetchArgs*)malloc(sizeof(FetchArgs));
    if (!args) return;
    strncpy(args->artist, artist, sizeof(args->artist) - 1);
    args->artist[sizeof(args->artist) - 1] = '\0';
    strncpy(args->title, title, sizeof(args->title) - 1);
    args->title[sizeof(args->title) - 1] = '\0';
    snprintf(args->track_path, sizeof(args->track_path), "%s", track_path);
    args->embedded = embedded ? strdup(embedded) : NULL;
    args->duration_sec = duration_sec;
    args->generation = fetch_generation;

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, fetch_thread_func, args) != 0) {
        free_fetch_args(args);
    }
    pthread_attr_destroy(&attr);
}

int Lyrics_currentIndex(int position_ms) {
    if (!lyrics_available) return -1;
    return LyricWindow_currentIndex(lyrics_lines, lyrics_line_count, position_ms);
}

int Lyrics_lineCount(void) {
    return lyrics_available ? lyrics_line_count : 0;
}

const char* Lyrics_lineText(int index) {
    if (!lyrics_available || index < 0 || index >= lyrics_line_count) return NULL;
    return lyrics_lines[index].text;
}

bool Lyrics_isAvailable(void) {
    return lyrics_available;
}

bool Lyrics_isSynced(void) {
    return lyrics_available && lyrics_synced;
}
