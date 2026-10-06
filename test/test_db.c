#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#include "db.h"
#include "db_schema.h"
#include "db_statement.h"
#include "defines.h"
#include "file_utils.h"
#include "test.h"

// The id of the Music root, for a row inserted with SQL.
#define ROOT_DIR_SQL "(SELECT id FROM dirs WHERE parent_id IS NULL)"

static char temp_dir[512];
static char database_path[1024];

void LOG_note(int level, const char* format, ...) {
    (void)level;
    (void)format;
}

static bool start_test(void) {
    if (!mk_tempdir("db-test", temp_dir, sizeof(temp_dir))) return false;
    snprintf(database_path, sizeof(database_path), "%s/music-player.db", temp_dir);
    return true;
}

static void stop_test(void) {
    Db_quit();
    rm_rf(temp_dir);
    temp_dir[0] = '\0';
    database_path[0] = '\0';
}

static bool sqlite_exec(const char* path, const char* sql) {
    sqlite3* database = NULL;
    char* error = NULL;
    int result = sqlite3_open(path, &database);
    if (result == SQLITE_OK) result = sqlite3_exec(database, sql, NULL, NULL, &error);
    sqlite3_free(error);
    if (database) sqlite3_close(database);
    return result == SQLITE_OK;
}

static int sqlite_user_version(const char* path) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    int version = -1;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &statement, NULL) ==
            SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return version;
}

static int sqlite_settings_count(const char* path) {
    sqlite3* database = NULL;
    int count = -1;
    if (sqlite3_open(path, &database) == SQLITE_OK) {
        sqlite3_stmt* statement = NULL;
        if (sqlite3_prepare_v2(database, "SELECT COUNT(*) FROM settings", -1,
                               &statement, NULL) == SQLITE_OK &&
            sqlite3_step(statement) == SQLITE_ROW) {
            count = sqlite3_column_int(statement, 0);
        }
        sqlite3_finalize(statement);
    }
    if (database) sqlite3_close(database);
    return count;
}

static bool sqlite_has_table(const char* path, const char* name) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    bool exists = false;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(
            database,
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?",
            -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_text(statement, 1, name, -1, SQLITE_STATIC) == SQLITE_OK) {
        exists = sqlite3_step(statement) == SQLITE_ROW;
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return exists;
}

static bool sqlite_has_index(const char* path, const char* name) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    bool exists = false;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(
            database,
            "SELECT 1 FROM sqlite_master WHERE type = 'index' AND name = ?",
            -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_text(statement, 1, name, -1, SQLITE_STATIC) == SQLITE_OK) {
        exists = sqlite3_step(statement) == SQLITE_ROW;
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return exists;
}

static bool sqlite_has_column(const char* path, const char* table,
                              const char* column, const char* type) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    char sql[128];
    bool exists = false;
    int length = snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table);

    if (length >= 0 && (size_t)length < sizeof(sql) &&
        sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(database, sql, -1, &statement, NULL) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(statement, 1);
            const unsigned char* column_type = sqlite3_column_text(statement, 2);
            if (name && strcmp((const char*)name, column) == 0 &&
                (!type || (column_type &&
                           strcasecmp((const char*)column_type, type) == 0))) {
                exists = true;
                break;
            }
        }
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return exists;
}

static bool sqlite_has_root_directory(const char* path) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    bool exists = false;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(database,
                           "SELECT parent_id, path, filename "
                           "FROM dirs WHERE parent_id IS NULL",
                           -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* path_value = sqlite3_column_text(statement, 1);
        const unsigned char* name_value = sqlite3_column_text(statement, 2);
        exists = sqlite3_column_type(statement, 0) == SQLITE_NULL &&
                 path_value && strcmp((const char*)path_value, "") == 0 &&
                 name_value && strcmp((const char*)name_value, "") == 0;
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return exists;
}

static bool sqlite_setting_value(const char* path, const char* name,
                                 char* value, size_t size) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    bool found = false;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(database,
                           "SELECT value FROM settings WHERE name = ?",
                           -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_text(statement, 1, name, -1, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(statement, 0);
        if (text) {
            snprintf(value, size, "%s", text);
            found = true;
        }
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return found;
}

static int sqlite_last_played_flag(const char* path, const char* type) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    int flag = -1;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(database,
                           "SELECT is_last FROM last_played WHERE type = ?",
                           -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_bind_text(statement, 1, type, -1, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* value = sqlite3_column_text(statement, 0);
        flag = value && strcmp((const char*)value, "true") == 0 ? 1 : 0;
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return flag;
}

static bool write_resume_file(const char* contents) {
    if (!userdata_mkdir("")) return false;
    char path[512];
    int length = userdata_snpath("resume.cfg", path, sizeof(path));
    if (length < 0 || (size_t)length >= sizeof(path)) return false;

    FILE* file = fopen(path, "w");
    if (!file) return false;
    bool success = fputs(contents, file) >= 0;
    if (fclose(file) != 0) success = false;
    return success;
}

static bool create_schema_at_version_six(const char* path) {
    DbMigrationStep* steps = DbSchema_getSteps();
    if (!steps) return false;
    bool success =
        sqlite_exec(path,
                    "CREATE TABLE settings (name TEXT PRIMARY KEY NOT NULL, "
                    "type TEXT NOT NULL, value NOT NULL)") &&
        sqlite_exec(path, steps[5].sql) &&
        sqlite_exec(path, "PRAGMA user_version = 6");
    free(steps);
    return success;
}

TEST(fresh_database_gets_schema) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_isAvailable());
    CHECK_EQ_INT(sqlite_user_version(database_path), 12);
    CHECK(sqlite_has_table(database_path, "settings"));
    CHECK(sqlite_has_table(database_path, "dirs"));
    CHECK(sqlite_has_table(database_path, "files"));
    CHECK(!sqlite_has_table(database_path, "music_dirs"));
    CHECK(!sqlite_has_table(database_path, "music_files"));
    CHECK(sqlite_has_table(database_path, "playlists"));
    CHECK(sqlite_has_table(database_path, "last_played"));
    CHECK(sqlite_has_column(database_path, "files", "id", "INTEGER"));
    CHECK(sqlite_has_column(database_path, "files", "parent_id", "INTEGER"));
    CHECK(sqlite_has_column(database_path, "files", "filename", "TEXT"));
    CHECK(sqlite_has_column(database_path, "files", "type", "TEXT"));
    CHECK(!sqlite_has_column(database_path, "files", "title", NULL));
    CHECK(!sqlite_has_column(database_path, "files", "artist", NULL));
    CHECK(!sqlite_has_column(database_path, "files", "album", NULL));
    CHECK(sqlite_has_root_directory(database_path));
    CHECK(Db_getRootDirId() > 0);
    CHECK(!sqlite_has_index(database_path, "files_parent"));
    CHECK(sqlite_has_index(database_path, "dirs_parent"));
    stop_test();
}

TEST(settings_file_migration_runs_ordered_actions) {
    CHECK(start_test());
    CHECK(userdata_mkdir(""));

    char settings_path[512];
    CHECK(userdata_snpath("settings.cfg", settings_path, sizeof(settings_path)) >= 0);
    FILE* file = fopen(settings_path, "w");
    CHECK(file != NULL);
    if (file) {
        fputs("screen_off_timeout=90\n"
              "lyrics_enabled=0\n"
              "bass_filter_hz=200\n"
              "soft_limiter=3\n"
              "auto_update=0\n", file);
        CHECK(fclose(file) == 0);
    }

    CHECK(sqlite_exec(database_path,
                      "CREATE TABLE settings (name TEXT PRIMARY KEY NOT NULL, type TEXT NOT NULL, "
                      "value NOT NULL);"
                      "PRAGMA user_version = 1"));
    CHECK(Db_initInternal(database_path));

    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->count == 5);
    Db_freeResult(result);
    CHECK(access(settings_path, F_OK) != 0);
    CHECK_EQ_INT(sqlite_user_version(database_path), 12);

    Db_quit();
    CHECK(Db_initInternal(database_path));
    CHECK(access(settings_path, F_OK) != 0);
    stop_test();
}

TEST(failed_settings_migration_keeps_no_partial_rows) {
    CHECK(start_test());
    CHECK(userdata_mkdir(""));

    char settings_path[512];
    CHECK(userdata_snpath("settings.cfg", settings_path, sizeof(settings_path)) >= 0);
    FILE* file = fopen(settings_path, "w");
    CHECK(file != NULL);
    if (file) {
        fputs("screen_off_timeout=90\n"
              "bass_filter_hz=200\n", file);
        CHECK(fclose(file) == 0);
    }

    // The second write of the settings migration fails.
    CHECK(sqlite_exec(database_path,
                      "CREATE TABLE settings (name TEXT PRIMARY KEY NOT NULL, type TEXT NOT NULL, "
                      "value NOT NULL);"
                      "CREATE TRIGGER fail_bass BEFORE INSERT ON settings "
                      "WHEN NEW.name = 'bass_filter_hz' "
                      "BEGIN SELECT RAISE(ABORT, 'test'); END;"
                      "PRAGMA user_version = 1"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_settings_count(database_path), 0);
    CHECK_EQ_INT(sqlite_user_version(database_path), 3);
    CHECK(access(settings_path, F_OK) == 0);

    Db_quit();
    CHECK(sqlite_exec(database_path, "DROP TRIGGER fail_bass"));
    CHECK(Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_settings_count(database_path), 2);
    CHECK_EQ_INT(sqlite_user_version(database_path), 12);
    CHECK(access(settings_path, F_OK) != 0);
    stop_test();
}

TEST(open_failure_disables_database) {
    CHECK(start_test());
    char missing_path[sizeof(database_path)];
    snprintf(missing_path, sizeof(missing_path), "%s/missing/music-player.db", temp_dir);
    CHECK(!Db_initInternal(missing_path));
    CHECK(!Db_isAvailable());
    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->count == 0);
    Db_freeResult(result);
    stop_test();
}

TEST(newer_database_is_not_changed) {
    CHECK(start_test());
    CHECK(sqlite_exec(database_path, "PRAGMA user_version = 99"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 99);
    CHECK(!sqlite_has_table(database_path, "settings"));
    stop_test();
}

TEST(failed_schema_migration_rolls_back) {
    CHECK(start_test());
    CHECK(sqlite_exec(database_path, "CREATE TABLE settings (wrong INTEGER)"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 0);
    CHECK(sqlite_exec(database_path, "DROP TABLE settings"));
    CHECK(Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 12);
    stop_test();
}

TEST(resume_file_is_removed_without_copying) {
    CHECK(start_test());
    const char* resume =
        "type=1\n"
        "folder_path=/mnt/SDCARD/Music/Album\n"
        "track_path=/mnt/SDCARD/Music/Album/song.mp3\n"
        "position_ms=90000\n";
    CHECK(write_resume_file(resume));
    CHECK(create_schema_at_version_six(database_path));
    CHECK(Db_initInternal(database_path));

    DbLastPlayedResult* last_played = Db_readLastPlayed();
    CHECK(!last_played);
    Db_freeResult(last_played);

    char resume_path[512];
    CHECK(userdata_snpath("resume.cfg", resume_path, sizeof(resume_path)) >= 0);
    CHECK(access(resume_path, F_OK) != 0);
    stop_test();
}

TEST(settings_save_read_and_reset) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute("DELETE FROM settings"));
    CHECK(Db_saveIntSetting("int", 42));
    CHECK(Db_saveBoolSetting("bool", true));
    CHECK(Db_saveStringSetting("string", "value"));

    char bool_value[sizeof("true")];
    CHECK(sqlite_setting_value(database_path, "bool", bool_value,
                               sizeof(bool_value)));
    CHECK(strcmp(bool_value, "true") == 0);

    DbSettingsResult* saved = Db_readSettings();
    CHECK(saved != NULL);
    CHECK(saved && saved->count == 3);
    CHECK(saved && saved->items[0].name != NULL);
    Db_freeResult(saved);

    Db_quit();
    CHECK(Db_initInternal(database_path));
    DbSettingsResult* restored = Db_readSettings();
    CHECK(restored != NULL);
    CHECK(restored && restored->count == 3);
    Db_freeResult(restored);
    stop_test();
}

TEST(result_frees_through_shared_header) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_saveStringSetting("shared", "result"));

    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->base.free != NULL);
    Db_freeResult(result);
    stop_test();
}

TEST(index_reads_copy_rows) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute(
        "INSERT INTO dirs (id, parent_id, path, filename) VALUES "
        "(101, " ROOT_DIR_SQL ", 'b', 'b'),"
        "(102, " ROOT_DIR_SQL ", 'A', 'A'),"
        "(103, 102, 'A/Blue', 'Blue')"));
    CHECK(Db_execute(
        "INSERT INTO files (id, parent_id, filename, type) VALUES "
        "(10, " ROOT_DIR_SQL ", 'z.mp3', 'music'),"
        "(11, " ROOT_DIR_SQL ", 'Y.flac', 'music'),"
        "(13, " ROOT_DIR_SQL ", 'readme.txt', 'unknown'),"
        "(12, 102, 'track.mp3', 'music')"));
    CHECK(Db_execute(
        "INSERT INTO playlists (id, path, num_entries) VALUES "
        "(20, 'mix.m3u', 2)"));
    CHECK(Db_execute(
        "INSERT INTO last_played (type, id1, id2, position, is_last) "
        "VALUES ('folder', 102, 12, 9000, 'true')"));

    DbDirResult* dir = Db_getDir(102);
    CHECK(dir);
    CHECK(dir && dir->dir.parent_id == Db_getRootDirId());
    CHECK(dir && strcmp(dir->dir.path, "A") == 0);
    CHECK_EQ_INT(Db_countFiles(102, DB_FILE_TYPE_ALL, false), 1);

    CHECK_EQ_INT(Db_countFiles(Db_getRootDirId(), DB_FILE_TYPE_ALL, false), 3);
    CHECK_EQ_INT(Db_countFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, false), 2);
    CHECK_EQ_INT(Db_countFiles(Db_getRootDirId(), DB_FILE_TYPE_UNKNOWN, false), 1);
    CHECK_EQ_INT(Db_countFiles(99999, DB_FILE_TYPE_ALL, false), 0);
    CHECK_EQ_INT(Db_countFiles(Db_getRootDirId(), DB_FILE_TYPE_ALL, true), 4);
    CHECK_EQ_INT(Db_countFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, true), 3);
    CHECK_EQ_INT(Db_countFiles(102, DB_FILE_TYPE_MUSIC, true), 1);

    DbDirResult* dir_by_path = Db_getDirByPath("A/Blue");
    CHECK(dir_by_path);
    CHECK(dir_by_path && dir_by_path->dir.id == 103);

    DbFileResult* file = Db_getFile(12);
    CHECK(file);
    CHECK(file && strcmp(file->file.dir_path, "A") == 0);
    CHECK(file && strcmp(file->file.path, "A/track.mp3") == 0);
    CHECK(file && strcmp(file->file.filename, "track.mp3") == 0);
    CHECK(file && file->file.type == DB_FILE_TYPE_MUSIC);

    DbFileResult* file_by_path = Db_getFileByPath("A/track.mp3");
    CHECK(file_by_path);
    CHECK(file_by_path && file_by_path->file.id == 12);

    DbFileResult* file_in_dir = Db_getFileInDir(102, "track.mp3");
    CHECK(file_in_dir);
    CHECK(file_in_dir && file_in_dir->file.id == 12);

    DbReadDirResult* contents = Db_readDir(Db_getRootDirId());
    CHECK(contents && contents->dir_count == 2);
    CHECK(contents && contents->file_count == 3);
    CHECK(contents && contents->count == 5);
    CHECK(contents && contents->dirs && strcmp(contents->dirs[0].filename, "A") == 0);
    CHECK(contents && contents->dirs && strcmp(contents->dirs[1].filename, "b") == 0);
    CHECK(contents && contents->files && strcmp(contents->files[0].filename, "readme.txt") == 0);
    CHECK(contents && contents->files && contents->files[0].type == DB_FILE_TYPE_UNKNOWN);
    CHECK(contents && contents->files && strcmp(contents->files[1].filename, "Y.flac") == 0);
    CHECK(contents && contents->files && strcmp(contents->files[2].filename, "z.mp3") == 0);

    DbPlaylistResult* playlist = Db_getPlaylist(20);
    CHECK(playlist);
    CHECK(playlist && strcmp(playlist->playlist.path, "mix.m3u") == 0);
    CHECK(playlist && playlist->playlist.num_entries == 2);

    DbPlaylistsResult* playlists = Db_readPlaylists();
    CHECK(playlists && playlists->count == 1);
    CHECK(playlists && playlists->items && playlists->items[0].id == 20);

    DbLastPlayedResult* last_played = Db_readLastPlayed();
    CHECK(last_played);
    CHECK(last_played &&
          last_played->last_played.type == DB_LAST_PLAYED_FOLDER);
    CHECK(last_played && last_played->last_played.position == 9000);

    CHECK(Db_execute("DELETE FROM dirs WHERE id = 102"));
    CHECK(dir && strcmp(dir->dir.path, "A") == 0);
    CHECK(file && strcmp(file->file.filename, "track.mp3") == 0);

    Db_freeResult(dir);
    Db_freeResult(dir_by_path);
    Db_freeResult(file);
    Db_freeResult(file_by_path);
    Db_freeResult(file_in_dir);
    Db_freeResult(contents);
    Db_freeResult(playlist);
    Db_freeResult(playlists);
    Db_freeResult(last_played);
    stop_test();
}

TEST(file_pages_use_keyset_tokens) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute(
        "INSERT INTO dirs (id, parent_id, path, filename) "
        "VALUES (101, " ROOT_DIR_SQL ", 'Page', 'Page')"));

    char sql[256];
    for (int index = 0; index < 700; index++) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO files (parent_id, filename, type) "
                 "VALUES (101, 'track%03d.mp3', 'music')", index);
        CHECK(Db_execute(sql));
    }

    DbFilesResult* first = Db_getFiles(101, DB_FILE_TYPE_MUSIC, true, 500, 0);
    CHECK(first && first->count == 500);
    CHECK(first && first->has_more);
    int token = first && first->count > 0 ? first->items[first->count - 1].id : 0;
    CHECK_EQ_INT(token, 500);

    DbFilesResult* second = Db_getFiles(101, DB_FILE_TYPE_MUSIC, true, 500, token);
    CHECK(second && second->count == 200);
    CHECK(second && !second->has_more);
    if (first && second && first->count == 500 && second->count == 200) {
        CHECK(strcmp(first->items[499].filename, "track499.mp3") == 0);
        CHECK(strcmp(second->items[0].filename, "track500.mp3") == 0);
        for (int first_index = 0; first_index < first->count; first_index++) {
            for (int second_index = 0; second_index < second->count; second_index++) {
                CHECK(first->items[first_index].id !=
                      second->items[second_index].id);
            }
        }
    }

    Db_freeResult(first);
    Db_freeResult(second);
    stop_test();
}

TEST(file_pages_keep_case_ties_and_deleted_tokens) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute(
        "INSERT INTO dirs (id, parent_id, path, filename) "
        "VALUES (101, " ROOT_DIR_SQL ", 'Tie', 'Tie')"));
    CHECK(Db_execute(
        "INSERT INTO files (parent_id, filename, type) VALUES "
        "(101, 'A.mp3', 'music'), (101, 'a.mp3', 'music'), (101, 'B.mp3', 'music')"));

    DbFilesResult* first = Db_getFiles(101, DB_FILE_TYPE_MUSIC, false, 1, 0);
    CHECK(first && first->count == 1);
    CHECK(first && first->has_more);
    int token = first && first->count > 0 ? first->items[0].id : 0;
    DbFilesResult* second = Db_getFiles(101, DB_FILE_TYPE_MUSIC, false, 1, token);
    CHECK(second && second->count == 1);
    CHECK(second && second->items && strcmp(second->items[0].filename, "a.mp3") == 0);

    CHECK(Db_execute("DELETE FROM files WHERE id = 1"));
    DbFilesResult* deleted_token = Db_getFiles(101, DB_FILE_TYPE_MUSIC, false, 1, token);
    CHECK(deleted_token && deleted_token->count == 0);
    CHECK(deleted_token && !deleted_token->has_more);

    Db_freeResult(first);
    Db_freeResult(second);
    Db_freeResult(deleted_token);
    stop_test();
}

TEST(file_pages_match_exact_subtrees) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute(
        "INSERT INTO dirs (id, parent_id, path, filename) VALUES "
        "(101, " ROOT_DIR_SQL ", '50%_off', '50%_off'),"
        "(102, 101, '50%_off/Child', 'Child'),"
        "(103, " ROOT_DIR_SQL ", 'Albums', 'Albums'),"
        "(104, " ROOT_DIR_SQL ", 'albums', 'albums'),"
        "(105, " ROOT_DIR_SQL ", 'Empty', 'Empty')"));
    CHECK(Db_execute(
        "INSERT INTO files (parent_id, filename, type) VALUES "
        "(101, 'percent.mp3', 'music'),"
        "(102, 'child.mp3', 'music'),"
        "(103, 'upper.mp3', 'music'),"
        "(104, 'lower.mp3', 'music'),"
        "(" ROOT_DIR_SQL ", 'song.mp3', 'music'),"
        "(" ROOT_DIR_SQL ", 'notes.txt', 'unknown')"));

    CHECK_EQ_INT(Db_countFiles(101, DB_FILE_TYPE_MUSIC, true), 2);
    CHECK_EQ_INT(Db_countFiles(103, DB_FILE_TYPE_ALL, true), 1);
    CHECK_EQ_INT(Db_countFiles(105, DB_FILE_TYPE_ALL, true), 0);
    CHECK_EQ_INT(Db_countFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, true), 5);
    CHECK_EQ_INT(Db_countFiles(99999, DB_FILE_TYPE_ALL, true), 0);

    DbFilesResult* special = Db_getFiles(101, DB_FILE_TYPE_MUSIC, true, 10, 0);
    CHECK(special && special->count == 2);
    CHECK(special && special->items && strcmp(special->items[0].filename, "percent.mp3") == 0);
    CHECK(special && special->items && strcmp(special->items[1].filename, "child.mp3") == 0);

    DbFilesResult* albums = Db_getFiles(103, DB_FILE_TYPE_MUSIC, true, 10, 0);
    CHECK(albums && albums->count == 1);
    CHECK(albums && albums->items && strcmp(albums->items[0].filename, "upper.mp3") == 0);

    DbFilesResult* empty = Db_getFiles(105, DB_FILE_TYPE_MUSIC, true, 1, 0);
    CHECK(empty && empty->count == 0);
    CHECK(empty && !empty->has_more);

    DbFilesResult* whole_tree = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, true, 10, 0);
    CHECK(whole_tree && whole_tree->count == 5);

    DbFilesResult* root_files = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, false, 10, 0);
    CHECK(root_files && root_files->count == 1);
    CHECK(root_files && root_files->items &&
          strcmp(root_files->items[0].filename, "song.mp3") == 0);

    DbFilesResult* root_all = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_ALL, false, 10, 0);
    CHECK(root_all && root_all->count == 2);
    CHECK(root_all && root_all->items &&
          strcmp(root_all->items[0].filename, "notes.txt") == 0);
    DbFilesResult* root_unknown = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_UNKNOWN, true, 10,
                                              0);
    CHECK(root_unknown && root_unknown->count == 1);
    Db_freeResult(root_all);
    Db_freeResult(root_unknown);

    DbFileResult* root_file = Db_getFileByPath("song.mp3");
    CHECK(root_file);
    CHECK(root_file && root_file->file.parent_id == Db_getRootDirId());
    CHECK(root_file && strcmp(root_file->file.path, "song.mp3") == 0);
    CHECK(root_file && strcmp(root_file->file.dir_path, "") == 0);

    Db_freeResult(special);
    Db_freeResult(albums);
    Db_freeResult(empty);
    Db_freeResult(whole_tree);
    Db_freeResult(root_files);
    Db_freeResult(root_file);
    stop_test();
}

// Adds a file row, and sets its type as a file scan does. Returns its id, or 0 on error.
static int add_file_row(int parent_id, const char* filename, DbFileType type) {
    int id = Db_addFile(parent_id, filename);
    bool typed = type == DB_FILE_TYPE_UNKNOWN || Db_updateFileType(id, type);
    return id > 0 && typed ? id : 0;
}

TEST(listing_reads_leave_data_version_and_nest_in_transactions) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    int child_id = 0;
    CHECK((child_id = Db_addDir(Db_getRootDirId(), "Child", "Child")) > 0);
    CHECK(add_file_row(Db_getRootDirId(), "song.mp3", DB_FILE_TYPE_MUSIC) > 0);
    CHECK(Db_getOrCreatePlaylist("mix.m3u") > 0);

    int version = Db_dataVersion();
    DbReadDirResult* contents = Db_readDir(Db_getRootDirId());
    DbPlaylistsResult* playlists = Db_readPlaylists();
    CHECK(contents && contents->dir_count == 1 && contents->file_count == 1 &&
          contents->count == 2);
    CHECK(playlists && playlists->count == 1);
    CHECK_EQ_INT(Db_dataVersion(), version);
    Db_freeResult(contents);
    Db_freeResult(playlists);

    CHECK(Db_begin());
    DbReadDirResult* nested = Db_readDir(Db_getRootDirId());
    CHECK(nested && nested->dir_count == 1 && nested->file_count == 1);
    Db_freeResult(nested);
    CHECK(add_file_row(child_id, "other.mp3", DB_FILE_TYPE_MUSIC) > 0);
    CHECK(Db_commit());
    DbFileResult* other = Db_getFileByPath("Child/other.mp3");
    CHECK(other);
    Db_freeResult(other);
    stop_test();
}

TEST(file_pages_walk_directories_depth_first) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    // "Albums B" sorts before "Albums/Blue" by path, but after the whole "Albums" tree
    // in a depth-first walk.
    CHECK(Db_execute(
        "INSERT INTO dirs (id, parent_id, path, filename) VALUES "
        "(101, " ROOT_DIR_SQL ", 'Albums', 'Albums'),"
        "(102, 101, 'Albums/Blue', 'Blue'),"
        "(103, " ROOT_DIR_SQL ", 'Albums B', 'Albums B')"));
    CHECK(Db_execute(
        "INSERT INTO files (parent_id, filename, type) VALUES "
        "(" ROOT_DIR_SQL ", 'root.mp3', 'music'),"
        "(101, 'albums.mp3', 'music'),"
        "(102, 'blue.mp3', 'music'),"
        "(102, 'notes.txt', 'other'),"
        "(103, 'b.mp3', 'music')"));

    DbFilesResult* all = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, true, 0, 0);
    CHECK(all && all->count == 4 && !all->has_more);
    const char* expected[] = {"root.mp3", "albums.mp3", "blue.mp3", "b.mp3"};
    for (int i = 0; all && i < all->count && i < 4; i++) {
        CHECK(strcmp(all->items[i].filename, expected[i]) == 0);
    }

    DbFilesResult* first = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, true, 2, 0);
    CHECK(first && first->count == 2 && first->has_more);
    int token = first && first->count == 2 ? first->items[1].id : 0;
    DbFilesResult* rest = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, true, 0, token);
    CHECK(rest && rest->count == 2 && !rest->has_more);
    CHECK(rest && rest->count == 2 && strcmp(rest->items[0].filename, "blue.mp3") == 0);

    Db_freeResult(all);
    Db_freeResult(first);
    Db_freeResult(rest);
    stop_test();
}

TEST(failed_listing_count_returns_no_result) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_addDir(Db_getRootDirId(), "Child", "Child") > 0);
    // The subdirectory count succeeds and the file count fails.
    CHECK(Db_execute("ALTER TABLE files RENAME TO files_moved"));
    DbReadDirResult* contents = Db_readDir(Db_getRootDirId());
    CHECK(contents == NULL);
    Db_freeResult(contents);
    stop_test();
}

TEST(settings_read_skips_unknown_types) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute("DELETE FROM settings"));
    CHECK(Db_execute("INSERT INTO settings (name, type, value) VALUES "
                     "('odd', 'other', 1), ('kept', 'int', 3)"));
    DbSettingsResult* settings = Db_readSettings();
    CHECK(settings && settings->count == 1);
    CHECK(settings && settings->count == 1 && strcmp(settings->items[0].name, "kept") == 0 &&
          settings->items[0].type == DB_SETTING_INT && settings->items[0].int_value == 3);
    Db_freeResult(settings);
    stop_test();
}

TEST(missing_rows_read_as_absent) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    DbDirResult* dir = Db_getDir(99999);
    DbDirResult* dir_by_path = Db_getDirByPath("Missing");
    DbFileResult* file = Db_getFile(99999);
    DbFileResult* file_by_path = Db_getFileByPath("Missing/track.mp3");
    DbFileResult* file_in_dir = Db_getFileInDir(Db_getRootDirId(), "track.mp3");
    DbPlaylistResult* playlist = Db_getPlaylist(99999);
    DbLastPlayedResult* last_played = Db_readLastPlayed();
    CHECK(!dir);
    CHECK(!dir_by_path);
    CHECK(!file);
    CHECK(!file_by_path);
    CHECK(!file_in_dir);
    CHECK(!playlist);
    CHECK(!last_played);
    Db_freeResult(dir);
    Db_freeResult(dir_by_path);
    Db_freeResult(file);
    Db_freeResult(file_by_path);
    Db_freeResult(file_in_dir);
    Db_freeResult(playlist);
    Db_freeResult(last_played);

    CHECK(Db_execute(
        "INSERT INTO last_played (type, id1, id2, position, is_last) "
        "VALUES ('radio', 1, 1, 0, 'true')"));
    DbLastPlayedResult* unknown = Db_readLastPlayed();
    CHECK(!unknown);
    Db_freeResult(unknown);
    stop_test();
}

TEST(index_writes_and_silent_position_update) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    int dir_id = 0;
    int child_id = 0;
    int file_id = 0;
    int playlist_id = 0;
    CHECK((dir_id = Db_addDir(Db_getRootDirId(), "Writes", "Writes")) > 0);
    CHECK((child_id = Db_addDir(dir_id, "Writes/Child", "Child")) > 0);
    CHECK((file_id = add_file_row(child_id, "track.mp3", DB_FILE_TYPE_MUSIC)) > 0);
    CHECK((playlist_id = Db_getOrCreatePlaylist("writes.m3u")) > 0);
    CHECK_EQ_INT(Db_getOrCreatePlaylist("writes.m3u"), playlist_id);
    CHECK(!Db_updatePlaylist(0, 4));
    CHECK(Db_updatePlaylist(playlist_id, 4));

    DbFileResult* updated_file = Db_getFile(file_id);
    CHECK(updated_file);
    CHECK(updated_file &&
          updated_file->file.type == DB_FILE_TYPE_MUSIC);

    CHECK_EQ_INT(Db_countFiles(child_id, DB_FILE_TYPE_MUSIC, false), 1);
    DbPlaylistResult* updated_playlist = Db_getPlaylist(playlist_id);
    CHECK(updated_playlist &&
          updated_playlist->playlist.num_entries == 4);

    CHECK(Db_execute(
        "INSERT INTO last_played (type, id1, id2, position, is_last) "
        "VALUES ('playlist', 0, 0, 0, 'true')"));
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, dir_id, file_id, "Title", 0));
    CHECK_EQ_INT(sqlite_last_played_flag(database_path, "playlist"), 0);
    CHECK_EQ_INT(sqlite_last_played_flag(database_path, "folder"), 1);
    DbLastPlayedResult* saved = Db_readLastPlayed();
    CHECK(saved != NULL);
    CHECK(saved && strcmp(saved->last_played.track_name, "Title") == 0);
    int version = Db_dataVersion();
    CHECK(Db_saveLastPlayedPosition(12345));
    CHECK_EQ_INT(Db_dataVersion(), version);
    DbLastPlayedResult* positioned = Db_readLastPlayed();
    CHECK(positioned && positioned->last_played.position == 12345);
    CHECK(Db_clearLastPlayed());
    DbLastPlayedResult* dismissed = Db_readLastPlayed();
    CHECK(!dismissed);

    CHECK(Db_deletePlaylist(playlist_id));
    DbPlaylistResult* missing_playlist = Db_getPlaylist(playlist_id);
    CHECK(!missing_playlist);
    CHECK(Db_deleteDir(dir_id));
    DbDirResult* missing_dir = Db_getDir(child_id);
    DbFileResult* missing_file = Db_getFile(file_id);
    CHECK(!missing_dir);
    CHECK(!missing_file);

    Db_freeResult(updated_file);
    Db_freeResult(updated_playlist);
    Db_freeResult(saved);
    Db_freeResult(positioned);
    Db_freeResult(dismissed);
    Db_freeResult(missing_playlist);
    Db_freeResult(missing_dir);
    Db_freeResult(missing_file);
    stop_test();
}

TEST(execute_sql) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    int before = Db_dataVersion();
    CHECK(Db_execute("CREATE TABLE execute_values (value TEXT)"));
    CHECK(Db_execute("INSERT INTO execute_values (value) VALUES ('execute')"));
    CHECK_EQ_INT(Db_dataVersion(), before + 2);
    CHECK(!Db_execute("INSERT INTO missing_table (name) VALUES ('execute')"));

    stop_test();
}

TEST(statement_wrapper_reports_state) {
    sqlite3* database = NULL;
    CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
    CHECK(database != NULL);
    CHECK(sqlite3_exec(database, "CREATE TABLE test_values (value TEXT)", NULL, NULL, NULL) ==
          SQLITE_OK);

    DbStatement statement;
    char source[sizeof("changed")] = "saved";
    CHECK(DbStatement_begin(&statement, database, "SELECT ?"));
    CHECK(DbStatement_bind_text(&statement, 1, source));
    strcpy(source, "changed");
    CHECK(DbStatement_step(&statement));
    const char* bound = DbStatement_get_text(&statement, 0);
    CHECK(bound && strcmp(bound, "saved") == 0);
    CHECK(DbStatement_close(&statement));

    CHECK(DbStatement_begin(&statement, database, "SELECT ?"));
    CHECK(DbStatement_bind_text(&statement, 1, "copied"));
    CHECK(DbStatement_step(&statement));
    char* copy = DbStatement_dup_text(&statement, 0);
    CHECK(DbStatement_close(&statement));
    CHECK(copy && strcmp(copy, "copied") == 0);
    free(copy);

    CHECK(DbStatement_begin(&statement, database, "SELECT value FROM test_values WHERE value = ?"));
    CHECK(DbStatement_bind_text(&statement, 1, "missing"));
    CHECK(!DbStatement_step(&statement));
    CHECK(statement.ok);
    CHECK(DbStatement_close(&statement));
    CHECK(statement.statement == NULL);
    CHECK(DbStatement_close(&statement));

    CHECK(!DbStatement_begin(&statement, database, "SELECT value FROM missing"));
    CHECK(!DbStatement_step(&statement));
    CHECK(!statement.ok);
    CHECK(!DbStatement_close(&statement));
    CHECK(statement.statement == NULL);
    CHECK(statement.errmsg[0] != '\0');

    CHECK(DbStatement_begin(&statement, database, "SELECT ?, ?, ?"));
    CHECK(DbStatement_bind(&statement, 1, INT_ARG(42)));
    CHECK(DbStatement_bind(&statement, 2, TEXT_ARG("bound")));
    CHECK(DbStatement_bind(&statement, 3, NULL_ARG));
    CHECK(DbStatement_step(&statement));
    CHECK_EQ_INT(DbStatement_get_int(&statement, 0), 42);
    CHECK(strcmp(DbStatement_get_text(&statement, 1), "bound") == 0);
    CHECK(DbStatement_get_text_or_null(&statement, 2) == NULL);
    CHECK(DbStatement_close(&statement));

    CHECK(DbStatement_begin(&statement, database, "SELECT NULL, NULL, 'text', 7"));
    CHECK(DbStatement_step(&statement));
    CHECK(DbStatement_get_text_or_null(&statement, 0) == NULL);
    char* no_copy = DbStatement_dup_text_or_null(&statement, 0);
    CHECK(no_copy == NULL);
    CHECK_EQ_INT(DbStatement_get_int_or(&statement, 1, -5), -5);
    CHECK(strcmp(DbStatement_get_text(&statement, 2), "text") == 0);
    CHECK_EQ_INT(DbStatement_get_int(&statement, 3), 7);
    CHECK(statement.ok);
    CHECK(DbStatement_close(&statement));

    CHECK(DbStatement_begin(&statement, database, "SELECT NULL"));
    CHECK(DbStatement_step(&statement));
    CHECK(DbStatement_get_text(&statement, 0) == NULL);
    CHECK(!statement.ok);
    CHECK(!DbStatement_close(&statement));

    CHECK(DbStatement_begin(&statement, database, "SELECT NULL"));
    CHECK(DbStatement_step(&statement));
    CHECK_EQ_INT(DbStatement_get_int(&statement, 0), 0);
    CHECK(!DbStatement_close(&statement));

    CHECK(DbStatement_exec(&statement, database, "INSERT INTO test_values (value) VALUES (?)",
                           TEXT_ARG("row")));
    CHECK(statement.last_insert_id > 0);
    CHECK_EQ_INT(statement.last_insert_id, (int)sqlite3_last_insert_rowid(database));
    CHECK_EQ_INT(DbStatement_query_int(&statement, database,
                                       "SELECT COUNT(*) FROM test_values WHERE value = ?",
                                       TEXT_ARG("row")), 1);
    CHECK(statement.ok);
    CHECK_EQ_INT(DbStatement_query_int(&statement, database,
                                       "SELECT 5 FROM test_values WHERE value = ?",
                                       TEXT_ARG("missing")), 0);
    CHECK(!statement.ok);
    CHECK_EQ_INT(DbStatement_query_int(&statement, database, "SELECT NULL"), 0);
    CHECK(!statement.ok);
    CHECK_EQ_INT(DbStatement_query_int_or(&statement, database, -1,
                                          "SELECT 5 FROM test_values WHERE value = ?",
                                          TEXT_ARG("missing")), -1);
    CHECK(statement.ok);
    CHECK_EQ_INT(DbStatement_query_int_or(&statement, database, -1, "SELECT NULL"), -1);
    CHECK(statement.ok);
    CHECK_EQ_INT(DbStatement_query_int_or(&statement, database, -1, "SELECT 7"), 7);
    CHECK_EQ_INT(DbStatement_query_int_or(&statement, database, -1, "SELECT x FROM missing"),
                 -1);
    CHECK(!statement.ok);

    // The error names the operation that failed, and later operations keep it.
    CHECK(DbStatement_begin(&statement, database, "SELECT ?"));
    CHECK(!DbStatement_bind(&statement, 5, INT_ARG(1)));
    CHECK(!DbStatement_step(&statement));
    CHECK(!DbStatement_close(&statement));
    CHECK(strncmp(statement.errmsg, "bind 5: ", 8) == 0);

    sqlite3_close(database);
}

TEST(result_is_a_copy_and_tracks_commits) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute("DELETE FROM settings"));
    CHECK(Db_saveStringSetting("key", "old"));

    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->count == 1);
    CHECK(result && result->items[0].string_value &&
          strcmp(result->items[0].string_value, "old") == 0);
    CHECK(result && Db_resultIsCurrent(&result->base));

    CHECK(Db_saveStringSetting("key", "new"));
    CHECK(result && result->items[0].string_value &&
          strcmp(result->items[0].string_value, "old") == 0);
    CHECK(result && !Db_resultIsCurrent(&result->base));
    Db_freeResult(result);
    stop_test();
}

TEST(rollback_keeps_data_version_increment) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute("DELETE FROM settings"));
    int before = Db_dataVersion();
    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);

    CHECK(Db_begin());
    CHECK(Db_saveStringSetting("key", "rolled-back"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);
    CHECK(Db_rollback());
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);
    CHECK(result && !Db_resultIsCurrent(&result->base));
    Db_freeResult(result);

    DbSettingsResult* missing = Db_readSettings();
    CHECK(missing != NULL);
    CHECK(missing && missing->count == 0);
    Db_freeResult(missing);
    stop_test();
}

TEST(writes_in_one_transaction_change_version_per_write) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    int before = Db_dataVersion();
    CHECK(Db_saveStringSetting("single", "write"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);

    before = Db_dataVersion();
    CHECK(Db_begin());
    CHECK(Db_saveIntSetting("one", 1));
    CHECK(Db_saveIntSetting("two", 2));
    CHECK(Db_saveIntSetting("three", 3));
    CHECK_EQ_SZ(Db_dataVersion(), before + 3);
    CHECK(Db_commit());
    CHECK_EQ_SZ(Db_dataVersion(), before + 4);
    stop_test();
}

static pthread_barrier_t worker_ready;
static pthread_barrier_t worker_release;
static bool worker_ok;
static bool settings_thread_ok;

static void* transaction_worker(void* unused) {
    (void)unused;
    worker_ok = Db_begin() && Db_saveStringSetting("worker", "value");
    pthread_barrier_wait(&worker_ready);
    pthread_barrier_wait(&worker_release);
    if (worker_ok) worker_ok = Db_commit();
    return NULL;
}

static void* settings_worker(void* unused) {
    (void)unused;
    settings_thread_ok = Db_saveStringSetting("thread", "value");
    return NULL;
}

TEST(thread_connections_isolate_uncommitted_rows) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(pthread_barrier_init(&worker_ready, NULL, 2) == 0);
    CHECK(pthread_barrier_init(&worker_release, NULL, 2) == 0);

    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, transaction_worker, NULL) == 0);
    pthread_barrier_wait(&worker_ready);

    struct timespec started;
    struct timespec finished;
    clock_gettime(CLOCK_MONOTONIC, &started);
    DbSettingsResult* pending = Db_readSettings();
    CHECK(pending != NULL);
    CHECK(pending && pending->count == 0);
    Db_freeResult(pending);
    clock_gettime(CLOCK_MONOTONIC, &finished);
    double elapsed = (double)(finished.tv_sec - started.tv_sec) +
                     (double)(finished.tv_nsec - started.tv_nsec) / 1000000000.0;
    CHECK(elapsed < 1.0);

    pthread_barrier_wait(&worker_release);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(worker_ok);
    DbSettingsResult* committed = Db_readSettings();
    CHECK(committed != NULL);
    CHECK(committed && committed->count == 1);
    CHECK(committed && strcmp(committed->items[0].name, "worker") == 0);
    Db_freeResult(committed);
    CHECK(pthread_barrier_destroy(&worker_ready) == 0);
    CHECK(pthread_barrier_destroy(&worker_release) == 0);
    stop_test();
}

TEST(thread_connection_shares_settings) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute("DELETE FROM settings"));

    pthread_t worker;
    settings_thread_ok = false;
    CHECK(pthread_create(&worker, NULL, settings_worker, NULL) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(settings_thread_ok);
    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->count == 1);
    CHECK(result && result->items[0].string_value &&
          strcmp(result->items[0].string_value, "value") == 0);
    Db_freeResult(result);
    stop_test();
}

int main(void) {
    RUN(fresh_database_gets_schema);
    RUN(settings_file_migration_runs_ordered_actions);
    RUN(failed_settings_migration_keeps_no_partial_rows);
    RUN(open_failure_disables_database);
    RUN(newer_database_is_not_changed);
    RUN(failed_schema_migration_rolls_back);
    RUN(resume_file_is_removed_without_copying);
    RUN(settings_save_read_and_reset);
    RUN(result_frees_through_shared_header);
    RUN(index_reads_copy_rows);
    RUN(file_pages_use_keyset_tokens);
    RUN(file_pages_keep_case_ties_and_deleted_tokens);
    RUN(file_pages_match_exact_subtrees);
    RUN(listing_reads_leave_data_version_and_nest_in_transactions);
    RUN(file_pages_walk_directories_depth_first);
    RUN(failed_listing_count_returns_no_result);
    RUN(settings_read_skips_unknown_types);
    RUN(missing_rows_read_as_absent);
    RUN(index_writes_and_silent_position_update);
    RUN(execute_sql);
    RUN(statement_wrapper_reports_state);
    RUN(result_is_a_copy_and_tracks_commits);
    RUN(rollback_keeps_data_version_increment);
    RUN(writes_in_one_transaction_change_version_per_write);
    RUN(thread_connections_isolate_uncommitted_rows);
    RUN(thread_connection_shares_settings);
    return test_summary();
}
