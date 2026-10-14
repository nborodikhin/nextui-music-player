#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "test.h"
#include "db.h"
#include "file_utils.h"
#include "filedb.h"
#include "player.h"
#include "playlist.h"

#ifndef PLAYLIST_MUSIC_PATH
#define PLAYLIST_MUSIC_PATH "./playlist-music"
#endif

#ifndef PLAYLIST_DATABASE_PATH
#define PLAYLIST_DATABASE_PATH "./playlist.db"
#endif

#define BIG_FOLDER_FILES 510

void LOG_note(int level, const char* format, ...) {
    (void)level;
    (void)format;
}

AudioFormat Player_detectFormat(const char* filepath) {
    const char* extension = strrchr(filepath, '.');
    if (extension && strcasecmp(extension, ".mp3") == 0) return AUDIO_FORMAT_MP3;
    return AUDIO_FORMAT_UNKNOWN;
}

static bool write_file(const char* directory, const char* name) {
    char path[1024];
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)) {
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    fputc(0, file);
    fclose(file);
    return true;
}

static bool start_test(void) {
    FileDb_quit();
    Db_quit();
    rm_rf(PLAYLIST_MUSIC_PATH);
    rm_rf(PLAYLIST_DATABASE_PATH);
    if (!mkdir_p(PLAYLIST_MUSIC_PATH "/Big")) return false;
    for (int i = 0; i < BIG_FOLDER_FILES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "f%03d.mp3", i);
        if (!write_file(PLAYLIST_MUSIC_PATH "/Big", name)) return false;
    }
    if (!write_file(PLAYLIST_MUSIC_PATH "/Big", "notes.txt")) return false;
    if (!mkdir_p(PLAYLIST_MUSIC_PATH "/Small/Sub")) return false;
    if (!write_file(PLAYLIST_MUSIC_PATH "/Small", "a.mp3")) return false;
    if (!write_file(PLAYLIST_MUSIC_PATH "/Small", "b.mp3")) return false;
    if (!write_file(PLAYLIST_MUSIC_PATH "/Small", "c.mp3")) return false;
    if (!write_file(PLAYLIST_MUSIC_PATH "/Small/Sub", "d.mp3")) return false;
    if (!write_file(PLAYLIST_MUSIC_PATH "/Small/Sub", "e.mp3")) return false;
    if (!Db_initInternal(PLAYLIST_DATABASE_PATH)) return false;
    FileDb_start();
    int request = FileDb_scanAll();
    return FileDb_waitBlocking(request);
}

static void stop_test(void) {
    FileDb_quit();
    Db_quit();
    rm_rf(PLAYLIST_MUSIC_PATH);
    rm_rf(PLAYLIST_DATABASE_PATH);
}

static int dir_id_at(const char* path) {
    DbDirResult* result = Db_getDirByPath(path);
    int id = result ? result->dir.id : -1;
    Db_freeResult(result);
    return id;
}

static int file_id_at(const char* path) {
    DbFileResult* result = Db_getFileByPath(path);
    int id = result ? result->file.id : 0;
    Db_freeResult(result);
    return id;
}

TEST(queue_starts_at_selected_file_past_first_page) {
    CHECK(start_test());
    int big = dir_id_at("Big");
    int start = file_id_at("Big/f505.mp3");
    CHECK(big > 0 && start > 0);

    PlaylistContext playlist = {0};
    int count = Playlist_buildFromDirectory(&playlist, big, start);
    CHECK_EQ_INT(count, PLAYLIST_MAX_TRACKS);
    CHECK_EQ_INT(Playlist_getCurrentIndex(&playlist), 0);
    const PlaylistTrack* first = Playlist_getTrack(&playlist, 0);
    const PlaylistTrack* wrapped = Playlist_getTrack(&playlist, 5);
    CHECK(first && strcmp(first->name, "f505.mp3") == 0);
    CHECK(wrapped && strcmp(wrapped->name, "f000.mp3") == 0);
    Playlist_free(&playlist);
    stop_test();
}

TEST(queue_without_start_file_holds_first_files) {
    CHECK(start_test());
    PlaylistContext playlist = {0};
    int count = Playlist_buildFromDirectory(&playlist, dir_id_at("Big"), 0);
    CHECK_EQ_INT(count, PLAYLIST_MAX_TRACKS);
    const PlaylistTrack* first = Playlist_getTrack(&playlist, 0);
    const PlaylistTrack* last = Playlist_getTrack(&playlist, count - 1);
    CHECK(first && strcmp(first->name, "f000.mp3") == 0);
    CHECK(last && strcmp(last->name, "f499.mp3") == 0);
    Playlist_free(&playlist);
    stop_test();
}

static bool queue_order_is(const PlaylistContext* playlist, const char* order) {
    int count = Playlist_getCount(playlist);
    if (count != (int)strlen(order)) return false;
    for (int i = 0; i < count; i++) {
        const PlaylistTrack* track = Playlist_getTrack(playlist, i);
        if (!track || track->name[0] != order[i]) return false;
    }
    return true;
}

TEST(queue_puts_earlier_files_before_subdirectory_files) {
    CHECK(start_test());
    PlaylistContext playlist = {0};
    int small = dir_id_at("Small");
    CHECK(Playlist_buildFromDirectory(&playlist, small,
                                      file_id_at("Small/b.mp3")) == 5);
    CHECK(queue_order_is(&playlist, "bcade"));
    CHECK(Playlist_buildFromDirectory(&playlist, small,
                                      file_id_at("Small/Sub/e.mp3")) == 5);
    CHECK(queue_order_is(&playlist, "eabcd"));
    CHECK(Playlist_buildFromDirectory(&playlist, small, 0) == 5);
    CHECK(queue_order_is(&playlist, "abcde"));
    Playlist_free(&playlist);
    stop_test();
}

TEST(directory_collection_pages_past_queue_limit) {
    CHECK(start_test());
    PlaylistTrack* tracks = NULL;
    int count = Playlist_collectDirectory(dir_id_at("Big"), 1000, &tracks);
    CHECK_EQ_INT(count, BIG_FOLDER_FILES);
    CHECK(tracks && strcmp(tracks[BIG_FOLDER_FILES - 1].name, "f509.mp3") == 0);
    free(tracks);
    stop_test();
}

int main(void) {
    RUN(queue_starts_at_selected_file_past_first_page);
    RUN(queue_without_start_file_holds_first_files);
    RUN(queue_puts_earlier_files_before_subdirectory_files);
    RUN(directory_collection_pages_past_queue_limit);
    return test_summary();
}
