#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "audiobook.h"
#include "browser.h"
#include "m4b_chapters.h"
#include "player.h"
#include "include/parson/parson.h"

#define AUDIOBOOK_DIR_NAME "Audiobook"
#define AUDIOBOOK_DATA_DIR SHARED_USERDATA_PATH "/music-player"
#define AUDIOBOOK_PROGRESS_FILE AUDIOBOOK_DATA_DIR "/audiobooks.json"
#define AUDIOBOOK_LIBRARY_FILE  AUDIOBOOK_DATA_DIR "/audiobook_library.json"

// Guards against a pathological directory; a real book never comes close
#define AUDIOBOOK_MAX_CHAPTERS 4096

static char audiobook_root[AUDIOBOOK_MAX_PATH];
static bool initialized = false;

// ---------------------------------------------------------------------------
// Progress — one entry per book, keyed by its path relative to /Audiobook.
// Grown on demand: a fixed table would silently drop books past the cap.
// ---------------------------------------------------------------------------

typedef struct {
    char key[AUDIOBOOK_MAX_PATH];
    int  chapter;
    int  position_ms;
    bool finished;
    uint32_t last_played;
} ProgressEntry;

static ProgressEntry* progress_entries = NULL;
static int progress_count = 0;
static int progress_capacity = 0;
static bool progress_dirty = false;

// Scan cache: title/author/duration/chapter list per book, invalidated by mtime
static JSON_Value* library_cache = NULL;
static bool library_cache_dirty = false;

// The library
static Audiobook* books = NULL;
static int book_count = 0;
static int book_capacity = 0;

// Chapters of the most recently opened book
static AudiobookChapter* chapters = NULL;
static int chapter_count = 0;
static bool chapters_single_file = false;

const char* Audiobook_getRootPath(void) {
    return audiobook_root;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void strip_extension(char* name) {
    char* dot = strrchr(name, '.');
    if (dot && dot != name) *dot = '\0';
}

static int compare_names(const void* a, const void* b) {
    return strcasecmp((const char*)a, (const char*)b);
}

static int compare_books_by_title(const void* a, const void* b) {
    return strcasecmp(((const Audiobook*)a)->title, ((const Audiobook*)b)->title);
}

// In-progress books first, most recently played at the top; then the rest by title.
static int compare_books_for_display(const void* a, const void* b) {
    const Audiobook* ba = (const Audiobook*)a;
    const Audiobook* bb = (const Audiobook*)b;

    bool a_active = (ba->last_played > 0 && !ba->finished);
    bool b_active = (bb->last_played > 0 && !bb->finished);
    if (a_active != b_active) return a_active ? -1 : 1;
    if (a_active && ba->last_played != bb->last_played) {
        return (ba->last_played > bb->last_played) ? -1 : 1;
    }
    return strcasecmp(ba->title, bb->title);
}

static ProgressEntry* find_progress(const char* key) {
    for (int i = 0; i < progress_count; i++) {
        if (strcmp(progress_entries[i].key, key) == 0) return &progress_entries[i];
    }
    return NULL;
}

static ProgressEntry* add_progress(const char* key) {
    if (progress_count == progress_capacity) {
        int new_cap = progress_capacity ? progress_capacity * 2 : 16;
        ProgressEntry* grown = realloc(progress_entries, new_cap * sizeof(ProgressEntry));
        if (!grown) return NULL;
        progress_entries = grown;
        progress_capacity = new_cap;
    }
    ProgressEntry* entry = &progress_entries[progress_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->key, sizeof(entry->key), "%s", key);
    return entry;
}

static Audiobook* add_book(void) {
    if (book_count == book_capacity) {
        int new_cap = book_capacity ? book_capacity * 2 : 16;
        Audiobook* grown = realloc(books, new_cap * sizeof(Audiobook));
        if (!grown) return NULL;
        books = grown;
        book_capacity = new_cap;
    }
    Audiobook* book = &books[book_count++];
    memset(book, 0, sizeof(*book));
    return book;
}

static void free_chapters(void) {
    free(chapters);
    chapters = NULL;
    chapter_count = 0;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

static void load_progress(void) {
    JSON_Value* root = json_parse_file(AUDIOBOOK_PROGRESS_FILE);
    if (!root) return;

    JSON_Object* obj = json_value_get_object(root);
    if (obj) {
        size_t count = json_object_get_count(obj);
        for (size_t i = 0; i < count; i++) {
            const char* key = json_object_get_name(obj, i);
            JSON_Object* item = json_value_get_object(json_object_get_value_at(obj, i));
            if (!key || !item) continue;

            ProgressEntry* entry = add_progress(key);
            if (!entry) break;
            entry->chapter     = (int)json_object_get_number(item, "chapter");
            entry->position_ms = (int)json_object_get_number(item, "position_ms");
            entry->finished    = json_object_get_boolean(item, "finished") == 1;
            entry->last_played = (uint32_t)json_object_get_number(item, "last_played");
        }
    }
    json_value_free(root);
}

void Audiobook_flushProgress(void) {
    if (!progress_dirty) return;

    JSON_Value* root = json_value_init_object();
    JSON_Object* obj = json_value_get_object(root);
    if (!obj) {
        json_value_free(root);
        return;
    }

    for (int i = 0; i < progress_count; i++) {
        JSON_Value* item_value = json_value_init_object();
        JSON_Object* item = json_value_get_object(item_value);
        json_object_set_number(item, "chapter", progress_entries[i].chapter);
        json_object_set_number(item, "position_ms", progress_entries[i].position_ms);
        json_object_set_boolean(item, "finished", progress_entries[i].finished);
        json_object_set_number(item, "last_played", progress_entries[i].last_played);
        json_object_set_value(obj, progress_entries[i].key, item_value);
    }

    json_serialize_to_file_pretty(root, AUDIOBOOK_PROGRESS_FILE);
    json_value_free(root);
    progress_dirty = false;
}

static void load_library_cache(void) {
    library_cache = json_parse_file(AUDIOBOOK_LIBRARY_FILE);
    if (!library_cache || json_value_get_type(library_cache) != JSONObject) {
        if (library_cache) json_value_free(library_cache);
        library_cache = json_value_init_object();
    }
    library_cache_dirty = false;
}

static void save_library_cache(void) {
    if (!library_cache_dirty || !library_cache) return;
    json_serialize_to_file_pretty(library_cache, AUDIOBOOK_LIBRARY_FILE);
    library_cache_dirty = false;
}

// ---------------------------------------------------------------------------
// Chapter building (the slow path — results go into the scan cache)
// ---------------------------------------------------------------------------

// List the audio files of a multi-file book, sorted by name
static int list_chapter_files(const char* dir_path, char (**out)[AUDIOBOOK_MAX_TITLE]) {
    DIR* dir = opendir(dir_path);
    if (!dir) return 0;

    char (*names)[AUDIOBOOK_MAX_TITLE] = NULL;
    int count = 0, capacity = 0;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL && count < AUDIOBOOK_MAX_CHAPTERS) {
        if (ent->d_name[0] == '.') continue;
        if (!Browser_isAudioFile(ent->d_name)) continue;

        char full_path[AUDIOBOOK_MAX_PATH * 2];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name) >= (int)sizeof(full_path))
            continue;
        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (count == capacity) {
            int new_cap = capacity ? capacity * 2 : 32;
            void* grown = realloc(names, (size_t)new_cap * AUDIOBOOK_MAX_TITLE);
            if (!grown) break;
            names = grown;
            capacity = new_cap;
        }
        snprintf(names[count], AUDIOBOOK_MAX_TITLE, "%s", ent->d_name);
        count++;
    }
    closedir(dir);

    if (count > 1) qsort(names, count, AUDIOBOOK_MAX_TITLE, compare_names);

    *out = names;
    return count;
}

// Build the full chapter list of a book by reading the files on disk.
// Also fills in title/author/total duration when they can be read from tags.
static int build_chapters(Audiobook* book, AudiobookChapter** out) {
    *out = NULL;

    if (book->single_file) {
        int total_ms = Player_probeDuration(book->path);

        AudioFormat format = Player_detectFormat(book->path);
        if (format == AUDIO_FORMAT_M4B || format == AUDIO_FORMAT_M4A) {
            char tag_artist[AUDIOBOOK_MAX_AUTHOR] = "";
            char tag_album[AUDIOBOOK_MAX_TITLE] = "";
            if (M4B_readTags(book->path, NULL, 0,
                             tag_artist, sizeof(tag_artist),
                             tag_album, sizeof(tag_album))) {
                if (tag_album[0]) snprintf(book->title, sizeof(book->title), "%s", tag_album);
                if (tag_artist[0]) snprintf(book->author, sizeof(book->author), "%s", tag_artist);
            }

            int count = M4B_parseChapters(book->path, total_ms, out);
            if (count > 0) {
                book->total_duration_ms = total_ms;
                return count;
            }
        }

        // Degraded case: a single file with no internal chapters
        AudiobookChapter* single = calloc(1, sizeof(AudiobookChapter));
        if (!single) return 0;
        snprintf(single->title, sizeof(single->title), "%s", book->title);
        snprintf(single->path, sizeof(single->path), "%s", book->path);
        single->start_ms = 0;
        single->duration_ms = total_ms;
        book->total_duration_ms = total_ms;
        *out = single;
        return 1;
    }

    char (*names)[AUDIOBOOK_MAX_TITLE] = NULL;
    int count = list_chapter_files(book->path, &names);
    if (count == 0) {
        free(names);
        return 0;
    }

    AudiobookChapter* list = calloc(count, sizeof(AudiobookChapter));
    if (!list) {
        free(names);
        return 0;
    }

    int total_ms = 0;
    for (int i = 0; i < count; i++) {
        snprintf(list[i].path, sizeof(list[i].path), "%s/%s", book->path, names[i]);
        snprintf(list[i].title, sizeof(list[i].title), "%s", names[i]);
        strip_extension(list[i].title);
        list[i].start_ms = 0;  // Each file starts at its own zero
        list[i].duration_ms = Player_probeDuration(list[i].path);
        total_ms += list[i].duration_ms;
    }
    free(names);

    book->total_duration_ms = total_ms;
    *out = list;
    return count;
}

// ---------------------------------------------------------------------------
// Scan cache
// ---------------------------------------------------------------------------

static JSON_Object* cache_entry_for(const char* key) {
    JSON_Object* root = json_value_get_object(library_cache);
    if (!root) return NULL;
    return json_value_get_object(json_object_get_value(root, key));
}

static void cache_store(const Audiobook* book, uint32_t mtime,
                        const AudiobookChapter* list, int count) {
    JSON_Object* root = json_value_get_object(library_cache);
    if (!root) return;

    JSON_Value* entry_value = json_value_init_object();
    JSON_Object* entry = json_value_get_object(entry_value);
    json_object_set_number(entry, "mtime", mtime);
    json_object_set_string(entry, "title", book->title);
    json_object_set_string(entry, "author", book->author);
    json_object_set_boolean(entry, "single_file", book->single_file);
    json_object_set_number(entry, "duration_ms", book->total_duration_ms);

    JSON_Value* array_value = json_value_init_array();
    JSON_Array* array = json_value_get_array(array_value);
    size_t prefix_len = strlen(book->path) + 1;  // "<book path>/"
    for (int i = 0; i < count; i++) {
        JSON_Value* item_value = json_value_init_object();
        JSON_Object* item = json_value_get_object(item_value);
        json_object_set_string(item, "title", list[i].title);
        // Store just the file name; the book may be moved with the SD card
        const char* file = book->single_file ? "" : list[i].path + prefix_len;
        json_object_set_string(item, "file", file);
        json_object_set_number(item, "start_ms", list[i].start_ms);
        json_object_set_number(item, "duration_ms", list[i].duration_ms);
        json_array_append_value(array, item_value);
    }
    json_object_set_value(entry, "chapters", array_value);

    json_object_set_value(root, book->key, entry_value);
    library_cache_dirty = true;
}

// Materialise a cached chapter array into AudiobookChapter records
static int cache_read_chapters(const Audiobook* book, JSON_Object* entry,
                               AudiobookChapter** out) {
    JSON_Array* array = json_object_get_array(entry, "chapters");
    if (!array) return 0;

    int count = (int)json_array_get_count(array);
    if (count <= 0 || count > AUDIOBOOK_MAX_CHAPTERS) return 0;

    AudiobookChapter* list = calloc(count, sizeof(AudiobookChapter));
    if (!list) return 0;

    for (int i = 0; i < count; i++) {
        JSON_Object* item = json_array_get_object(array, i);
        if (!item) continue;
        const char* title = json_object_get_string(item, "title");
        const char* file = json_object_get_string(item, "file");
        snprintf(list[i].title, sizeof(list[i].title), "%s", title ? title : "");
        if (book->single_file || !file || !file[0]) {
            snprintf(list[i].path, sizeof(list[i].path), "%s", book->path);
        } else {
            snprintf(list[i].path, sizeof(list[i].path), "%s/%s", book->path, file);
        }
        list[i].start_ms = (int)json_object_get_number(item, "start_ms");
        list[i].duration_ms = (int)json_object_get_number(item, "duration_ms");
    }

    *out = list;
    return count;
}

// Fill in a book's metadata, rebuilding it only when the cache is stale
static bool resolve_metadata(Audiobook* book, uint32_t mtime) {
    JSON_Object* entry = cache_entry_for(book->key);
    if (entry && (uint32_t)json_object_get_number(entry, "mtime") == mtime) {
        const char* title = json_object_get_string(entry, "title");
        const char* author = json_object_get_string(entry, "author");
        if (title && title[0]) snprintf(book->title, sizeof(book->title), "%s", title);
        if (author) snprintf(book->author, sizeof(book->author), "%s", author);
        book->total_duration_ms = (int)json_object_get_number(entry, "duration_ms");
        JSON_Array* array = json_object_get_array(entry, "chapters");
        book->chapter_count = array ? (int)json_array_get_count(array) : 0;
        return book->chapter_count > 0;
    }

    AudiobookChapter* list = NULL;
    int count = build_chapters(book, &list);
    if (count == 0) {
        free(list);
        return false;
    }
    book->chapter_count = count;
    cache_store(book, mtime, list, count);
    free(list);
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int Audiobook_init(void) {
    if (initialized) return 0;

    snprintf(audiobook_root, sizeof(audiobook_root), "%s/" AUDIOBOOK_DIR_NAME, SDCARD_PATH);
    mkdir(AUDIOBOOK_DATA_DIR, 0755);
    mkdir(audiobook_root, 0755);

    load_progress();
    load_library_cache();

    initialized = true;
    return 0;
}

void Audiobook_cleanup(void) {
    Audiobook_flushProgress();
    save_library_cache();

    free_chapters();
    free(books);
    books = NULL;
    book_count = 0;
    book_capacity = 0;

    if (library_cache) {
        json_value_free(library_cache);
        library_cache = NULL;
    }
    // Progress entries stay resident: they are small and reloading costs a file read.
}

void Audiobook_scanLibrary(void) {
    if (!initialized) Audiobook_init();

    book_count = 0;
    free_chapters();

    DIR* dir = opendir(audiobook_root);
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[AUDIOBOOK_MAX_PATH];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", audiobook_root, ent->d_name)
                >= (int)sizeof(full_path)) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        bool is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !Browser_isAudioFile(ent->d_name)) continue;

        Audiobook* book = add_book();
        if (!book) break;

        book->single_file = !is_dir;
        snprintf(book->path, sizeof(book->path), "%s", full_path);
        snprintf(book->key, sizeof(book->key), "%s", ent->d_name);
        snprintf(book->title, sizeof(book->title), "%s", ent->d_name);
        if (!is_dir) strip_extension(book->title);

        if (!resolve_metadata(book, (uint32_t)st.st_mtime)) {
            book_count--;  // No playable chapters — not a book
            continue;
        }

        const ProgressEntry* entry = find_progress(book->key);
        if (entry) {
            book->current_chapter = entry->chapter;
            book->position_ms = entry->position_ms;
            book->finished = entry->finished;
            book->last_played = entry->last_played;
            if (book->current_chapter >= book->chapter_count) {
                book->current_chapter = book->chapter_count - 1;
            }
            if (book->current_chapter < 0) book->current_chapter = 0;
        }
    }
    closedir(dir);

    if (book_count > 1) {
        // Sort by title first so equal-ranked books stay in a stable order
        qsort(books, book_count, sizeof(Audiobook), compare_books_by_title);
        qsort(books, book_count, sizeof(Audiobook), compare_books_for_display);
    }

    save_library_cache();
}

int Audiobook_getCount(void) {
    return book_count;
}

Audiobook* Audiobook_get(int index) {
    if (index < 0 || index >= book_count) return NULL;
    return &books[index];
}

int Audiobook_getResumeIndex(void) {
    int best = -1;
    uint32_t best_time = 0;
    for (int i = 0; i < book_count; i++) {
        if (books[i].finished || books[i].last_played == 0) continue;
        if (best < 0 || books[i].last_played > best_time) {
            best = i;
            best_time = books[i].last_played;
        }
    }
    return best;
}

int Audiobook_loadChapters(const Audiobook* book) {
    free_chapters();
    if (!book) return 0;

    chapters_single_file = book->single_file;

    JSON_Object* entry = cache_entry_for(book->key);
    if (entry) {
        chapter_count = cache_read_chapters(book, entry, &chapters);
        if (chapter_count > 0) return chapter_count;
    }

    // Cache miss (first run, or the entry was pruned) — rebuild from disk
    Audiobook scratch = *book;
    chapter_count = build_chapters(&scratch, &chapters);
    if (chapter_count > 0) {
        struct stat st;
        uint32_t mtime = (stat(book->path, &st) == 0) ? (uint32_t)st.st_mtime : 0;
        cache_store(&scratch, mtime, chapters, chapter_count);
        save_library_cache();
    }
    return chapter_count;
}

int Audiobook_getChapterCount(void) {
    return chapter_count;
}

AudiobookChapter* Audiobook_getChapter(int index) {
    if (index < 0 || index >= chapter_count) return NULL;
    return &chapters[index];
}

int Audiobook_getChapterBookOffsetMs(int chapter_index) {
    if (chapter_index < 0 || chapter_index >= chapter_count) return 0;

    // Single-file books already carry offsets from the start of the book
    if (chapters_single_file) return chapters[chapter_index].start_ms;

    int offset = 0;
    for (int i = 0; i < chapter_index; i++) offset += chapters[i].duration_ms;
    return offset;
}

void Audiobook_saveProgress(Audiobook* book, int chapter, int position_ms) {
    if (!book) return;
    if (chapter < 0) chapter = 0;
    if (position_ms < 0) position_ms = 0;

    book->current_chapter = chapter;
    book->position_ms = position_ms;
    book->last_played = (uint32_t)time(NULL);

    ProgressEntry* entry = find_progress(book->key);
    if (!entry) {
        entry = add_progress(book->key);
        if (!entry) return;
    }
    entry->chapter = chapter;
    entry->position_ms = position_ms;
    entry->finished = book->finished;
    entry->last_played = book->last_played;
    progress_dirty = true;
}

void Audiobook_markFinished(Audiobook* book, bool finished) {
    if (!book) return;

    // Deliberately keeps chapter/position so the end can be replayed — the
    // podcast module's -1 sentinel throws that away.
    book->finished = finished;

    ProgressEntry* entry = find_progress(book->key);
    if (!entry) {
        entry = add_progress(book->key);
        if (!entry) return;
        entry->chapter = book->current_chapter;
        entry->position_ms = book->position_ms;
        entry->last_played = book->last_played;
    }
    entry->finished = finished;
    progress_dirty = true;
}
