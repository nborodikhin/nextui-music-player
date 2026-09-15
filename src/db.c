#include "db.h"

#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "file_utils.h"
#include "db_schema.h"
#include "db_statement.h"

static pthread_key_t connection_key;
static pthread_once_t connection_key_once = PTHREAD_ONCE_INIT;
static bool connection_key_ready;
static char* database_path;

static volatile int database_available;
static volatile int data_version;
static volatile int open_connection_count;

static int atomic_load(volatile int *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static void atomic_store(volatile int *ptr, int value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

static int atomic_increment(volatile int *ptr) {
    return __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

static int atomic_decrement(volatile int *ptr) {
    return __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

static void increment_data_version(void) {
    atomic_increment(&data_version);
}

static void connection_destroy(void* value) {
    sqlite3* database = value;
    if (!database) return;

    int result = sqlite3_close(database);
    if (result != SQLITE_OK) {
        LOG_error("[Db] failed to close connection: %s\n", sqlite3_errstr(result));
    }
    atomic_decrement(&open_connection_count);
}

static void create_connection_key(void) {
    int result = pthread_key_create(&connection_key, connection_destroy);
    if (result != 0) {
        LOG_error("[Db] failed to create connection key: %s\n", strerror(result));
        return;
    }
    connection_key_ready = true;
}

static bool ensure_connection_key(void) {
    int result = pthread_once(&connection_key_once, create_connection_key);
    if (result != 0 || !connection_key_ready) {
        if (result != 0) {
            LOG_error("[Db] failed to initialize connection key: %s\n", strerror(result));
        }
        return false;
    }
    return true;
}

static sqlite3* connection(void) {
    if (!atomic_load(&database_available)) return NULL;
    if (!ensure_connection_key()) return NULL;

    sqlite3* database = pthread_getspecific(connection_key);
    if (database) return database;

    int result = sqlite3_open(database_path, &database);
    if (result != SQLITE_OK) {
        LOG_error("[Db] failed to open %s: %s\n", database_path,
                  database ? sqlite3_errmsg(database) : sqlite3_errstr(result));
        if (database) sqlite3_close(database);
        return NULL;
    }

    result = sqlite3_busy_timeout(database, 2000);
    if (result != SQLITE_OK) {
        LOG_error("[Db] failed to set busy timeout: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return NULL;
    }

    DbStatement statement;

    bool journal_ok = false;
    DbStatement_begin(&statement, database, "PRAGMA journal_mode=WAL");
    if (DbStatement_step(&statement)) {
        const char* journal_mode = DbStatement_get_text(&statement, 0);
        journal_ok = journal_mode && strcmp(journal_mode, "wal") == 0;
    }
    DbStatement_close(&statement);

    if (!journal_ok || !statement.ok) {
        LOG_error("[Db] failed to enable WAL: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return NULL;
    }

    DbStatement_exec(&statement, database, "PRAGMA wal_autocheckpoint=0");
    if (!statement.ok) {
        LOG_error("[Db] failed to disable automatic checkpoints: %s\n",
                  statement.errmsg);
        sqlite3_close(database);
        return NULL;
    }

    DbStatement_exec(&statement, database, "CREATE TEMP TABLE scratch (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
    if (!statement.ok) {
        LOG_error("[Db] failed to create scratch table: %s\n", statement.errmsg);
        sqlite3_close(database);
        return NULL;
    }

    result = pthread_setspecific(connection_key, database);
    if (result != 0) {
        LOG_error("[Db] failed to store connection: %s\n", strerror(result));
        sqlite3_close(database);
        return NULL;
    }
    atomic_increment(&open_connection_count);
    return database;
}

static void close_current_connection(void) {
    if (!connection_key_ready) return;

    sqlite3* database = pthread_getspecific(connection_key);
    if (!database) return;

    pthread_setspecific(connection_key, NULL);
    connection_destroy(database);
}

static void disable_database(void) {
    atomic_store(&database_available, 0);
    close_current_connection();
}

static bool execute(sqlite3* database, const char* sql) {
    if (!database) {
        LOG_error("[Db] SQL failed: %s: no database\n", sql ? sql : "(null)");
        return false;
    }

    char* error = NULL;
    int result = sqlite3_exec(database, sql, NULL, NULL, &error);
    if (result == SQLITE_OK) {
        sqlite3_free(error);
        return true;
    }

    LOG_error("[Db] SQL failed: %s: %s\n", sql ? sql : "(null)",
              error ? error : sqlite3_errmsg(database));
    sqlite3_free(error);
    return false;
}

static bool rollback_quiet(sqlite3* database) {
    return sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL) == SQLITE_OK;
}

static bool read_schema_version(sqlite3* database, int* version) {
    DbStatement statement;
    DbStatement_begin(&statement, database, "PRAGMA user_version");
    if (DbStatement_step(&statement)) {
        *version = DbStatement_get_int(&statement, 0);
    }
    DbStatement_close(&statement);

    if (!statement.ok) {
        LOG_error("[Db] failed to read schema version, stop %d\n", statement.error_op);
        return false;
    }

    return true;
}

static bool validate_migrations(int* schema_version) {
    int expected_version = 0;
    for (size_t index = 0; index < db_migrations_count; index++) {
        const DbSchemaMigration* migration = &db_migrations[index];
        if (!migration->sql || migration->from_version != expected_version ||
            migration->to_version <= migration->from_version) {
            LOG_error("[Db] invalid schema migration %zu\n", index);
            return false;
        }
        expected_version = migration->to_version;
    }
    *schema_version = expected_version;
    return true;
}

static bool migrate(sqlite3* database) {
    int version = 0;
    if (!read_schema_version(database, &version)) return false;
    int schema_version = 0;
    if (!validate_migrations(&schema_version)) return false;
    if (version < 0 || version > schema_version) {
        LOG_error("[Db] database schema version %d is newer than binary version %d\n",
                  version, schema_version);
        return false;
    }

    for (size_t index = 0;
         index < db_migrations_count && version < schema_version;
         index++) {
        const DbSchemaMigration* migration = &db_migrations[index];
        if (migration->to_version <= version) continue;
        if (migration->from_version != version) {
            LOG_error("[Db] no schema migration from version %d\n", version);
            return false;
        }

        if (!execute(database, "BEGIN")) return false;
        if (!execute(database, migration->sql)) {
            rollback_quiet(database);
            return false;
        }

        char pragma[64];
        snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d",
                 migration->to_version);
        if (!execute(database, pragma)) {
            rollback_quiet(database);
            return false;
        }
        if (!execute(database, "COMMIT")) {
            rollback_quiet(database);
            return false;
        }
        version = migration->to_version;
        increment_data_version();
    }
    return version == schema_version;
}

bool Db_init(void) {
    if (!userdata_mkdir("")) {
        LOG_error("[Db] failed to create the app data directory\n");
        return false;
    }

    char* path = userdata_path("music-player.db");
    if (!path) {
        LOG_error("[Db] failed to allocate the database path\n");
        return false;
    }

    bool result = Db_initInternal(path);
    free(path);
    return result;
}

bool Db_initInternal(const char* path) {
    if (!path || !path[0]) {
        LOG_error("[Db] database path is empty\n");
        return false;
    }

    Db_quit();
    if (!ensure_connection_key()) return false;

    database_path = strdup(path);
    if (!database_path) {
        LOG_error("[Db] failed to allocate the database path\n");
        return false;
    }

    atomic_store(&data_version, 0);
    atomic_store(&database_available, 1);
    if (!connection()) {
        LOG_error("[Db] database is unavailable\n");
        disable_database();
        free(database_path);
        database_path = NULL;
        return false;
    }

    sqlite3* database = pthread_getspecific(connection_key);
    if (!migrate(database)) {
        disable_database();
        free(database_path);
        database_path = NULL;
        return false;
    }
    return true;
}

void Db_quit(void) {
    atomic_store(&database_available, 0);
    close_current_connection();

    int count = atomic_load(&open_connection_count);
    if (count != 0) {
        LOG_error("[Db] %d thread connection(s) remain open\n", count);
    }

    free(database_path);
    database_path = NULL;
}

bool Db_begin(void) {
    return execute(connection(), "BEGIN");
}

bool Db_commit(void) {
    if (!execute(connection(), "COMMIT")) return false;

    increment_data_version();
    return true;
}

bool Db_rollback(void) {
    return execute(connection(), "ROLLBACK");
}

bool Db_execute(const char* sql) {
    if (!sql || !sql[0]) return false;

    if (!execute(connection(), sql)) return false;

    increment_data_version();
    return true;
}

bool Db_dataMigrationIsDone(const char* name) {
    if (!name) return false;

    DbStatement statement;
    DbStatement_begin(&statement, connection(), "SELECT 1 FROM data_migrations WHERE name = ?");
    DbStatement_bind_text(&statement, 1, name);
    bool done = DbStatement_step(&statement);
    DbStatement_close(&statement);

    if (!statement.ok) {
        LOG_error("[Db] failed to read data migration, op %d\n", statement.error_op);
        return false;
    }

    return done;
}

bool Db_markDataMigrationDone(const char* name) {
    if (!name) return false;

    DbStatement statement;
    DbStatement_begin(&statement, connection(), "INSERT OR IGNORE INTO data_migrations (name) VALUES (?)");
    DbStatement_bind_text(&statement, 1, name);
    DbStatement_step(&statement);
    DbStatement_close(&statement);

    if (!statement.ok) {
        LOG_error("[Db] write failed, op %d\n", statement.error_op);
        return false;
    }

    increment_data_version();
    return true;
}

int Db_dataVersion(void) {
    return atomic_load(&data_version);
}

bool Db_resultIsCurrent(const void* result) {
    if (!result) return false;
    const DbResult* base = result;
    return base->data_version == Db_dataVersion();
}

void Db_freeResult(void* result) {
    if (!result) return;
    DbResult* base = result;
    if (base->destroy) base->destroy(base);
}

static void destroy_scratch_result(DbResult* base) {
    DbScratchResult* result = (DbScratchResult*)base;
    free(result->value);
    free(result);
}

bool Db_scratchSave(const char* key, const char* value) {
    if (!key || !value) return false;

    DbStatement statement;
    DbStatement_begin(&statement, connection(), "INSERT OR REPLACE INTO scratch (key, value) VALUES (?, ?)");
    DbStatement_bind_text(&statement, 1, key);
    DbStatement_bind_text(&statement, 2, value);
    DbStatement_step(&statement);
    DbStatement_close(&statement);

    if (!statement.ok) {
        LOG_error("[Db] failed to save scratch value, op %d\n", statement.error_op);
        return false;
    }

    increment_data_version();

    return true;
}

DbScratchResult* Db_scratchRead(const char* key) {
    if (!key) return NULL;

    sqlite3* database = connection();
    if (!database) return NULL;

    DbScratchResult* result = calloc(1, sizeof(*result));
    if (!result) return NULL;
    result->base.destroy = destroy_scratch_result;
    result->base.data_version = Db_dataVersion();

    DbStatement statement;
    DbStatement_begin(&statement, database, "SELECT value FROM scratch WHERE key = ? LIMIT 1");
    DbStatement_bind_text(&statement, 1, key);
    if (DbStatement_step(&statement)) {
        result->value = DbStatement_dup_ext(&statement, 0);
    }
    DbStatement_close(&statement);

    if (!statement.ok) {
        LOG_error("[Db] failed to finish reading scratch value, op %d\n",
                  statement.error_op);
        Db_freeResult(result);
        return NULL;
    }
    return result;
}
