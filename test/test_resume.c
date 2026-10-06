#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "test.h"
#include "db.h"
#include "file_utils.h"
#include "playlist_m3u.h"
#include "resume.h"

#ifndef TEST_TEMP_DIR
#define TEST_TEMP_DIR "."
#endif

#ifndef TEST_MUSIC_PATH
#define TEST_MUSIC_PATH TEST_TEMP_DIR "/resume-music"
#endif

#ifndef TEST_DATABASE_PATH
#define TEST_DATABASE_PATH TEST_TEMP_DIR "/resume.db"
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
    rm_rf(TEST_TEMP_DIR "/resume-userdata");
    mkdir_p(TEST_MUSIC_PATH);
}

static bool start_test(void) {
    reset_test_files();
    Resume_init();
    return Db_initInternal(TEST_DATABASE_PATH);
}

static void stop_test(void) {
    Db_quit();
    rm_rf(TEST_MUSIC_PATH);
    rm_rf(TEST_DATABASE_PATH);
    rm_rf(TEST_TEMP_DIR "/resume-userdata");
}

// Adds a file to the card and its row to the index.
// Adds a file row, and sets its type as a file scan does. Returns its id, or 0 on error.
static int add_file_row(int parent_id, const char* filename, DbFileType type) {
    int id = Db_addFile(parent_id, filename);
    bool typed = type == DB_FILE_TYPE_UNKNOWN || Db_updateFileType(id, type);
    return id > 0 && typed ? id : 0;
}

static bool add_file(const char* name, int* id) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_MUSIC_PATH, name);
    FILE* file = fopen(path, "w");
    if (!file) return false;
    fclose(file);
    *id = add_file_row(Db_getRootDirId(), name, DB_FILE_TYPE_MUSIC);
    return *id > 0;
}

// Adds only the row of a file to the index.
static bool add_row(const char* name, int* id) {
    *id = add_file_row(Db_getRootDirId(), name, DB_FILE_TYPE_MUSIC);
    return *id > 0;
}

TEST(folder_resume_uses_saved_file_and_position) {
    CHECK(start_test());
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, Db_getRootDirId(), file_id, NULL, 90000));
    Resume_init();
    const ResumeState* state = Resume_getState();
    CHECK(state != NULL);
    CHECK(state && state->type == RESUME_TYPE_FILES);
    CHECK(state && state->dir_id == Db_getRootDirId());
    CHECK(state && state->playlist_id == 0);
    CHECK(state && state->file_id == file_id);
    CHECK(state && state->position_ms == 90000);
    CHECK(strcmp(Resume_getLabel(), "Resume: song") == 0);
    stop_test();
}

TEST(label_uses_saved_track_name) {
    CHECK(start_test());
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, Db_getRootDirId(), file_id, "At a Place", 0));
    Resume_init();
    CHECK(strcmp(Resume_getLabel(), "Resume: At a Place") == 0);
    stop_test();
}

TEST(file_without_row_has_no_resume_state) {
    CHECK(start_test());
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, Db_getRootDirId(), file_id + 1000, NULL, 0));
    Resume_init();
    CHECK(!Resume_isAvailable());
    CHECK(Resume_getState() == NULL);
    CHECK(Resume_getLabel() == NULL);
    stop_test();
}

TEST(file_gone_from_card_has_no_resume_state) {
    CHECK(start_test());
    int file_id = 0;
    CHECK(add_row("gone.mp3", &file_id));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, Db_getRootDirId(), file_id, NULL, 0));
    Resume_init();
    CHECK(!Resume_isAvailable());
    stop_test();
}

TEST(playlist_resume_uses_saved_file) {
    CHECK(start_test());
    int file_id = 0;
    int playlist_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK((playlist_id = Db_getOrCreatePlaylist("mix.m3u")) > 0);
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_PLAYLIST, playlist_id, file_id, NULL, 5000));
    Resume_init();
    const ResumeState* state = Resume_getState();
    CHECK(state && state->type == RESUME_TYPE_PLAYLIST);
    CHECK(state && state->playlist_id == playlist_id);
    CHECK(state && state->dir_id == 0);
    CHECK(state && state->file_id == file_id);
    CHECK(state && state->position_ms == 5000);
    stop_test();
}

TEST(record_without_flag_is_not_available) {
    CHECK(start_test());
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, Db_getRootDirId(), file_id, NULL, 0));
    CHECK(Db_clearLastPlayed());
    Resume_init();
    CHECK(!Resume_isAvailable());
    stop_test();
}

TEST(clear_removes_record) {
    CHECK(start_test());
    int file_id = 0;
    CHECK(add_file("song.mp3", &file_id));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, Db_getRootDirId(), file_id, NULL, 0));
    Resume_init();
    CHECK(Resume_isAvailable());
    Resume_clear();
    CHECK(!Resume_isAvailable());
    DbLastPlayedResult* result = Db_readLastPlayed();
    CHECK(!result);
    Db_freeResult(result);
    stop_test();
}

int main(void) {
    RUN(folder_resume_uses_saved_file_and_position);
    RUN(label_uses_saved_track_name);
    RUN(file_without_row_has_no_resume_state);
    RUN(file_gone_from_card_has_no_resume_state);
    RUN(playlist_resume_uses_saved_file);
    RUN(record_without_flag_is_not_available);
    RUN(clear_removes_record);
    return test_summary();
}
