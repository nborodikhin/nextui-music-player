#include "filedb.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "browser.h"
#include "db.h"
#include "file_utils.h"
#include "player.h"

typedef enum {
    FILEDB_REQUEST_ALL,
    FILEDB_REQUEST_DIR,
    FILEDB_REQUEST_FILE,
    FILEDB_REQUEST_PLAYLIST,
    FILEDB_REQUEST_PLAYLISTS,
} FileDbRequestType;

typedef struct {
    int                 id;
    FileDbRequestType  type;
    int                 item_id;
    bool                recursive;
} FileDbRequest;

typedef struct {
    FileDbRequest* items;
    size_t          count;
    size_t          head;
    size_t          capacity;
} RequestQueue;

typedef struct {
    int*   items;
    size_t count;
    size_t head;
    size_t capacity;
} IdQueue;

typedef struct {
    int*   items;
    size_t count;
    size_t capacity;
} IdSet;

typedef struct {
    char name[256];
    bool is_dir;
} CardEntry;

typedef struct {
    char path[512];
    int  num_files;
} CardPlaylist;

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t  condition;
    pthread_t       thread;
    RequestQueue    requests;
    int             next_id;
    volatile int    last_done;
    volatile int    idle;
    bool            running;
    bool            stop;
} file_db = {
    .mutex     = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
    .next_id   = 1,
};

static int atomic_load_int(volatile int* value) {
    return __atomic_load_n(value, __ATOMIC_SEQ_CST);
}

static void atomic_store_int(volatile int* value, int item) {
    __atomic_store_n(value, item, __ATOMIC_SEQ_CST);
}

static bool filedb_is_stopping(void) {
    pthread_mutex_lock(&file_db.mutex);
    bool stop = file_db.stop;
    pthread_mutex_unlock(&file_db.mutex);
    return stop;
}

static bool id_queue_push(IdQueue* queue, int id) {
    if (queue->count == queue->capacity) {
        size_t capacity = queue->capacity ? queue->capacity * 2 : 16;
        int* items = realloc(queue->items, capacity * sizeof(*items));
        if (!items) return false;
        queue->items = items;
        queue->capacity = capacity;
    }
    queue->items[queue->count++] = id;
    return true;
}

static bool id_queue_pop(IdQueue* queue, int* id) {
    if (queue->head == queue->count) return false;
    *id = queue->items[queue->head++];
    return true;
}

static void id_queue_free(IdQueue* queue) {
    free(queue->items);
    memset(queue, 0, sizeof(*queue));
}

static bool id_set_contains(const IdSet* set, int id) {
    for (size_t i = 0; i < set->count; i++) {
        if (set->items[i] == id) return true;
    }
    return false;
}

static bool id_set_add(IdSet* set, int id) {
    if (id_set_contains(set, id)) return true;
    if (set->count == set->capacity) {
        size_t capacity = set->capacity ? set->capacity * 2 : 16;
        int* items = realloc(set->items, capacity * sizeof(*items));
        if (!items) return false;
        set->items = items;
        set->capacity = capacity;
    }
    set->items[set->count++] = id;
    return true;
}

static void id_set_free(IdSet* set) {
    free(set->items);
    memset(set, 0, sizeof(*set));
}

static bool request_queue_push(RequestQueue* queue, const FileDbRequest* request) {
    if (queue->count == queue->capacity) {
        size_t capacity = queue->capacity ? queue->capacity * 2 : 16;
        FileDbRequest* items = realloc(queue->items, capacity * sizeof(*items));
        if (!items) return false;
        queue->items = items;
        queue->capacity = capacity;
    }
    queue->items[queue->count++] = *request;
    return true;
}

static bool request_queue_pop(RequestQueue* queue, FileDbRequest* request) {
    if (queue->head == queue->count) return false;
    *request = queue->items[queue->head++];
    if (queue->head == queue->count) {
        queue->head = 0;
        queue->count = 0;
    }
    return true;
}

static void request_queue_free(RequestQueue* queue) {
    free(queue->items);
    memset(queue, 0, sizeof(*queue));
}

static int enqueue_request(FileDbRequestType type, int item_id,
                           bool recursive) {
    pthread_mutex_lock(&file_db.mutex);
    if (file_db.stop) {
        pthread_mutex_unlock(&file_db.mutex);
        return 0;
    }

    FileDbRequest request = {
        .id        = file_db.next_id++,
        .type      = type,
        .item_id   = item_id,
        .recursive = recursive,
    };
    if (!request_queue_push(&file_db.requests, &request)) {
        pthread_mutex_unlock(&file_db.mutex);
        return 0;
    }
    atomic_store_int(&file_db.idle, 0);
    pthread_cond_signal(&file_db.condition);
    pthread_mutex_unlock(&file_db.mutex);
    return request.id;
}

// Returns the type of a regular file that a file scan stores.
static DbFileType file_type_of(const char* name) {
    return Player_detectFormat(name) != AUDIO_FORMAT_UNKNOWN
        ? DB_FILE_TYPE_MUSIC
        : DB_FILE_TYPE_OTHER;
}

static int card_entry_compare(const void* left, const void* right) {
    const CardEntry* a = left;
    const CardEntry* b = right;
    int result = strcasecmp(a->name, b->name);
    if (result != 0) return result;
    result = strcmp(a->name, b->name);
    if (result != 0) return result;
    return (int)b->is_dir - (int)a->is_dir;
}

static int playlist_compare(const void* left, const void* right) {
    const CardPlaylist* a = left;
    const CardPlaylist* b = right;
    return strcasecmp(a->path, b->path);
}

static int open_card_directory(const char* relative_path) {
    char root[1024];
    if (!get_music_abspath("", root, sizeof(root))) {
        errno = EINVAL;
        return -1;
    }

    int directory = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0) return -1;

    const char* component = relative_path ? relative_path : "";
    while (*component) {
        const char* slash = strchr(component, '/');
        size_t length = slash ? (size_t)(slash - component) : strlen(component);
        if (length == 0 || length >= 256 ||
            (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            close(directory);
            errno = EINVAL;
            return -1;
        }

        char name[256];
        memcpy(name, component, length);
        name[length] = '\0';
        int child = openat(directory, name,
                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        int open_error = errno;
        close(directory);
        if (child < 0) {
            errno = open_error;
            return -1;
        }
        directory = child;
        component = slash ? slash + 1 : component + length;
    }
    return directory;
}

static bool card_has_dir(const CardEntry* entries, int count, const char* name) {
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir && strcmp(entries[i].name, name) == 0) return true;
    }
    return false;
}

static bool card_has_file(const CardEntry* entries, int count, const char* name) {
    for (int i = 0; i < count; i++) {
        if (!entries[i].is_dir && strcmp(entries[i].name, name) == 0) return true;
    }
    return false;
}

static bool rows_have_dir(const DbReadDirResult* rows, const char* name) {
    for (int i = 0; i < rows->dir_count; i++) {
        if (strcmp(rows->dirs[i].filename, name) == 0) return true;
    }
    return false;
}

static bool rows_have_file(const DbReadDirResult* rows, const char* name) {
    for (int i = 0; i < rows->file_count; i++) {
        if (strcmp(rows->files[i].filename, name) == 0) return true;
    }
    return false;
}

// Pass a flag in gone to learn whether the directory no longer exists as a
// real directory.
static bool read_card_directory(const char* relative_path, CardEntry** output,
                                int* count, bool* gone) {
    *gone = false;
    int descriptor = open_card_directory(relative_path);
    if (descriptor < 0) {
        *gone = errno == ENOENT || errno == ENOTDIR || errno == ELOOP;
        return false;
    }
    DIR* directory = fdopendir(descriptor);
    if (!directory) {
        close(descriptor);
        return false;
    }

    CardEntry* entries = NULL;
    int entry_count = 0;
    int capacity = 0;
    bool stopped = false;
    struct dirent* item;
    while ((item = readdir(directory)) != NULL) {
        if (filedb_is_stopping()) {
            stopped = true;
            break;
        }
        if (item->d_name[0] == '.') continue;
        if (strlen(item->d_name) >= sizeof(entries[0].name)) continue;

        struct stat status;
        if (fstatat(dirfd(directory), item->d_name, &status,
                    AT_SYMLINK_NOFOLLOW) != 0 || S_ISLNK(status.st_mode)) {
            continue;
        }

        CardEntry entry = {
            .is_dir = S_ISDIR(status.st_mode),
        };
        strncpy(entry.name, item->d_name, sizeof(entry.name) - 1);
        if (!entry.is_dir && !S_ISREG(status.st_mode)) continue;

        if (entry_count == capacity) {
            int next_capacity = capacity ? capacity * 2 : 32;
            CardEntry* next = realloc(entries, (size_t)next_capacity * sizeof(*next));
            if (!next) {
                free(entries);
                closedir(directory);
                return false;
            }
            entries = next;
            capacity = next_capacity;
        }
        entries[entry_count++] = entry;
    }
    closedir(directory);

    if (stopped) {
        free(entries);
        return false;
    }

    qsort(entries, (size_t)entry_count, sizeof(*entries), card_entry_compare);
    *output = entries;
    *count = entry_count;
    return true;
}

// Runs the file scan for each file of a directory listing that has the type
// unknown, in one transaction.
static bool check_unknown_files(const DbReadDirResult* rows) {
    bool found = false;
    for (int i = 0; i < rows->file_count && !found; i++) {
        found = rows->files[i].type == DB_FILE_TYPE_UNKNOWN;
    }
    if (!found) return true;

    bool success = Db_begin();
    for (int i = 0; success && i < rows->file_count; i++) {
        const DbFile* file = &rows->files[i];
        if (file->type != DB_FILE_TYPE_UNKNOWN) continue;
        success = Db_updateFileType(file->id, file_type_of(file->filename));
    }
    if (success) success = Db_commit();
    if (!success) Db_rollback();
    return success;
}

static bool scan_directory(int dir_id, IdQueue* children) {
    DbDirResult* directory_result = Db_getDir(dir_id);
    if (!directory_result) {
        Db_freeResult(directory_result);
        return false;
    }

    DbDir* directory_row = &directory_result->dir;
    CardEntry* card_entries = NULL;
    int card_count = 0;
    bool gone = false;
    bool listed = read_card_directory(directory_row->path, &card_entries,
                                      &card_count, &gone);
    if (!listed) {
        bool is_root = directory_row->parent_id < 0;
        free(card_entries);
        Db_freeResult(directory_result);
        return gone && !is_root && Db_deleteDir(dir_id);
    }

    DbReadDirResult* rows = Db_readDir(dir_id);
    if (!rows) {
        free(card_entries);
        Db_freeResult(directory_result);
        return false;
    }

    int file_count = 0;
    for (int i = 0; i < card_count; i++) {
        if (!card_entries[i].is_dir) file_count++;
    }

    bool changed = rows->file_count != file_count;
    for (int i = 0; i < rows->dir_count && !changed; i++) {
        changed = !card_has_dir(card_entries, card_count, rows->dirs[i].filename);
    }
    for (int i = 0; i < rows->file_count && !changed; i++) {
        changed = !card_has_file(card_entries, card_count, rows->files[i].filename);
    }
    for (int i = 0; i < card_count && !changed; i++) {
        changed = card_entries[i].is_dir
            ? !rows_have_dir(rows, card_entries[i].name)
            : !rows_have_file(rows, card_entries[i].name);
    }

    bool success = true;
    if (changed) {
        success = Db_begin();
        for (int i = 0; success && i < rows->dir_count; i++) {
            if (!card_has_dir(card_entries, card_count, rows->dirs[i].filename)) {
                success = Db_deleteDir(rows->dirs[i].id);
            }
        }
        for (int i = 0; success && i < rows->file_count; i++) {
            if (!card_has_file(card_entries, card_count, rows->files[i].filename)) {
                success = Db_deleteFile(rows->files[i].id);
            }
        }
        for (int i = 0; success && i < card_count; i++) {
            if (card_entries[i].is_dir && !rows_have_dir(rows, card_entries[i].name)) {
                char relative[1024];
                int length = directory_row->path && directory_row->path[0]
                    ? snprintf(relative, sizeof(relative), "%s/%s",
                               directory_row->path, card_entries[i].name)
                    : snprintf(relative, sizeof(relative), "%s", card_entries[i].name);
                if (length < 0 || (size_t)length >= sizeof(relative) ||
                    !Db_addDir(dir_id, relative, card_entries[i].name)) {
                    success = false;
                }
            } else if (!card_entries[i].is_dir &&
                       !rows_have_file(rows, card_entries[i].name)) {
                success = Db_addFile(dir_id, card_entries[i].name) != 0;
            }
        }
        if (success) success = Db_commit();
        if (!success) Db_rollback();
    }

    if (success) {
        DbReadDirResult* updated = Db_readDir(dir_id);
        if (!updated) {
            success = false;
        } else {
            for (int i = 0; i < updated->dir_count; i++) {
                if (!id_queue_push(children, updated->dirs[i].id)) {
                    success = false;
                    break;
                }
            }
            if (success) success = check_unknown_files(updated);
            Db_freeResult(updated);
        }
    }

    Db_freeResult(rows);
    free(card_entries);
    Db_freeResult(directory_result);
    return success;
}

static bool scan_directory_once(int dir_id) {
    IdQueue children = {0};
    bool success = scan_directory(dir_id, &children);
    id_queue_free(&children);
    return success;
}

static bool scan_directory_tree(int dir_id) {
    IdQueue directories = {0};
    IdSet listed = {0};
    bool success = id_queue_push(&directories, dir_id);
    while (success && !filedb_is_stopping()) {
        int current = 0;
        if (!id_queue_pop(&directories, &current)) break;
        if (!id_set_add(&listed, current)) {
            success = false;
            break;
        }

        IdQueue children = {0};
        success = scan_directory(current, &children);
        for (size_t i = children.head; success && i < children.count; i++) {
            if (!id_set_contains(&listed, children.items[i])) {
                success = id_queue_push(&directories, children.items[i]);
            }
        }
        id_queue_free(&children);
    }
    id_set_free(&listed);
    id_queue_free(&directories);
    return success;
}

// Returns true when the file of an index row is a regular file on the card.
static bool file_on_card(const DbFile* file) {
    struct stat status;
    int directory = open_card_directory(file->dir_path);
    bool valid_name = file->filename && file->filename[0] &&
                      !strchr(file->filename, '/') &&
                      strcmp(file->filename, ".") != 0 &&
                      strcmp(file->filename, "..") != 0;
    bool present = directory >= 0 && valid_name &&
                   fstatat(directory, file->filename, &status,
                           AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(status.st_mode);
    if (directory >= 0) close(directory);
    return present;
}

bool FileDb_fileExists(int file_id) {
    DbFileResult* result = Db_getFile(file_id);
    bool present = result && file_on_card(&result->file);
    Db_freeResult(result);
    return present;
}

static bool scan_file(int file_id) {
    DbFileResult* result = Db_getFile(file_id);
    if (!result) {
        Db_freeResult(result);
        return true;
    }

    DbFile* file = &result->file;
    bool success;
    if (!file_on_card(file)) {
        success = Db_deleteFile(file->id);
    } else {
        DbFileType type = file_type_of(file->filename);
        success = type == file->type || Db_updateFileType(file->id, type);
    }
    Db_freeResult(result);
    return success;
}

static int count_playlist_tracks(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return -1;
    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length > 0 && line[0] != '#') count++;
    }
    bool error = ferror(file) != 0;
    fclose(file);
    return error ? -1 : count;
}

static bool playlist_path(const char* relative, char* path, size_t path_size) {
    char relative_path[1024];
    int length = snprintf(relative_path, sizeof(relative_path), "playlists/%s", relative);
    if (length < 0 || (size_t)length >= sizeof(relative_path)) return false;
    length = userdata_snpath(relative_path, path, path_size);
    return length >= 0 && (size_t)length < path_size;
}

static bool read_card_playlists(CardPlaylist** output, int* count) {
    char directory_path[1024];
    if (userdata_snpath("playlists", directory_path, sizeof(directory_path)) < 0) {
        return false;
    }
    if (!userdata_mkdir("playlists")) return false;

    DIR* directory = opendir(directory_path);
    if (!directory) return false;

    CardPlaylist* entries = NULL;
    int entry_count = 0;
    int capacity = 0;
    struct dirent* item;
    while ((item = readdir(directory)) != NULL) {
        if (item->d_name[0] == '.') continue;
        const char* extension = strrchr(item->d_name, '.');
        if (!extension || strcasecmp(extension, ".m3u") != 0) continue;
        if (strlen(item->d_name) >= sizeof(entries[0].path)) continue;

        char path[1024];
        if (!playlist_path(item->d_name, path, sizeof(path))) continue;
        struct stat status;
        if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode)) continue;

        int num_files = count_playlist_tracks(path);
        if (num_files < 0) continue;
        if (entry_count == capacity) {
            int next_capacity = capacity ? capacity * 2 : 16;
            CardPlaylist* next = realloc(entries,
                                         (size_t)next_capacity * sizeof(*next));
            if (!next) {
                free(entries);
                closedir(directory);
                return false;
            }
            entries = next;
            capacity = next_capacity;
        }
        CardPlaylist entry = {
            .num_files = num_files,
        };
        strncpy(entry.path, item->d_name, sizeof(entry.path) - 1);
        entries[entry_count++] = entry;
    }
    closedir(directory);
    qsort(entries, (size_t)entry_count, sizeof(*entries), playlist_compare);
    *output = entries;
    *count = entry_count;
    return true;
}

static bool card_has_playlist(const CardPlaylist* entries, int count, const char* path) {
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].path, path) == 0) return true;
    }
    return false;
}

static bool scan_playlists(void) {
    CardPlaylist* card = NULL;
    int card_count = 0;
    if (!read_card_playlists(&card, &card_count)) {
        return false;
    }
    DbPlaylistsResult* rows = Db_readPlaylists();
    if (!rows) {
        free(card);
        return false;
    }

    bool changed = false;
    for (int i = 0; i < rows->count && !changed; i++) {
        changed = !card_has_playlist(card, card_count, rows->items[i].path);
    }
    for (int i = 0; i < card_count && !changed; i++) {
        bool found = false;
        for (int j = 0; j < rows->count; j++) {
            if (strcmp(card[i].path, rows->items[j].path) == 0) {
                found = true;
                if (card[i].num_files != rows->items[j].num_entries) changed = true;
                break;
            }
        }
        if (!found) changed = true;
    }

    bool success = true;
    if (changed) {
        success = Db_begin();
        for (int i = 0; success && i < rows->count; i++) {
            if (!card_has_playlist(card, card_count, rows->items[i].path)) {
                success = Db_deletePlaylist(rows->items[i].id);
            }
        }
        for (int i = 0; success && i < card_count; i++) {
            DbPlaylist* existing = NULL;
            for (int j = 0; j < rows->count; j++) {
                if (strcmp(card[i].path, rows->items[j].path) == 0) {
                    existing = &rows->items[j];
                    break;
                }
            }
            int id = existing ? existing->id : 0;
            if (!existing) {
                id = Db_getOrCreatePlaylist(card[i].path);
                success = id != 0;
            }
            if (success && (!existing || existing->num_entries != card[i].num_files)) {
                success = Db_updatePlaylist(id, card[i].num_files);
            }
        }
        if (success) success = Db_commit();
        if (!success) Db_rollback();
    }

    Db_freeResult(rows);
    free(card);
    return success;
}

// Returns the id of the directory with a relative path, or -1 when it has no row.
static int find_directory(const char* path) {
    DbDirResult* result = Db_getDirByPath(path);
    int id = result ? result->dir.id : -1;
    Db_freeResult(result);
    return id;
}

// Lists a directory once in one playlist scan. Pass the ids that the scan listed in listed.
static bool list_directory_once(int dir_id, IdSet* listed) {
    if (id_set_contains(listed, dir_id)) return true;
    return scan_directory_once(dir_id) && id_set_add(listed, dir_id);
}

// Lists the directory with a relative path, thus its files have rows. A directory that has no
// row yet gets one from the listing of its parent, thus the listings start at the nearest
// directory above it that has a row. Returns false when the directory is not on the card.
static bool list_entry_directory(const char* dir_path, IdSet* listed) {
    int dir_id = find_directory(dir_path);
    if (dir_id < 0) {
        dir_id = Db_getRootDirId();
        const char* end = dir_path;
        while (dir_id >= 0 && *end) {
            const char* slash = strchr(end, '/');
            end = slash ? slash : end + strlen(end);
            char prefix[1024];
            int length = snprintf(prefix, sizeof(prefix), "%.*s", (int)(end - dir_path), dir_path);
            if (length < 0 || (size_t)length >= sizeof(prefix)) return false;
            int child_id = find_directory(prefix);
            if (child_id < 0) {
                if (!list_directory_once(dir_id, listed)) return false;
                child_id = find_directory(prefix);
            }
            dir_id = child_id;
            if (*end) end++;
        }
        if (dir_id < 0) return false;
    }
    return list_directory_once(dir_id, listed);
}

// Lists the directory of each entry of an .m3u file, thus a lookup of an entry finds the
// current row of its file. An entry outside the Music root, or in a directory that is gone,
// is skipped.
static void list_playlist_entries(const char* m3u_path) {
    FILE* file = fopen(m3u_path, "rb");
    if (!file) return;

    IdSet listed = {0};
    char line[1024];
    while (!filedb_is_stopping() && fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0 || line[0] == '#') continue;

        char relative[1024];
        if (!get_music_relpath(line, relative, sizeof(relative))) continue;
        char* slash = strrchr(relative, '/');
        if (slash) {
            *slash = '\0';
        } else {
            relative[0] = '\0';
        }
        list_entry_directory(relative, &listed);
    }
    id_set_free(&listed);
    fclose(file);
}

static bool scan_playlist(int playlist_id) {
    DbPlaylistResult* result = Db_getPlaylist(playlist_id);
    if (!result) {
        Db_freeResult(result);
        return true;
    }

    char path[1024];
    bool present = playlist_path(result->playlist.path, path, sizeof(path));
    struct stat status;
    present = present && lstat(path, &status) == 0 && S_ISREG(status.st_mode);
    bool success = present ? true : Db_deletePlaylist(result->playlist.id);
    if (success && present) {
        int count = count_playlist_tracks(path);
        success = count >= 0 && count == result->playlist.num_entries
            ? true
            : (count >= 0 && Db_updatePlaylist(result->playlist.id, count));
        list_playlist_entries(path);
    }
    Db_freeResult(result);
    return success;
}

// Returns the directory of the last played file, or -1 when there is none.
static int last_played_directory(void) {
    int directory_id = -1;
    DbLastPlayedResult* result = Db_readLastPlayed();
    if (result) {
        DbFileResult* file = Db_getFile(result->last_played.id2);
        if (file) {
            directory_id = file->file.parent_id;
        } else if (result->last_played.type == DB_LAST_PLAYED_FOLDER) {
            directory_id = result->last_played.id1;
        }
        Db_freeResult(file);
    }
    Db_freeResult(result);
    return directory_id;
}

static bool execute_request(const FileDbRequest* request) {
    if (filedb_is_stopping()) return false;
    switch (request->type) {
        case FILEDB_REQUEST_ALL:
            return scan_directory_tree(Db_getRootDirId());
        case FILEDB_REQUEST_DIR:
            return request->recursive
                ? scan_directory_tree(request->item_id)
                : scan_directory_once(request->item_id);
        case FILEDB_REQUEST_FILE:
            return scan_file(request->item_id);
        case FILEDB_REQUEST_PLAYLIST:
            return scan_playlist(request->item_id);
        case FILEDB_REQUEST_PLAYLISTS:
            return scan_playlists();
    }
    return false;
}

static void* filedb_thread_main(void* unused) {
    (void)unused;
    IdQueue directories = {0};
    IdSet listed = {0};

    char music_path[1024];
    if (get_music_abspath("", music_path, sizeof(music_path))) {
        mkdir_p(music_path);
    }
    scan_playlists();
    // The walk lists this directory first and lists it again in its place in
    // the breadth-first order, thus the order of the other directories stays.
    int first_directory = last_played_directory();
    int root_id = Db_getRootDirId();
    if (first_directory == root_id) first_directory = -1;
    if (root_id >= 0) id_queue_push(&directories, root_id);

    for (;;) {
        FileDbRequest request;
        int directory_id = 0;
        bool do_first = false;
        bool do_request = false;
        bool do_directory = false;

        pthread_mutex_lock(&file_db.mutex);
        while (!file_db.stop && file_db.requests.head == file_db.requests.count &&
               first_directory < 0 && directories.head == directories.count) {
            atomic_store_int(&file_db.idle, 1);
            pthread_cond_wait(&file_db.condition, &file_db.mutex);
        }
        if (file_db.stop) {
            pthread_mutex_unlock(&file_db.mutex);
            break;
        }
        atomic_store_int(&file_db.idle, 0);
        do_request = request_queue_pop(&file_db.requests, &request);
        if (!do_request && first_directory >= 0) {
            do_first = true;
        } else if (!do_request && id_queue_pop(&directories, &directory_id)) {
            do_directory = true;
        }
        pthread_mutex_unlock(&file_db.mutex);

        if (do_request) {
            execute_request(&request);
            atomic_store_int(&file_db.last_done, request.id);
            continue;
        }

        if (do_first) {
            int directory = first_directory;
            first_directory = -1;
            if (!filedb_is_stopping()) scan_directory_once(directory);
            continue;
        }

        if (do_directory) {
            if (filedb_is_stopping()) continue;
            if (id_set_contains(&listed, directory_id)) continue;
            if (!id_set_add(&listed, directory_id)) continue;
            IdQueue children = {0};
            bool success = scan_directory(directory_id, &children);
            if (success) {
                for (size_t i = children.head; i < children.count; i++) {
                    if (!id_set_contains(&listed, children.items[i])) {
                        if (!id_queue_push(&directories, children.items[i])) {
                            success = false;
                            break;
                        }
                    }
                }
            }
            id_queue_free(&children);
            continue;
        }
    }

    id_set_free(&listed);
    id_queue_free(&directories);
    pthread_mutex_lock(&file_db.mutex);
    file_db.running = false;
    atomic_store_int(&file_db.idle, 1);
    request_queue_free(&file_db.requests);
    pthread_mutex_unlock(&file_db.mutex);
    return NULL;
}

void FileDb_start(void) {
    pthread_mutex_lock(&file_db.mutex);
    if (file_db.running) {
        pthread_mutex_unlock(&file_db.mutex);
        return;
    }
    file_db.stop = false;
    file_db.running = true;
    atomic_store_int(&file_db.last_done, 0);
    atomic_store_int(&file_db.idle, 0);
    bool created = pthread_create(&file_db.thread, NULL, filedb_thread_main, NULL) == 0;
    if (!created) {
        file_db.running = false;
        atomic_store_int(&file_db.idle, 1);
    }
    pthread_mutex_unlock(&file_db.mutex);
}

void FileDb_quit(void) {
    pthread_mutex_lock(&file_db.mutex);
    if (!file_db.running) {
        pthread_mutex_unlock(&file_db.mutex);
        return;
    }
    file_db.stop = true;
    pthread_cond_signal(&file_db.condition);
    pthread_t thread = file_db.thread;
    pthread_mutex_unlock(&file_db.mutex);
    pthread_join(thread, NULL);

    // Accept requests again, thus they wait for the next start.
    pthread_mutex_lock(&file_db.mutex);
    file_db.stop = false;
    pthread_mutex_unlock(&file_db.mutex);
}

int FileDb_scanAll(void) {
    return enqueue_request(FILEDB_REQUEST_ALL, 0, true);
}

int FileDb_scanDir(int dir_id, bool recursive) {
    if (dir_id < 0) return 0;
    return enqueue_request(FILEDB_REQUEST_DIR, dir_id, recursive);
}

int FileDb_scanFile(int file_id) {
    return enqueue_request(FILEDB_REQUEST_FILE, file_id, false);
}

int FileDb_scanPlaylist(int playlist_id) {
    return enqueue_request(FILEDB_REQUEST_PLAYLIST, playlist_id, false);
}

int FileDb_scanPlaylists(void) {
    return enqueue_request(FILEDB_REQUEST_PLAYLISTS, 0, false);
}

bool FileDb_isRequestDone(int request_id) {
    return request_id <= 0 || request_id <= atomic_load_int(&file_db.last_done);
}

bool FileDb_isRunning(void) {
    pthread_mutex_lock(&file_db.mutex);
    bool running = file_db.running;
    pthread_mutex_unlock(&file_db.mutex);
    return running;
}

bool FileDb_waitBlocking(int request_id) {
    if (request_id <= 0) return false;
    while (!FileDb_isRequestDone(request_id)) {
        if (!FileDb_isRunning()) return FileDb_isRequestDone(request_id);
        usleep(100000);
    }
    return true;
}

bool FileDb_isIdle(void) {
    pthread_mutex_lock(&file_db.mutex);
    bool idle = file_db.running && atomic_load_int(&file_db.idle) != 0 &&
                file_db.requests.head == file_db.requests.count;
    pthread_mutex_unlock(&file_db.mutex);
    return idle;
}

