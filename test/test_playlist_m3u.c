#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "test.h"
#include "db.h"
#include "file_utils.h"
#include "playlist_m3u.h"

#ifndef TEST_TEMP_DIR
#define TEST_TEMP_DIR "."
#endif

#ifndef TEST_MUSIC_PATH
#define TEST_MUSIC_PATH TEST_TEMP_DIR "/playlist-music"
#endif

#ifndef TEST_DATABASE_PATH
#define TEST_DATABASE_PATH TEST_TEMP_DIR "/playlist-m3u.db"
#endif

void LOG_note(int level, const char* format, ...) {
    (void)level;
    (void)format;
}

AudioFormat Player_detectFormat(const char* filepath) {
    const char* extension = strrchr(filepath, '.');
    if (extension && strcasecmp(extension, ".mp3") == 0) return AUDIO_FORMAT_MP3;
    return AUDIO_FORMAT_UNKNOWN;
}

void LOG_error(const char* format, ...) {
    (void)format;
}

static void reset_test_files(void) {
    Db_quit();
    rm_rf(TEST_MUSIC_PATH);
    rm_rf(TEST_DATABASE_PATH);
    rm_rf(TEST_TEMP_DIR "/playlist-userdata");
    mkdir_p(TEST_MUSIC_PATH);
}

static bool start_test(void) {
    reset_test_files();
    return Db_initInternal(TEST_DATABASE_PATH);
}

static void stop_test(void) {
    Db_quit();
    rm_rf(TEST_MUSIC_PATH);
    rm_rf(TEST_DATABASE_PATH);
    rm_rf(TEST_TEMP_DIR "/playlist-userdata");
}

static bool playlist_path(const char* name, char* out, size_t out_size) {
    int length = userdata_snpath(name, out, out_size);
    return length >= 0 && (size_t)length < out_size;
}

// Adds a file row, and sets its type as a file scan does. Returns its id, or 0 on error.
static int add_file_row(int parent_id, const char* filename, DbFileType type) {
    int id = Db_addFile(parent_id, filename);
    bool typed = type == DB_FILE_TYPE_UNKNOWN || Db_updateFileType(id, type);
    return id > 0 && typed ? id : 0;
}

static bool add_file(const char* name, int* id) {
    FILE* file = fopen(TEST_MUSIC_PATH "/placeholder", "ab");
    if (file) fclose(file);
    *id = add_file_row(Db_getRootDirId(), name, DB_FILE_TYPE_MUSIC);
    return *id > 0;
}

// Returns the number of entry lines of an .m3u file, or -1 when it cannot be read.
static int entry_lines(const char* playlist) {
    FILE* file = fopen(playlist, "r");
    if (!file) return -1;
    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] != '#' && line[0] != '\n') count++;
    }
    fclose(file);
    return count;
}

// Appends an entry to an .m3u file as it is, with no check.
static bool append_entry(const char* playlist, const char* track_path, const char* name) {
    FILE* file = fopen(playlist, "a");
    if (!file) return false;
    fprintf(file, "#EXTINF:0,%s\n%s\n", name, track_path);
    fclose(file);
    return true;
}

static int listed_count(const char* name) {
    PlaylistInfo playlists[8];
    int count = M3U_listPlaylists(playlists, 8);
    for (int i = 0; i < count; i++) {
        if (strcmp(playlists[i].name, name) == 0) return playlists[i].track_count;
    }
    return -1;
}

TEST(create_and_delete_updates_the_index) {
    CHECK(start_test());
    M3U_init();
    int playlist_id = 0;
    CHECK((playlist_id = M3U_create("mix")) > 0);
    CHECK(listed_count("mix") == 0);

    char path[512];
    CHECK(playlist_path("playlists/mix.m3u", path, sizeof(path)));
    CHECK(M3U_delete(playlist_id));
    CHECK(listed_count("mix") == -1);
    CHECK(access(path, F_OK) != 0);
    CHECK(!M3U_delete(playlist_id));
    stop_test();
}

TEST(load_tracks_resolves_ids_and_drops_missing_and_external_paths) {
    CHECK(start_test());
    M3U_init();
    int playlist_id = 0;
    CHECK((playlist_id = M3U_create("mix")) > 0);

    int first_id = 0;
    int second_id = 0;
    int image_id = 0;
    CHECK(add_file("first.mp3", &first_id));
    CHECK(add_file("second.mp3", &second_id));
    CHECK((image_id = add_file_row(Db_getRootDirId(), "cover.jpg", DB_FILE_TYPE_OTHER)) > 0);

    char playlist[512];
    CHECK(playlist_path("playlists/mix.m3u", playlist, sizeof(playlist)));
    char gone[512];
    char image[512];
    snprintf(gone, sizeof(gone), "%s/gone.mp3", TEST_MUSIC_PATH);
    snprintf(image, sizeof(image), "%s/cover.jpg", TEST_MUSIC_PATH);
    // A file that is not music and an id with no row are not added.
    int ids[] = { first_id, second_id, image_id, 99999 };
    CHECK_EQ_INT(M3U_addTracks(playlist_id, ids, 4), 2);
    CHECK(append_entry(playlist, image, "Cover"));
    CHECK(append_entry(playlist, gone, "Gone"));
    CHECK(append_entry(playlist, "/outside/song.mp3", "Outside"));

    PlaylistTrack tracks[8];
    CHECK_EQ_INT(M3U_loadTracks(playlist_id, tracks, 8), 2);
    CHECK_EQ_INT(M3U_loadTracks(0, tracks, 8), -1);
    CHECK_EQ_INT(tracks[0].file_id, first_id);
    CHECK_EQ_INT(tracks[1].file_id, second_id);
    CHECK_EQ_INT(listed_count("mix"), 2);
    stop_test();
}

TEST(add_and_remove_update_the_index_count) {
    CHECK(start_test());
    M3U_init();
    int playlist_id = 0;
    CHECK((playlist_id = M3U_create("mix")) > 0);
    PlaylistInfo* info = M3U_getInfo(playlist_id);
    CHECK(info && strcmp(info->name, "mix") == 0);
    free(info);
    CHECK(M3U_getInfo(0) == NULL);
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));

    char playlist[512];
    CHECK(playlist_path("playlists/mix.m3u", playlist, sizeof(playlist)));
    CHECK_EQ_INT(M3U_addTracks(playlist_id, &file_id, 1), 1);
    CHECK_EQ_INT(M3U_addTracks(playlist_id, &file_id, 1), 0);
    CHECK_EQ_INT(M3U_addTracks(0, &file_id, 1), 0);
    CHECK(listed_count("mix") == 1);
    CHECK(M3U_removeTrack(playlist_id, file_id));
    CHECK(listed_count("mix") == 0);
    CHECK(!M3U_removeTrack(playlist_id, file_id));
    stop_test();
}

TEST(remove_finds_the_file_after_an_entry_that_does_not_resolve) {
    CHECK(start_test());
    M3U_init();
    int playlist_id = 0;
    CHECK((playlist_id = M3U_create("mix")) > 0);
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));

    char playlist[512];
    char gone[512];
    CHECK(playlist_path("playlists/mix.m3u", playlist, sizeof(playlist)));
    snprintf(gone, sizeof(gone), "%s/gone.mp3", TEST_MUSIC_PATH);
    CHECK(append_entry(playlist, gone, "Gone"));
    CHECK_EQ_INT(M3U_addTracks(playlist_id, &file_id, 1), 1);

    CHECK(M3U_removeTrack(playlist_id, file_id));
    CHECK_EQ_INT(entry_lines(playlist), 0);
    PlaylistTrack tracks[4];
    CHECK_EQ_INT(M3U_loadTracks(playlist_id, tracks, 4), 0);
    stop_test();
}

TEST(add_removes_entries_that_do_not_resolve) {
    CHECK(start_test());
    M3U_init();
    int playlist_id = 0;
    CHECK((playlist_id = M3U_create("mix")) > 0);
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK(add_file_row(Db_getRootDirId(), "cover.jpg", DB_FILE_TYPE_OTHER) > 0);

    char playlist[512];
    char gone[512];
    char image[512];
    CHECK(playlist_path("playlists/mix.m3u", playlist, sizeof(playlist)));
    snprintf(gone, sizeof(gone), "%s/gone.mp3", TEST_MUSIC_PATH);
    snprintf(image, sizeof(image), "%s/cover.jpg", TEST_MUSIC_PATH);
    CHECK(append_entry(playlist, gone, "Gone"));
    CHECK(append_entry(playlist, image, "Cover"));
    CHECK(append_entry(playlist, "/outside/song.mp3", "Outside"));
    CHECK_EQ_INT(entry_lines(playlist), 3);

    CHECK_EQ_INT(M3U_addTracks(playlist_id, &file_id, 1), 1);
    CHECK_EQ_INT(entry_lines(playlist), 1);
    CHECK_EQ_INT(listed_count("mix"), 1);
    stop_test();
}

int main(void) {
    RUN(create_and_delete_updates_the_index);
    RUN(load_tracks_resolves_ids_and_drops_missing_and_external_paths);
    RUN(add_and_remove_update_the_index_count);
    RUN(remove_finds_the_file_after_an_entry_that_does_not_resolve);
    RUN(add_removes_entries_that_do_not_resolve);
    return test_summary();
}
