#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "browser.h"
#include "db.h"
#include "filedb.h"
#include "file_utils.h"

void Playlist_init(PlaylistContext* ctx) {
    if (!ctx) return;
    ctx->tracks = malloc(sizeof(PlaylistTrack) * PLAYLIST_MAX_TRACKS);
    ctx->track_count = 0;
    ctx->current_index = 0;
}

void Playlist_free(PlaylistContext* ctx) {
    if (!ctx) return;
    free(ctx->tracks);
    ctx->tracks = NULL;
    ctx->track_count = 0;
    ctx->current_index = 0;
}

void Playlist_clear(PlaylistContext* ctx) {
    if (!ctx) return;
    ctx->track_count = 0;
    ctx->current_index = 0;
}

static bool copy_text(char* out, size_t out_size, const char* value) {
    int length = snprintf(out, out_size, "%s", value ? value : "");
    return length >= 0 && (size_t)length < out_size;
}

static bool fill_track(PlaylistTrack* track, const DbFile* file) {
    if (!get_music_abspath(file->path, track->path, sizeof(track->path)) ||
        !copy_text(track->name, sizeof(track->name), file->filename)) {
        return false;
    }
    track->file_id = file->id;
    track->format = Player_detectFormat(file->filename);
    return true;
}

static int append_tracks(PlaylistTrack* tracks, int count, int max_tracks,
                         const PlaylistTrack* items, int item_count) {
    for (int i = 0; i < item_count && count < max_tracks; i++) {
        tracks[count++] = items[i];
    }
    return count;
}

// Pages the playable files of an indexed subtree into tracks, which holds
// max_tracks items. The file start_file_id and the files after it come first.
// When the start file is in dir_id itself, the files before it follow the
// other files of dir_id, and the files of the subdirectories come last.
// Otherwise the files before it come last. Returns the number of tracks, or -1
// on error.
static int collect_tracks(int dir_id, int start_file_id, PlaylistTrack* tracks,
                          int max_tracks) {
    PlaylistTrack* before = NULL;
    if (start_file_id > 0) {
        before = malloc((size_t)max_tracks * sizeof(*before));
        if (!before) return -1;
    }

    int count = 0;
    int before_count = 0;
    bool found = start_file_id <= 0;
    bool start_in_dir = false;
    bool before_done = false;
    int token = 0;
    bool more = true;
    bool success = true;
    while (success && more && count < max_tracks) {
        DbFilesResult* page = Db_getFiles(dir_id, DB_FILE_TYPE_MUSIC, true, PLAYLIST_MAX_TRACKS, token);
        if (!page) {
            success = false;
            break;
        }
        for (int i = 0; success && i < page->count && count < max_tracks; i++) {
            const DbFile* file = &page->items[i];
            if (!found && file->id == start_file_id) {
                found = true;
                start_in_dir = file->parent_id == dir_id;
            } else if (found && start_in_dir && !before_done &&
                       file->parent_id != dir_id) {
                count = append_tracks(tracks, count, max_tracks, before,
                                      before_count);
                before_done = true;
                if (count >= max_tracks) break;
            }
            if (found) {
                success = fill_track(&tracks[count], file);
                if (success) count++;
            } else if (before_count < max_tracks) {
                success = fill_track(&before[before_count], file);
                if (success) before_count++;
            }
        }
        more = page->has_more && page->count > 0;
        if (page->count > 0) token = page->items[page->count - 1].id;
        Db_freeResult(page);
    }

    if (success && before && !before_done) {
        if (!found) count = 0;
        count = append_tracks(tracks, count, max_tracks, before, before_count);
    }
    free(before);
    return success ? count : -1;
}

int Playlist_buildFromDirectory(PlaylistContext* ctx, int dir_id,
                                int start_file_id) {
    if (!ctx || dir_id < 0) return -1;
    if (!ctx->tracks) {
        Playlist_init(ctx);
        if (!ctx->tracks) return -1;
    }

    Playlist_clear(ctx);
    int request = FileDb_scanDir(dir_id, true);
    if (!FileDb_waitBlocking(request)) return -1;

    int count = collect_tracks(dir_id, start_file_id, ctx->tracks,
                               PLAYLIST_MAX_TRACKS);
    if (count < 0) return -1;
    ctx->track_count = count;
    ctx->current_index = 0;
    return count;
}

int Playlist_collectDirectory(int dir_id, int max_tracks, PlaylistTrack** tracks) {
    if (!tracks) return -1;
    *tracks = NULL;
    if (dir_id < 0 || max_tracks <= 0) return -1;

    int request = FileDb_scanDir(dir_id, true);
    if (!FileDb_waitBlocking(request)) return -1;

    PlaylistTrack* items = malloc((size_t)max_tracks * sizeof(*items));
    if (!items) return -1;
    int count = collect_tracks(dir_id, 0, items, max_tracks);
    if (count <= 0) {
        free(items);
        return count;
    }
    *tracks = items;
    return count;
}

int Playlist_next(PlaylistContext* ctx) {
    if (!ctx || ctx->track_count == 0 ||
        ctx->current_index >= ctx->track_count - 1) return -1;
    ctx->current_index++;
    return ctx->current_index;
}

int Playlist_prev(PlaylistContext* ctx) {
    if (!ctx || ctx->track_count == 0 || ctx->current_index <= 0) return -1;
    ctx->current_index--;
    return ctx->current_index;
}

int Playlist_shuffle(PlaylistContext* ctx) {
    if (!ctx || ctx->track_count == 0) return -1;
    if (ctx->track_count == 1) return 0;
    int index;
    do {
        index = rand() % ctx->track_count;
    } while (index == ctx->current_index);
    ctx->current_index = index;
    return index;
}

int Playlist_setCurrentIndex(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->track_count) return -1;
    ctx->current_index = index;
    return 0;
}

const PlaylistTrack* Playlist_getCurrentTrack(const PlaylistContext* ctx) {
    if (!ctx || !ctx->tracks || ctx->current_index < 0 ||
        ctx->current_index >= ctx->track_count) return NULL;
    return &ctx->tracks[ctx->current_index];
}

const PlaylistTrack* Playlist_getTrack(const PlaylistContext* ctx, int index) {
    if (!ctx || !ctx->tracks || index < 0 || index >= ctx->track_count) return NULL;
    return &ctx->tracks[index];
}

int Playlist_getCount(const PlaylistContext* ctx) {
    return ctx ? ctx->track_count : 0;
}

int Playlist_getCurrentIndex(const PlaylistContext* ctx) {
    return ctx ? ctx->current_index : 0;
}

bool Playlist_isActive(const PlaylistContext* ctx) {
    return ctx && ctx->tracks && ctx->track_count > 0;
}
