#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "browser.h"
#include "db.h"
#include "defines.h"
#include "api.h"
#include "file_utils.h"
#include "filedb.h"
#include "playlist_m3u.h"
#include "utf8.h"
#include "player.h"

void M3U_init(void) {
    userdata_mkdir("playlists");
}

// Count non-comment, non-empty lines in an m3u file (= track count)
static int count_tracks_in_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;

    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Trim trailing newline
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;
        count++;
    }
    fclose(f);
    return count;
}

static bool playlist_file_path(const char* relative, char* out, size_t out_size) {
    char path[1024];
    int length = snprintf(path, sizeof(path), "playlists/%s", relative);
    if (length < 0 || (size_t)length >= sizeof(path)) return false;
    length = userdata_snpath(path, out, out_size);
    return length >= 0 && (size_t)length < out_size;
}

// Fills info from a playlist row. Returns false for a row that is not an .m3u file, or when
// the path of the file does not fit.
static bool fill_info(const DbPlaylist* playlist, PlaylistInfo* info) {
    // Only .m3u files
    const char* name = playlist->path;
    size_t len = strlen(name);
    if (len < 4 || strcasecmp(name + len - 4, ".m3u") != 0) return false;

    if (!playlist_file_path(playlist->path, info->path, sizeof(info->path))) return false;
    info->id = playlist->id;

    // Name without .m3u extension
    snprintf(info->name, sizeof(info->name), "%.*s", (int)len - 4, name);

    info->track_count = playlist->num_entries;
    return true;
}

int M3U_listPlaylists(PlaylistInfo* out, int max) {
    if (!out || max <= 0) return 0;
    DbPlaylistsResult* result = Db_readPlaylists();
    if (!result) return 0;

    int count = 0;
    for (int i = 0; i < result->count && count < max; i++) {
        if (fill_info(&result->items[i], &out[count])) count++;
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcasecmp(out[i].name, out[j].name) > 0) {
                PlaylistInfo tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
    Db_freeResult(result);
    return count;
}

PlaylistInfo* M3U_getInfo(int playlist_id) {
    if (playlist_id <= 0) return NULL;

    DbPlaylistResult* playlist = Db_getPlaylist(playlist_id);
    PlaylistInfo* info = playlist ? malloc(sizeof(*info)) : NULL;
    if (info && !fill_info(&playlist->playlist, info)) {
        free(info);
        info = NULL;
    }
    Db_freeResult(playlist);
    return info;
}

// Makes a user-typed name safe to build a file name from: path separators and control
// characters become '_', surrounding spaces go, and the copy is cut on a character boundary.
// Returns false when nothing usable is left ("", ".", "..").
static bool sanitize_name(const char* name, char* out, size_t out_size) {
    while (*name == ' ') name++;

    // Characters a path cannot hold become '_' before the copy, so the length
    // the copier works to is the one that lands on disk
    char cleaned[512];
    size_t cleaned_length = 0;
    for (; *name && cleaned_length + 1 < sizeof(cleaned); name++) {
        unsigned char c = (unsigned char)*name;
        bool forbidden = (c == '/' || c == '\\' || c < 0x20 || c == 0x7F);
        cleaned[cleaned_length++] = forbidden ? '_' : (char)c;
    }
    cleaned[cleaned_length] = '\0';

    // Whole characters only: a name cut mid-character would name a file nothing
    // can render
    size_t length = UTF8_copy(out, out_size, cleaned);
    while (length > 0 && out[length - 1] == ' ') length--;
    out[length] = '\0';

    // A name made only of dots would address the directory itself
    if (length == 0 || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) return false;
    return true;
}

int M3U_create(const char* name) {
    if (!name || !name[0]) return 0;

    M3U_init();

    char safe_name[MAX_PLAYLIST_NAME];
    if (!sanitize_name(name, safe_name, sizeof(safe_name))) return 0;

    char relative_path[sizeof("playlists/") + MAX_PLAYLIST_NAME + sizeof(".m3u")];
    char playlist_name[MAX_PLAYLIST_NAME + sizeof(".m3u")];
    snprintf(playlist_name, sizeof(playlist_name), "%s.m3u", safe_name);
    snprintf(relative_path, sizeof(relative_path), "playlists/%s", playlist_name);

    char path[512];
    int length = userdata_snpath(relative_path, path, sizeof(path));
    if (length < 0 || (size_t)length >= sizeof(path)) return 0;

    // Don't overwrite existing
    if (access(path, F_OK) == 0) return 0;

    FILE* f = fopen(path, "w");
    if (!f) return 0;

    fprintf(f, "#EXTM3U\n");
    fclose(f);
    int id = Db_getOrCreatePlaylist(playlist_name);
    if (!id || !Db_updatePlaylist(id, 0)) {
        unlink(path);
        return 0;
    }
    return id;
}

// Writes the path of the .m3u file of a playlist to out. Returns false when the playlist has no
// row or the path does not fit.
static bool m3u_path_for_id(int playlist_id, char* out, size_t out_size) {
    DbPlaylistResult* playlist = Db_getPlaylist(playlist_id);
    bool found = playlist && playlist_file_path(playlist->playlist.path, out, out_size);
    Db_freeResult(playlist);
    return found;
}

bool M3U_delete(int playlist_id) {
    if (playlist_id <= 0) return false;

    char m3u_path[1024];
    if (!m3u_path_for_id(playlist_id, m3u_path, sizeof(m3u_path))) return false;
    if (unlink(m3u_path) != 0) return false;
    return Db_deletePlaylist(playlist_id);
}

// Returns the id of the indexed music file that an entry line of an .m3u file names, or 0.
static int entry_music_file_id(const char* entry) {
    char relative_path[1024];
    if (!get_music_relpath(entry, relative_path, sizeof(relative_path))) return 0;
    DbFileResult* result = Db_getFileByPath(relative_path);
    int id = result && result->file.type == DB_FILE_TYPE_MUSIC ? result->file.id : 0;
    Db_freeResult(result);
    return id;
}

// Writes the .m3u file of a playlist again with each entry that resolves to an indexed music
// file, and with no entry of remove_file_id. Pass 0 in remove_file_id to remove no file. Writes
// to *kept_ids the file ids of the entries that stay, with room for extra_ids more, and to
// *removed the number of entries of remove_file_id. Returns the number of entries that stay, or
// -1 when the read or the write failed. The caller frees *kept_ids.
static int rewrite_playlist(const char* m3u_path, int remove_file_id, int extra_ids,
                            int** kept_ids, int* removed) {
    *kept_ids = NULL;
    *removed = 0;
    int entry_lines = count_tracks_in_file(m3u_path);
    char temp_path[1100];
    int length = snprintf(temp_path, sizeof(temp_path), "%s.tmp", m3u_path);
    if (length < 0 || (size_t)length >= sizeof(temp_path)) return -1;

    int* ids = calloc((size_t)(entry_lines + extra_ids + 1), sizeof(*ids));
    FILE* in = ids ? fopen(m3u_path, "r") : NULL;
    FILE* out = in ? fopen(temp_path, "w") : NULL;
    if (!out) {
        if (in) fclose(in);
        free(ids);
        return -1;
    }

    int count = 0;
    char line[1024];
    char extinf[1024] = "";
    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        // An #EXTINF line belongs to the entry that follows it
        if (strncmp(line, "#EXTINF:", 8) == 0) {
            snprintf(extinf, sizeof(extinf), "%s", line);
            continue;
        }
        if (line[0] == '#') {
            fprintf(out, "%s\n", line);
            continue;
        }

        int id = entry_music_file_id(line);
        if (id > 0 && id == remove_file_id) (*removed)++;
        if (id > 0 && id != remove_file_id && count < entry_lines) {
            if (extinf[0]) fprintf(out, "%s\n", extinf);
            fprintf(out, "%s\n", line);
            ids[count++] = id;
        }
        extinf[0] = '\0';
    }

    bool ok = !ferror(in);
    fclose(in);
    ok = fclose(out) == 0 && ok;
    ok = ok && rename(temp_path, m3u_path) == 0;
    if (!ok) {
        unlink(temp_path);
        free(ids);
        return -1;
    }
    *kept_ids = ids;
    return count;
}

static bool has_id(const int* ids, int count, int id) {
    for (int i = 0; i < count; i++) {
        if (ids[i] == id) return true;
    }
    return false;
}

int M3U_addTracks(int playlist_id, const int* file_ids, int file_count) {
    if (playlist_id <= 0 || !file_ids || file_count <= 0) return 0;

    // The entries resolve through the index, thus the index lists their directories first
    FileDb_waitBlocking(FileDb_scanPlaylist(playlist_id));

    char m3u_path[1024];
    if (!m3u_path_for_id(playlist_id, m3u_path, sizeof(m3u_path))) return 0;

    int* ids = NULL;
    int removed = 0;
    int count = rewrite_playlist(m3u_path, 0, file_count, &ids, &removed);
    if (count < 0) return 0;

    FILE* f = fopen(m3u_path, "a");
    int added = 0;
    for (int i = 0; f && i < file_count; i++) {
        if (has_id(ids, count + added, file_ids[i])) continue;

        DbFileResult* file = Db_getFile(file_ids[i]);
        char track_path[1024];
        char name[256];
        bool music = file && file->file.type == DB_FILE_TYPE_MUSIC &&
                     get_music_abspath(file->file.path, track_path, sizeof(track_path));
        if (music) get_file_display_name(file->file.filename, name, sizeof(name));
        Db_freeResult(file);
        if (!music) continue;

        fprintf(f, "#EXTINF:0,%s\n%s\n", name, track_path);
        ids[count + added++] = file_ids[i];
    }
    if (f && fclose(f) != 0) added = 0;
    free(ids);
    Db_updatePlaylist(playlist_id, count + added);
    return added;
}

bool M3U_removeTrack(int playlist_id, int file_id) {
    if (playlist_id <= 0 || file_id <= 0) return false;

    // The entries resolve through the index, thus the index lists their directories first
    FileDb_waitBlocking(FileDb_scanPlaylist(playlist_id));

    char m3u_path[1024];
    if (!m3u_path_for_id(playlist_id, m3u_path, sizeof(m3u_path))) return false;

    int* ids = NULL;
    int removed = 0;
    int count = rewrite_playlist(m3u_path, file_id, 0, &ids, &removed);
    free(ids);
    if (count < 0) return false;
    Db_updatePlaylist(playlist_id, count);
    return removed > 0;
}

int M3U_loadTracks(int playlist_id, PlaylistTrack* tracks, int max) {
    if (playlist_id <= 0 || !tracks) return -1;

    // The entries resolve through the index, thus the index lists their directories first
    FileDb_waitBlocking(FileDb_scanPlaylist(playlist_id));

    char m3u_path[1024];
    if (!m3u_path_for_id(playlist_id, m3u_path, sizeof(m3u_path))) return -1;

    FILE* f = fopen(m3u_path, "r");
    if (!f) return -1;

    int count = 0;
    char line[1024];
    char last_extinf_name[256] = "";

    while (fgets(line, sizeof(line), f) && count < max) {
        // Trim trailing newline
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        // Parse #EXTINF for display name
        if (strncmp(line, "#EXTINF:", 8) == 0) {
            char* comma = strchr(line + 8, ',');
            if (comma) {
                snprintf(last_extinf_name, sizeof(last_extinf_name), "%s", comma + 1);
            }
            continue;
        }

        // Skip other comment lines
        if (line[0] == '#') continue;

        char relative_path[1024];
        if (!get_music_relpath(line, relative_path, sizeof(relative_path))) {
            last_extinf_name[0] = '\0';
            continue;
        }

        DbFileResult* result = Db_getFileByPath(relative_path);
        if (!result) {
            Db_freeResult(result);
            last_extinf_name[0] = '\0';
            continue;
        }
        DbFile file = result->file;
        if (file.type != DB_FILE_TYPE_MUSIC) {
            Db_freeResult(result);
            last_extinf_name[0] = '\0';
            continue;
        }
        PlaylistTrack* track = &tracks[count];
        if (!get_music_abspath(file.path, track->path, sizeof(track->path))) {
            Db_freeResult(result);
            last_extinf_name[0] = '\0';
            continue;
        }
        track->file_id = file.id;

        // Use EXTINF name if available, otherwise extract from filename
        if (last_extinf_name[0]) {
            snprintf(track->name, sizeof(track->name), "%s", last_extinf_name);
        } else {
            snprintf(track->name, sizeof(track->name), "%s", file.filename);
        }

        track->format = Player_detectFormat(file.filename);
        Db_freeResult(result);
        last_extinf_name[0] = '\0';
        count++;
    }

    fclose(f);
    Db_updatePlaylist(playlist_id, count);
    return count;
}
