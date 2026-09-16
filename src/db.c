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

static bool validate_actions(const DbSchemaAction* actions,
                             size_t* action_count, int* schema_version) {
    size_t index = 0;
    for (; actions[index].version >= 0; index++) {
        const DbSchemaAction* action = &actions[index];
        if (action->version != (int)index + 1) {
            LOG_error("[Db] invalid schema action %zu\n", index);
            return false;
        }

        bool valid_sql = action->type == DB_SCHEMA_SQL && action->sql;
        bool valid_function = action->type == DB_SCHEMA_FUNCTION &&
                              action->function;
        bool valid_text = action->text && action->text[0] != '\0';

        bool valid_action = (valid_sql || valid_function) && valid_text;
        if (!valid_action) {
            LOG_error("[Db] invalid schema action %zu\n", index);
            return false;
        }
    }
    *action_count = index;
    *schema_version = (int)index;
    return true;
}

static bool run_action(sqlite3* database, const DbSchemaAction* action) {
    char pragma[64];
    snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d", action->version);

    bool success = true;

    // begin/execute/update_version/commit
    success = success && Db_begin();
    if (action->type == DB_SCHEMA_SQL) {
        success = success && execute(database, action->sql);
    } else {
        success = success && action->function();
    }
    success = success && execute(database, pragma);
    success = success && Db_commit();
    if (!success) {
        Db_rollback();
        LOG_error("[Db] failed schema action %d \"%s\"\n", action->version,
                  action->text);
        return false;
    }
    return true;
}

static bool migrate(sqlite3* database) {
    int version = 0;
    if (!read_schema_version(database, &version)) return false;
    DbSchemaAction* actions = DbSchema_getActions();
    if (!actions) return false;
    size_t action_count = 0;
    int schema_version = 0;
    if (!validate_actions(actions, &action_count, &schema_version)) {
        free(actions);
        return false;
    }
    if (version < 0 || version > schema_version) {
        LOG_error("[Db] database schema version %d is newer than binary version %d\n",
                  version, schema_version);
        free(actions);
        return false;
    }

    for (size_t index = (size_t)version; index < action_count; index++) {
        if (!run_action(database, &actions[index])) {
            break;
        }
        version = actions[index].version;
    }
    bool success = version == schema_version;
    free(actions);
    return success;
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

bool Db_isAvailable(void) {
    return atomic_load(&database_available) != 0;
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

int Db_dataVersion(void) {
    return atomic_load(&data_version);
}

bool Db_resultIsCurrent(const DbResult* result) {
    if (!result) return false;
    return result->data_version == Db_dataVersion();
}

void Db_freeSettingsResult(DbSettingsResult* result) {
    if (!result) return;

    for (int index = 0; index < result->count; index++) {
        free(result->items[index].name);
        free(result->items[index].string_value);
    }
    free(result->items);
    free(result);
}

static bool append_setting(DbSettingsResult* result, DbSetting* setting) {
    DbSetting* items = realloc(result->items,
                               (size_t)(result->count + 1) * sizeof(*items));
    if (!items) return false;

    result->items = items;
    result->items[result->count++] = *setting;
    return true;
}

DbSettingsResult* Db_readSettings(void) {
    DbSettingsResult* result = calloc(1, sizeof(*result));
    if (!result) return NULL;
    result->base.data_version = Db_dataVersion();

    sqlite3* database = connection();
    if (!database) {
        LOG_error("[Db] failed to read settings: no database\n");
        return result;
    }

    DbStatement statement;
    if (!DbStatement_begin(&statement, database,
                           "SELECT name, type, value FROM settings")) {
        LOG_error("[Db] failed to start settings read, op %d\n",
                  statement.error_op);
        DbStatement_close(&statement);
        return result;
    }

    while (DbStatement_step(&statement)) {
        const char* name = DbStatement_get_text(&statement, 0);
        const char* type = DbStatement_get_text(&statement, 1);
        DbSetting setting = {0};

        if (!name || !type) {
            LOG_error("[Db] skipped setting with no name or type\n");
            continue;
        }

        if (strcmp(type, "int") == 0) {
            setting.type = DB_SETTING_INT;
            setting.int_value = DbStatement_get_int(&statement, 2);
        } else if (strcmp(type, "bool") == 0) {
            setting.type = DB_SETTING_BOOL;
            setting.bool_value = DbStatement_get_int(&statement, 2) != 0;
        } else if (strcmp(type, "string") == 0) {
            setting.type = DB_SETTING_STRING;
            setting.string_value = DbStatement_dup_ext(&statement, 2);
            if (!setting.string_value && statement.ok) {
                LOG_error("[Db] failed to copy setting %s\n", name);
                statement.ok = false;
            }
        } else {
            LOG_warn("[Db] skipped setting %s with unknown type %s\n", name, type);
            continue;
        }

        setting.name = strdup(name);
        if (!setting.name || !append_setting(result, &setting)) {
            free(setting.name);
            free(setting.string_value);
            LOG_error("[Db] failed to copy setting %s\n", name);
            statement.ok = false;
            break;
        }
    }

    if (!statement.ok) {
        LOG_error("[Db] failed to finish settings read, op %d\n",
                  statement.error_op);
        DbStatement_close(&statement);
        Db_freeSettingsResult(result);
        result = calloc(1, sizeof(*result));
        if (result) result->base.data_version = Db_dataVersion();
        return result;
    }

    if (!DbStatement_close(&statement)) {
        LOG_error("[Db] failed to close settings read, op %d\n",
                  statement.error_op);
        Db_freeSettingsResult(result);
        result = calloc(1, sizeof(*result));
        if (result) result->base.data_version = Db_dataVersion();
    }
    return result;
}

static bool save_setting(const char* name, const char* type,
                         int int_value, const char* string_value) {
    if (!name || !type) return false;

    DbStatement statement;
    DbStatement_begin(&statement, connection(),
                      "INSERT OR REPLACE INTO settings (name, type, value) "
                      "VALUES (?, ?, ?)");
    DbStatement_bind_text(&statement, 1, name);
    DbStatement_bind_text(&statement, 2, type);
    if (string_value) {
        DbStatement_bind_text(&statement, 3, string_value);
    } else {
        DbStatement_bind_int(&statement, 3, int_value);
    }
    DbStatement_step(&statement);
    DbStatement_close(&statement);

    if (!statement.ok) {
        LOG_error("[Db] failed to save setting %s, op %d\n",
                  name, statement.error_op);
        return false;
    }

    increment_data_version();
    return true;
}

bool Db_saveIntSetting(const char* name, int value) {
    return save_setting(name, "int", value, NULL);
}

bool Db_saveBoolSetting(const char* name, bool value) {
    return save_setting(name, "bool", value ? 1 : 0, NULL);
}

bool Db_saveStringSetting(const char* name, const char* value) {
    if (!value) return false;
    return save_setting(name, "string", 0, value);
}
