#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

static bool sqlite_has_data_migrations(const char* path) {
    sqlite3* database = NULL;
    sqlite3_stmt* statement = NULL;
    bool exists = false;

    if (sqlite3_open(path, &database) == SQLITE_OK &&
        sqlite3_prepare_v2(
            database,
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'data_migrations'",
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
    CHECK_EQ_INT(sqlite_user_version(database_path), 1);
    CHECK(sqlite_has_data_migrations(database_path));
    stop_test();
}

TEST(open_failure_disables_database) {
    CHECK(start_test());
    char missing_path[sizeof(database_path)];
    snprintf(missing_path, sizeof(missing_path), "%s/missing/music-player.db", temp_dir);
    CHECK(!Db_initInternal(missing_path));
    CHECK(Db_scratchRead("missing") == NULL);
    stop_test();
}

TEST(newer_database_is_not_changed) {
    CHECK(start_test());
    CHECK(sqlite_exec(database_path, "PRAGMA user_version = 99"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 99);
    CHECK(!sqlite_has_data_migrations(database_path));
    stop_test();
}

TEST(failed_schema_migration_rolls_back) {
    CHECK(start_test());
    CHECK(sqlite_exec(database_path, "CREATE TABLE data_migrations (wrong INTEGER)"));
    CHECK(!Db_initInternal(database_path));
    CHECK_EQ_INT(sqlite_user_version(database_path), 0);
    stop_test();
}

TEST(scratch_save_read_and_reset) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(Db_scratchSave("key", "value"));

    DbScratchResult* saved = Db_scratchRead("key");
    CHECK(saved != NULL);
    CHECK(saved && saved->value && strcmp(saved->value, "value") == 0);
    Db_freeResult(saved);

    DbScratchResult* missing = Db_scratchRead("never-saved");
    CHECK(missing != NULL);
    CHECK(missing && missing->value == NULL);
    Db_freeResult(missing);

    Db_quit();
    CHECK(Db_initInternal(database_path));
    DbScratchResult* reset = Db_scratchRead("key");
    CHECK(reset != NULL);
    CHECK(reset && reset->value == NULL);
    Db_freeResult(reset);
    stop_test();
}

TEST(execute_sql) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    int before = Db_dataVersion();
    CHECK(Db_execute(
        "INSERT INTO data_migrations (name) VALUES ('execute-one'); "
        "INSERT INTO data_migrations (name) VALUES ('execute-two')"));
    CHECK_EQ_INT(Db_dataVersion(), before + 1);
    CHECK(Db_dataMigrationIsDone("execute-one"));
    CHECK(Db_dataMigrationIsDone("execute-two"));
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
    CHECK(Db_scratchSave("key", "old"));

    DbScratchResult* result = Db_scratchRead("key");
    CHECK(result != NULL);
    CHECK(result && result->value && strcmp(result->value, "old") == 0);
    CHECK(result && Db_resultIsCurrent(result));

    CHECK(Db_scratchSave("key", "new"));
    CHECK(result && result->value && strcmp(result->value, "old") == 0);
    CHECK(result && !Db_resultIsCurrent(result));
    Db_freeResult(result);
    stop_test();
}

TEST(rollback_keeps_data_version_increment) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    int before = Db_dataVersion();
    DbScratchResult* result = Db_scratchRead("key");
    CHECK(result != NULL);

    CHECK(Db_begin());
    CHECK(Db_scratchSave("key", "rolled-back"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);
    CHECK(Db_rollback());
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);
    CHECK(result && !Db_resultIsCurrent(result));
    Db_freeResult(result);

    DbScratchResult* missing = Db_scratchRead("key");
    CHECK(missing != NULL);
    CHECK(missing && missing->value == NULL);
    Db_freeResult(missing);
    stop_test();
}

TEST(writes_in_one_transaction_change_version_per_write) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    int before = Db_dataVersion();
    CHECK(Db_scratchSave("single", "write"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);

    before = Db_dataVersion();
    CHECK(Db_begin());
    CHECK(Db_scratchSave("one", "1"));
    CHECK(Db_scratchSave("two", "2"));
    CHECK(Db_scratchSave("three", "3"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 3);
    CHECK(Db_commit());
    CHECK_EQ_SZ(Db_dataVersion(), before + 4);
    stop_test();
}

TEST(data_migration_mark_is_atomic) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));
    CHECK(!Db_dataMigrationIsDone("settings.to-db"));

    int before = Db_dataVersion();
    CHECK(Db_begin());
    CHECK(Db_markDataMigrationDone("settings.to-db"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);
    CHECK(Db_dataMigrationIsDone("settings.to-db"));
    CHECK(Db_rollback());
    CHECK_EQ_SZ(Db_dataVersion(), before + 1);
    CHECK(!Db_dataMigrationIsDone("settings.to-db"));

    CHECK(Db_begin());
    CHECK(Db_markDataMigrationDone("settings.to-db"));
    CHECK_EQ_SZ(Db_dataVersion(), before + 2);
    CHECK(Db_commit());
    CHECK_EQ_SZ(Db_dataVersion(), before + 3);
    CHECK(Db_dataMigrationIsDone("settings.to-db"));
    stop_test();
}

static pthread_barrier_t worker_ready;
static pthread_barrier_t worker_release;
static bool worker_ok;
static bool scratch_thread_ok;

static void* transaction_worker(void* unused) {
    (void)unused;
    worker_ok = Db_begin() && Db_markDataMigrationDone("worker");
    pthread_barrier_wait(&worker_ready);
    pthread_barrier_wait(&worker_release);
    if (worker_ok) worker_ok = Db_commit();
    return NULL;
}

static void* scratch_worker(void* unused) {
    (void)unused;
    scratch_thread_ok = Db_scratchSave("thread", "value");
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
    CHECK(!Db_dataMigrationIsDone("worker"));
    clock_gettime(CLOCK_MONOTONIC, &finished);
    double elapsed = (double)(finished.tv_sec - started.tv_sec) +
                     (double)(finished.tv_nsec - started.tv_nsec) / 1000000000.0;
    CHECK(elapsed < 1.0);

    pthread_barrier_wait(&worker_release);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(worker_ok);
    CHECK(Db_dataMigrationIsDone("worker"));
    CHECK(pthread_barrier_destroy(&worker_ready) == 0);
    CHECK(pthread_barrier_destroy(&worker_release) == 0);
    stop_test();
}

TEST(thread_connection_does_not_share_scratch) {
    CHECK(start_test());
    CHECK(Db_initInternal(database_path));

    pthread_t worker;
    scratch_thread_ok = false;
    CHECK(pthread_create(&worker, NULL, scratch_worker, NULL) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(scratch_thread_ok);
    DbScratchResult* result = Db_scratchRead("thread");
    CHECK(result != NULL);
    CHECK(result && result->value == NULL);
    Db_freeResult(result);
    stop_test();
}

int main(void) {
    RUN(fresh_database_gets_schema);
    RUN(open_failure_disables_database);
    RUN(newer_database_is_not_changed);
    RUN(failed_schema_migration_rolls_back);
    RUN(scratch_save_read_and_reset);
    RUN(execute_sql);
    RUN(statement_wrapper_reports_state);
    RUN(result_is_a_copy_and_tracks_commits);
    RUN(rollback_keeps_data_version_increment);
    RUN(writes_in_one_transaction_change_version_per_write);
    RUN(data_migration_mark_is_atomic);
    RUN(thread_connections_isolate_uncommitted_rows);
    RUN(thread_connection_does_not_share_scratch);
    return test_summary();
}
