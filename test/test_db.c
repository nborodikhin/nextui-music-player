#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#include "db.h"
#include "db_statement.h"
#include "file_utils.h"
#include "test.h"

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

static bool sqlite_has_settings(const char* path) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    bool exists = false;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(
            database,
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'settings'",
            -1, &statement, NULL) == SQLITE_OK) {
        exists = sqlite3_step(statement) == SQLITE_ROW;
    }
    if (statement) sqlite3_finalize(statement);
    if (database) sqlite3_close(database);
    return exists;
}

TEST(fresh_database_gets_schema) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_isAvailable());
    CHECK_EQ_INT(sqlite_user_version(database_path), 5);
    CHECK(sqlite_has_settings(database_path));
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
                      "CREATE TABLE settings (name TEXT PRIMARY KEY, type TEXT NOT NULL, value);"
                      "PRAGMA user_version = 1"));
    CHECK(Db_initInternal(database_path));

    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->count == 5);
    Db_freeSettingsResult(result);
    CHECK(access(settings_path, F_OK) != 0);
    CHECK_EQ_INT(sqlite_user_version(database_path), 5);

    Db_quit();
    CHECK(Db_initInternal(database_path));
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
    Db_freeSettingsResult(result);
    stop_test();
}

TEST(newer_database_is_not_changed) {
    CHECK(start_test());
    CHECK(sqlite_exec(database_path, "PRAGMA user_version = 99"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 99);
    CHECK(!sqlite_has_settings(database_path));
    stop_test();
}

TEST(failed_schema_migration_rolls_back) {
    CHECK(start_test());
    CHECK(sqlite_exec(database_path, "CREATE TABLE settings (wrong INTEGER)"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 0);
    CHECK(sqlite_exec(database_path, "DROP TABLE settings"));
    CHECK(Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 5);
    stop_test();
}

TEST(settings_save_read_and_reset) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_execute("DELETE FROM settings"));
    CHECK(Db_saveIntSetting("int", 42));
    CHECK(Db_saveBoolSetting("bool", true));
    CHECK(Db_saveStringSetting("string", "value"));

    DbSettingsResult* saved = Db_readSettings();
    CHECK(saved != NULL);
    CHECK(saved && saved->count == 3);
    CHECK(saved && saved->items[0].name != NULL);
    Db_freeSettingsResult(saved);

    Db_quit();
    CHECK(Db_initInternal(database_path));
    DbSettingsResult* restored = Db_readSettings();
    CHECK(restored != NULL);
    CHECK(restored && restored->count == 3);
    Db_freeSettingsResult(restored);
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
    char* copy = DbStatement_dup_ext(&statement, 0);
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
    Db_freeSettingsResult(result);
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
    Db_freeSettingsResult(result);

    DbSettingsResult* missing = Db_readSettings();
    CHECK(missing != NULL);
    CHECK(missing && missing->count == 0);
    Db_freeSettingsResult(missing);
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
    Db_freeSettingsResult(pending);
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
    Db_freeSettingsResult(committed);
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
    Db_freeSettingsResult(result);
    stop_test();
}

int main(void) {
    RUN(fresh_database_gets_schema);
    RUN(settings_file_migration_runs_ordered_actions);
    RUN(open_failure_disables_database);
    RUN(newer_database_is_not_changed);
    RUN(failed_schema_migration_rolls_back);
    RUN(settings_save_read_and_reset);
    RUN(execute_sql);
    RUN(statement_wrapper_reports_state);
    RUN(result_is_a_copy_and_tracks_commits);
    RUN(rollback_keeps_data_version_increment);
    RUN(writes_in_one_transaction_change_version_per_write);
    RUN(thread_connections_isolate_uncommitted_rows);
    RUN(thread_connection_shares_settings);
    return test_summary();
}
