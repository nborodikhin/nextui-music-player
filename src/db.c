#include "db.h"

#include <pthread.h>
#include <stdarg.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "defines.h"
#include "api.h"
#include "debug.h"
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

static int wait_for_database_lock(void* context, int previous_attempts) {
    (void)context;
    (void)previous_attempts;
    sqlite3_sleep(5);
    return 1;
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

    result = sqlite3_busy_handler(database, wait_for_database_lock, NULL);
    if (result != SQLITE_OK) {
        LOG_error("[Db] failed to set busy handler: %s\n", sqlite3_errmsg(database));
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

    DbStatement_exec(&statement, database, "PRAGMA foreign_keys=ON");
    if (!statement.ok) {
        LOG_error("[Db] failed to enable foreign keys: %s\n", statement.errmsg);
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
        LOG_error("[Db] failed to read schema version: %s\n", statement.errmsg);
        return false;
    }

    return true;
}

static bool validate_steps(const DbMigrationStep* steps,
                           size_t* step_count, int* schema_version) {
    size_t index = 0;
    for (; steps[index].version >= 0; index++) {
        const DbMigrationStep* step = &steps[index];
        if (step->version != (int)index + 1) {
            LOG_error("[Db] invalid migration step %zu\n", index);
            return false;
        }

        bool valid_sql = step->type == DB_MIGRATION_SQL && step->sql;
        bool valid_function = step->type == DB_MIGRATION_FUNCTION &&
                              step->function;
        bool valid_text = step->text && step->text[0] != '\0';

        bool valid_action = (valid_sql || valid_function) && valid_text;
        if (!valid_action) {
            LOG_error("[Db] invalid migration step %zu\n", index);
            return false;
        }
    }
    *step_count = index;
    *schema_version = (int)index;
    return true;
}

static bool run_step(sqlite3* database, const DbMigrationStep* step) {
    char pragma[64];
    snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d", step->version);

    bool success = true;

    // begin/execute/update_version/commit
    success = success && Db_begin();
    if (step->type == DB_MIGRATION_SQL) {
        success = success && execute(database, step->sql);
    } else {
        success = success && step->function(database);
    }
    success = success && execute(database, pragma);
    success = success && Db_commit();
    if (!success) {
        Db_rollback();
        LOG_error("[Db] failed migration step %d \"%s\"\n", step->version,
                  step->text);
    }
    return success;
}

static bool migrate(sqlite3* database) {
    int version = 0;
    if (!read_schema_version(database, &version)) return false;
    DbMigrationStep* steps = DbSchema_getSteps();
    if (!steps) return false;
    size_t step_count = 0;
    int schema_version = 0;
    if (!validate_steps(steps, &step_count, &schema_version)) {
        free(steps);
        return false;
    }
    if (version < 0 || version > schema_version) {
        LOG_error("[Db] database schema version %d is newer than binary version %d\n",
                  version, schema_version);
        free(steps);
        return false;
    }

    for (size_t index = (size_t)version; index < step_count; index++) {
        if (!run_step(database, &steps[index])) {
            break;
        }
        version = steps[index].version;
    }
    bool success = version == schema_version;
    free(steps);
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

void Db_freeResult(void* value) {
    if (!value) return;

    DbResult* result = value;
    if (!result->free) {
        LOG_error("[Db] result has no free function\n");
        return;
    }
    result->free(value);
}

// Snapshot reads

// Starts a savepoint. Outside a transaction it starts a read transaction, thus each query
// until the release sees the same data. Returns false when the savepoint did not start; the
// connection then cannot read either.
static bool begin_snapshot(const char* name) {
    char sql[64];
    snprintf(sql, sizeof(sql), "SAVEPOINT %s", name);
    return execute(connection(), sql);
}

static void end_snapshot(const char* name) {
    char sql[64];
    snprintf(sql, sizeof(sql), "RELEASE %s", name);
    execute(connection(), sql);
}

// Settings

static void free_settings_result(DbSettingsResult* result) {
    for (int index = 0; index < result->count; index++) {
        free(result->items[index].name);
        free(result->items[index].string_value);
    }
    free(result->items);
    free(result);
}

static DbSettingsResult* allocate_settings_result(void) {
    DbSettingsResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_settings_result;
    }
    return result;
}

// Returns -1 for a type name that is not known.
static DbSettingType setting_type_from_name(const char* name) {
    if (!name) return (DbSettingType) -1;
    if (strcmp(name, "int") == 0) return DB_SETTING_INT;
    if (strcmp(name, "bool") == 0) return DB_SETTING_BOOL;
    if (strcmp(name, "string") == 0) return DB_SETTING_STRING;
    return (DbSettingType) -1;
}

DbSettingsResult* Db_readSettings(void) {
    DbSettingsResult* result = allocate_settings_result();
    if (!result) return NULL;

    if (!begin_snapshot("read_settings")) {
        LOG_error("[Db] failed to read settings: no snapshot\n");
        return result;
    }

    DbStatement statement;
    int count = DbStatement_query_int(&statement, connection(),
                                      "SELECT COUNT(*) FROM settings");

    if (statement.ok && count > 0) {
        DbStatement_begin(&statement, connection(), "SELECT name, type, value FROM settings");
        result->items = DbStatement_calloc(&statement, (size_t)count * sizeof(*result->items));
        for (int row = 0; row < count && DbStatement_step(&statement); row++) {
            const char* type_name = DbStatement_get_text(&statement, 1);
            DbSettingType type = setting_type_from_name(type_name);
            if (type == (DbSettingType) -1) {
                if (statement.ok) {
                    LOG_warn("[Db] skipped setting %s with unknown type %s\n",
                             DbStatement_get_text(&statement, 0), type_name);
                }
                continue;
            }

            DbSetting* setting = &result->items[result->count++];
            setting->name = DbStatement_dup_text(&statement, 0);
            setting->type = type;
            switch (type) {
                case DB_SETTING_INT:
                    setting->int_value = DbStatement_get_int(&statement, 2);
                    break;
                case DB_SETTING_BOOL: {
                    const char* value = DbStatement_get_text(&statement, 2);
                    setting->bool_value = value && strcmp(value, "true") == 0;
                    break;
                }
                case DB_SETTING_STRING:
                    setting->string_value = DbStatement_dup_text(&statement, 2);
                    break;
            }
        }
        DbStatement_close(&statement);
    }

    end_snapshot("read_settings");

    if (!statement.ok) {
        free_settings_result(result);
        result = allocate_settings_result();
        LOG_error("[Db] failed to read settings: %s\n", statement.errmsg);
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
        LOG_error("[Db] failed to save setting %s: %s\n",
                  name, statement.errmsg);
        return false;
    }

    increment_data_version();
    return true;
}

bool Db_saveIntSetting(const char* name, int value) {
    return save_setting(name, "int", value, NULL);
}

bool Db_saveBoolSetting(const char* name, bool value) {
    return save_setting(name, "bool", 0, value ? "true" : "false");
}

bool Db_saveStringSetting(const char* name, const char* value) {
    if (!value) return false;
    return save_setting(name, "string", 0, value);
}

// Directory reads

static void free_dir_row(DbDir* row) {
    free(row->path);
    free(row->filename);
}

static void free_dir_result(DbDirResult* result) {
    free_dir_row(&result->dir);
    free(result);
}

static DbDirResult* allocate_dir_result(void) {
    DbDirResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_dir_result;
    }
    return result;
}

// Reads a directory row: id, parent_id, path, filename.
static void copy_dir_row(DbStatement* statement, DbDir* row) {
    row->id = DbStatement_get_int(statement, 0);
    row->parent_id = DbStatement_get_int_or(statement, 1, -1);
    row->path = DbStatement_dup_text(statement, 2);
    row->filename = DbStatement_dup_text(statement, 3);
}

int Db_getRootDirId(void) {
    DbStatement statement;
    int id = DbStatement_query_int_or(&statement, connection(), -1,
                                      "SELECT id FROM dirs WHERE parent_id IS NULL");
    if (!statement.ok) {
        LOG_error("[Db] failed to read root directory: %s\n", statement.errmsg);
    }
    return id;
}

// Reads the directory row that matches a WHERE condition into row. Give one DbArg for each
// '?' of the condition, in order. Returns false when no row matches or the read failed; row
// then holds no allocated fields.
static bool read_dir_where(DbDir* dbDir, const char* condition, ...) {
    *dbDir = (DbDir){0};

    const char* format =
        "SELECT id, parent_id, path, filename FROM dirs WHERE %s";

    char sql[256];
    int length = snprintf(sql, sizeof(sql), format, condition);
    if (length < 0 || (size_t)length >= sizeof(sql)) {
        LOG_error("[Db] directory query is too long\n");
        return false;
    }

    DbStatement statement;
    DbStatement_begin(&statement, connection(), sql);
    int arg_count = DbStatement_parameter_count(&statement);
    va_list args;
    va_start(args, condition);
    for (int index = 1; index <= arg_count; index++) {
        DbStatement_bind(&statement, index, va_arg(args, DbArg));
    }
    va_end(args);

    bool found = DbStatement_step(&statement);
    copy_dir_row(&statement, dbDir);
    if (!DbStatement_close(&statement)) {
        free_dir_row(dbDir);
        *dbDir = (DbDir){0};
        LOG_error("[Db] failed to read directory: %s\n", statement.errmsg);
        return false;
    }
    return found;
}

static const char* file_type_name(DbFileType type) {
    switch (type) {
        case DB_FILE_TYPE_MUSIC:   return "music";
        case DB_FILE_TYPE_OTHER:   return "other";
        case DB_FILE_TYPE_UNKNOWN:
        default:                   return "unknown";
    }
}

// Reads the directory that matches a WHERE condition with one '?'. Returns NULL when no
// directory matches or the read failed.
static DbDirResult* get_dir_where(const char* condition, DbArg arg) {
    DbDirResult* result = allocate_dir_result();
    if (result && !read_dir_where(&result->dir, condition, arg)) {
        free_dir_result(result);
        result = NULL;
    }
    return result;
}

DbDirResult* Db_getDir(int id) {
    return get_dir_where("id = ?", INT_ARG(id));
}

DbDirResult* Db_getDirByPath(const char* path) {
    if (!path) return NULL;
    return get_dir_where("path = ?", TEXT_ARG(path));
}

// File reads

static DbFileType file_type_from_name(const char* name) {
    if (!name) return DB_FILE_TYPE_UNKNOWN;
    if (strcmp(name, "music") == 0) return DB_FILE_TYPE_MUSIC;
    if (strcmp(name, "other") == 0) return DB_FILE_TYPE_OTHER;
    return DB_FILE_TYPE_UNKNOWN;
}

static void free_file_row(DbFile* row) {
    free(row->dir_path);
    free(row->path);
    free(row->filename);
}

static void free_file_result(DbFileResult* result) {
    free_file_row(&result->file);
    free(result);
}

static DbFileResult* allocate_file_result(void) {
    DbFileResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_file_result;
    }
    return result;
}

// The columns of a file row, for copy_file_row. A query gives `f` for `files` and `d` for the
// `dirs` row of the directory of the file. The path of a file in the Music root is its filename.
#define FILE_ROW_COLUMNS \
    "f.id, f.parent_id, d.path, " \
    "CASE WHEN d.path = '' THEN f.filename ELSE d.path || '/' || f.filename END, " \
    "f.filename, f.type"

// Reads a file row of FILE_ROW_COLUMNS: id, parent_id, dir_path, path, filename, type.
static void copy_file_row(DbStatement* statement, DbFile* row) {
    row->id = DbStatement_get_int(statement, 0);
    row->parent_id = DbStatement_get_int(statement, 1);
    row->dir_path = DbStatement_dup_text(statement, 2);
    row->path = DbStatement_dup_text(statement, 3);
    row->filename = DbStatement_dup_text(statement, 4);
    row->type = file_type_from_name(DbStatement_get_text(statement, 5));
}

// Reads one file row that matches a WHERE condition.
// Condition can refer to `f` from `files` table and `d` from `dirs` table.
// Caller must pass one DbArg for each '?' of the condition, in order.
// Returns NULL when no file matches or the read failed.
static DbFileResult* read_file_where(const char* condition, ...) {
    const char *format =
        "SELECT " FILE_ROW_COLUMNS " "
        "FROM files f JOIN dirs d ON d.id = f.parent_id WHERE %s";

    char sql[512];
    int length = snprintf(sql, sizeof(sql), format, condition);
    if (length < 0 || (size_t)length >= sizeof(sql)) {
        LOG_error("[Db] file query is too long\n");
        return NULL;
    }

    DbFileResult* result = allocate_file_result();
    if (!result) return NULL;

    DbStatement statement;
    DbStatement_begin(&statement, connection(), sql);
    int arg_count = DbStatement_parameter_count(&statement);
    va_list args;
    va_start(args, condition);
    for (int index = 1; index <= arg_count; index++) {
        DbStatement_bind(&statement, index, va_arg(args, DbArg));
    }
    va_end(args);

    bool found = DbStatement_step(&statement);
    if (found) copy_file_row(&statement, &result->file);
    DbStatement_close(&statement);
    if (!statement.ok) LOG_error("[Db] failed to read file: %s\n", statement.errmsg);

    if (!found || !statement.ok) {
        free_file_result(result);
        result = NULL;
    }
    return result;
}

DbFileResult* Db_getFile(int id) {
    return read_file_where("f.id = ?", INT_ARG(id));
}

DbFileResult* Db_getFileByPath(const char* path) {
    if (!path || !path[0]) return NULL;

    const char* slash = strrchr(path, '/');
    const char* filename = slash ? slash + 1 : path;
    size_t dir_length = slash ? (size_t)(slash - path) : 0;
    if (!filename[0]) return NULL;

    char* dir_path = strndup(path, dir_length);
    if (!dir_path) return NULL;

    DbFileResult* result = read_file_where("d.path = ? AND f.filename = ?",
                                          TEXT_ARG(dir_path), TEXT_ARG(filename));
    free(dir_path);
    return result;
}

DbFileResult* Db_getFileInDir(int dir_id, const char* filename) {
    if (!filename) return NULL;
    return read_file_where("f.parent_id = ? AND f.filename = ?",
                          INT_ARG(dir_id), TEXT_ARG(filename));
}

static void free_file_array(DbFile* rows, int count) {
    for (int index = 0; index < count; index++) {
        free_file_row(&rows[index]);
    }
    free(rows);
}

// Directory contents read

static void free_dir_array(DbDir* rows, int count) {
    for (int index = 0; index < count; index++) {
        free_dir_row(&rows[index]);
    }
    free(rows);
}

static void free_read_dir_result(DbReadDirResult* result) {
    free_dir_row(&result->dir);
    free_dir_array(result->dirs, result->dir_count);
    free_file_array(result->files, result->file_count);
    free(result);
}

static DbReadDirResult* allocate_read_dir_result(void) {
    DbReadDirResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_read_dir_result;
    }
    return result;
}

DbReadDirResult* Db_readDir(int dir_id) {
    DbReadDirResult* result = allocate_read_dir_result();
    if (!result) return NULL;

    if (!read_dir_where(&result->dir, "id = ?", INT_ARG(dir_id))) {
        free_read_dir_result(result);
        return NULL;
    }

    if (!begin_snapshot("read_dir")) {
        free_read_dir_result(result);
        LOG_error("[Db] failed to read directory contents: no snapshot\n");
        return NULL;
    }

    DbStatement statement;
    int dir_count = DbStatement_query_int(&statement, connection(),
                                          "SELECT COUNT(*) FROM dirs WHERE parent_id = ?",
                                          INT_ARG(dir_id));
    int file_count = statement.ok
        ? DbStatement_query_int(&statement, connection(),
                                "SELECT COUNT(*) FROM files WHERE parent_id = ?",
                                INT_ARG(dir_id))
        : 0;

    if (statement.ok && dir_count > 0) {
        DbStatement_begin(&statement, connection(),
                          "SELECT id, parent_id, path, filename FROM dirs "
                          "WHERE parent_id = ? ORDER BY filename COLLATE NOCASE, id");
        DbStatement_bind(&statement, 1, INT_ARG(dir_id));
        result->dirs = DbStatement_calloc(&statement, (size_t)dir_count * sizeof(*result->dirs));
        while (result->dir_count < dir_count && DbStatement_step(&statement)) {
            copy_dir_row(&statement, &result->dirs[result->dir_count++]);
        }
        DbStatement_close(&statement);
    }

    if (statement.ok && file_count > 0) {
        DbStatement_begin(&statement, connection(),
                          "SELECT " FILE_ROW_COLUMNS " "
                          "FROM files f JOIN dirs d ON d.id = f.parent_id "
                          "WHERE f.parent_id = ? ORDER BY f.filename COLLATE NOCASE, f.id");
        DbStatement_bind(&statement, 1, INT_ARG(dir_id));
        result->files = file_count > 0
            ? DbStatement_calloc(&statement, (size_t)file_count * sizeof(*result->files))
            : NULL;
        while (result->file_count < file_count && DbStatement_step(&statement)) {
            copy_file_row(&statement, &result->files[result->file_count++]);
        }
        DbStatement_close(&statement);
    }

    end_snapshot("read_dir");

    if (statement.ok) {
        result->count = result->dir_count + result->file_count;
    } else {
        free_read_dir_result(result);
        result = NULL;
        LOG_error("[Db] failed to read directory contents: %s\n", statement.errmsg);
    }
    return result;
}

// Files of directories

static void free_files_result(DbFilesResult* result) {
    free_file_array(result->items, result->count);
    free(result);
}

static DbFilesResult* allocate_files_result(void) {
    DbFilesResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_files_result;
    }
    return result;
}

// Returns a copy of the path of a directory, or NULL when the directory has no row or the read
// failed. The caller frees it.
static char* read_dir_path(DbStatement* statement, int dir_id) {
    DbStatement_begin(statement, connection(), "SELECT path FROM dirs WHERE id = ?");
    DbStatement_bind(statement, 1, INT_ARG(dir_id));
    if (!DbStatement_step(statement)) DbStatement_mark_failed(statement, "no directory %d", dir_id);
    char* path = DbStatement_dup_text(statement, 0);
    DbStatement_close(statement);
    if (!statement->ok) {
        free(path);
        path = NULL;
    }
    return path;
}

// Selects a directory, given as ?1, and each directory below it. The directories below have
// paths that start with the path of the directory, given as ?2, and a slash. '0' follows '/',
// thus the range holds each such path. The Music root has an empty path and holds each
// directory.
#define SUBTREE_DIRS_CONDITION \
    "(?2 = '' OR d.id = ?1 OR (d.path >= ?2 || '/' AND d.path < ?2 || '0'))"

// Counts the files of type in a directory and in each directory below it. Give "" in type_name
// to count the files of each type.
static int count_files_below(DbStatement* statement, int dir_id, const char* type_name) {
    char* path = read_dir_path(statement, dir_id);
    if (!path) return 0;

    int count = path[0]
        ? DbStatement_query_int(statement, connection(),
                                "SELECT COUNT(*) FROM dirs d JOIN files f ON f.parent_id = d.id "
                                "WHERE " SUBTREE_DIRS_CONDITION " AND (?3 = '' OR f.type = ?3)",
                                INT_ARG(dir_id), TEXT_ARG(path), TEXT_ARG(type_name))
        : DbStatement_query_int(statement, connection(),
                                "SELECT COUNT(*) FROM files WHERE ?1 = '' OR type = ?1",
                                TEXT_ARG(type_name));
    free(path);
    return count;
}

// Returns the name of type for a query, or "" for DB_FILE_TYPE_ALL.
static const char* type_filter(DbFileType type) {
    return type == DB_FILE_TYPE_ALL ? "" : file_type_name(type);
}

// Counts the files of a type name in a directory, and in each directory below it when
// recursive is true.
static int count_files(DbStatement* statement, int dir_id, const char* type_name,
                       bool recursive) {
    if (recursive) return count_files_below(statement, dir_id, type_name);
    return DbStatement_query_int(statement, connection(),
                                 "SELECT COUNT(*) FROM files "
                                 "WHERE parent_id = ?1 AND (?2 = '' OR type = ?2)",
                                 INT_ARG(dir_id), TEXT_ARG(type_name));
}

int Db_countFiles(int dir_id, DbFileType type, bool recursive) {
    if (!begin_snapshot("count_files")) {
        LOG_error("[Db] failed to count files: no snapshot\n");
        return 0;
    }
    DbStatement statement;
    int count = count_files(&statement, dir_id, type_filter(type), recursive);
    end_snapshot("count_files");

    if (!statement.ok) {
        LOG_error("[Db] failed to count files: %s\n", statement.errmsg);
        return 0;
    }
    return count;
}

// A directory of a subtree, while the subtree is put in the walk order.
typedef struct {
    int   id;
    int   parent_id;
    char* filename;
} SubtreeDir;

// Orders directories by parent, then by name without regard to case, then by id. The children
// of one directory are then next to each other, in the order of the walk.
static int compare_subtree_dirs(const void* left, const void* right) {
    const SubtreeDir* a = left;
    const SubtreeDir* b = right;
    if (a->parent_id != b->parent_id) return a->parent_id < b->parent_id ? -1 : 1;
    int names = strcasecmp(a->filename, b->filename);
    if (names != 0) return names;
    return a->id < b->id ? -1 : (a->id > b->id ? 1 : 0);
}

// Returns the index of the first child of parent_id in dirs, sorted by compare_subtree_dirs.
// Returns count when parent_id has no child.
static int first_child(const SubtreeDir* dirs, int count, int parent_id) {
    int low = 0;
    int high = count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (dirs[middle].parent_id < parent_id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < count && dirs[low].parent_id == parent_id ? low : count;
}

// Writes to *ids the id of a directory and of each directory below it, in the order of a depth
// first walk: a directory, then the subtree of each child, by name. Returns the count of ids, or
// -1 when the read failed. The caller frees *ids.
static int read_subtree_order(DbStatement* statement, int dir_id, int** ids) {
    *ids = NULL;
    char* path = read_dir_path(statement, dir_id);
    if (!path) return -1;

    int count = DbStatement_query_int(statement, connection(),
                                      "SELECT COUNT(*) FROM dirs d WHERE " SUBTREE_DIRS_CONDITION,
                                      INT_ARG(dir_id), TEXT_ARG(path));
    SubtreeDir* dirs = DbStatement_calloc(statement, (size_t)count * sizeof(*dirs));
    int dir_count = 0;
    DbStatement_begin(statement, connection(),
                      "SELECT d.id, d.parent_id, d.filename FROM dirs d "
                      "WHERE " SUBTREE_DIRS_CONDITION);
    DbStatement_bind(statement, 1, INT_ARG(dir_id));
    DbStatement_bind(statement, 2, TEXT_ARG(path));
    while (dirs && dir_count < count && DbStatement_step(statement)) {
        SubtreeDir* dir = &dirs[dir_count++];
        dir->id = DbStatement_get_int(statement, 0);
        dir->parent_id = DbStatement_get_int_or(statement, 1, -1);
        dir->filename = DbStatement_dup_text(statement, 2);
    }
    DbStatement_close(statement);
    free(path);

    // The subtree holds its own directory, thus count is at least 1.
    int* order = DbStatement_calloc(statement, (size_t)count * sizeof(*order));
    int* stack = DbStatement_calloc(statement, (size_t)count * sizeof(*stack));
    int order_count = 0;
    if (statement->ok) {
        qsort(dirs, (size_t)dir_count, sizeof(*dirs), compare_subtree_dirs);
        int stack_count = 0;
        stack[stack_count++] = dir_id;
        while (stack_count > 0) {
            int id = stack[--stack_count];
            order[order_count++] = id;
            // The first child goes on the stack last, thus the walk takes it first.
            int first = first_child(dirs, dir_count, id);
            int last = first;
            while (last < dir_count && dirs[last].parent_id == id) last++;
            for (int i = last - 1; i >= first; i--) stack[stack_count++] = dirs[i].id;
        }
    }

    for (int i = 0; i < dir_count; i++) free(dirs[i].filename);
    free(dirs);
    free(stack);
    if (!statement->ok) {
        free(order);
        return -1;
    }
    *ids = order;
    return order_count;
}

// A page of files that Db_getFiles fills.
typedef struct {
    DbFilesResult* result;
    int            limit;        // the number of files to add
    int            waiting_for;  // the file id to find before adding starts; 0 adds at once
} FilePage;

// Adds the files of a type name in one directory, in the name order, to the page. Sets has_more
// of the page result when a file follows a full page.
static void add_page_files(DbStatement* statement, int dir_id, const char* type_name,
                           FilePage* page) {
    DbFilesResult* result = page->result;
    if (type_name[0]) {
        DbStatement_begin(statement, connection(),
                          "SELECT " FILE_ROW_COLUMNS " "
                          "FROM files f JOIN dirs d ON d.id = f.parent_id "
                          "WHERE f.parent_id = ? AND f.type = ? "
                          "ORDER BY f.filename COLLATE NOCASE, f.id");
        DbStatement_bind(statement, 1, INT_ARG(dir_id));
        DbStatement_bind(statement, 2, TEXT_ARG(type_name));
    } else {
        DbStatement_begin(statement, connection(),
                          "SELECT " FILE_ROW_COLUMNS " "
                          "FROM files f JOIN dirs d ON d.id = f.parent_id "
                          "WHERE f.parent_id = ? "
                          "ORDER BY f.filename COLLATE NOCASE, f.id");
        DbStatement_bind(statement, 1, INT_ARG(dir_id));
    }

    while (!result->has_more && DbStatement_step(statement)) {
        if (page->waiting_for) {
            if (DbStatement_get_int(statement, 0) == page->waiting_for) page->waiting_for = 0;
            continue;
        }
        if (result->count == page->limit) {
            result->has_more = true;
            break;
        }
        copy_file_row(statement, &result->items[result->count++]);
    }
    DbStatement_close(statement);
}

DbFilesResult* Db_getFiles(int dir_id, DbFileType type, bool recursive, int max_count,
                           int token) {
    DbFilesResult* result = allocate_files_result();
    if (!result) return NULL;

    if (!begin_snapshot("get_files")) {
        LOG_error("[Db] failed to read files: no snapshot\n");
        return result;
    }

    const char* type_name = type_filter(type);
    DbStatement statement;
    int* subtree_ids = NULL;
    int single_id[1] = { dir_id };
    int dir_count = recursive ? read_subtree_order(&statement, dir_id, &subtree_ids) : 1;
    const int* dir_ids = recursive ? subtree_ids : single_id;
    bool ok = dir_count >= 0;

    // The count holds the files before the token as well, thus it bounds the page.
    int total = ok ? count_files(&statement, dir_id, type_name, recursive) : 0;
    ok = ok && statement.ok;
    int limit = max_count > 0 && max_count < total ? max_count : total;
    if (ok && limit > 0) {
        result->items = DbStatement_calloc(&statement, (size_t)limit * sizeof(*result->items));
        ok = statement.ok;
    }

    FilePage page = {
        .result      = result,
        .limit       = limit,
        .waiting_for = token > 0 ? token : 0,
    };
    for (int i = 0; ok && limit > 0 && !result->has_more && i < dir_count; i++) {
        add_page_files(&statement, dir_ids[i], type_name, &page);
        ok = statement.ok;
    }

    end_snapshot("get_files");
    free(subtree_ids);

    if (!ok) {
        LOG_error("[Db] failed to read files of directory %d: %s\n", dir_id, statement.errmsg);
        free_files_result(result);
        result = allocate_files_result();
    }
    return result;
}

// Playlist reads

static void free_playlist_row(DbPlaylist* row) {
    free(row->path);
}

static void free_playlist_result(DbPlaylistResult* result) {
    free_playlist_row(&result->playlist);
    free(result);
}

static void free_playlists_result(DbPlaylistsResult* result) {
    for (int index = 0; index < result->count; index++) {
        free_playlist_row(&result->items[index]);
    }
    free(result->items);
    free(result);
}

static DbPlaylistResult* allocate_playlist_result(void) {
    DbPlaylistResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_playlist_result;
    }
    return result;
}

static DbPlaylistsResult* allocate_playlists_result(void) {
    DbPlaylistsResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_playlists_result;
    }
    return result;
}

// Reads a playlist row: id, path, num_entries.
static void copy_playlist_row(DbStatement* statement, DbPlaylist* row) {
    row->id = DbStatement_get_int(statement, 0);
    row->path = DbStatement_dup_text(statement, 1);
    row->num_entries = DbStatement_get_int_or(statement, 2, -1);
}

DbPlaylistResult* Db_getPlaylist(int id) {
    DbPlaylistResult* result = allocate_playlist_result();
    if (!result) return NULL;

    DbStatement statement;
    DbStatement_begin(&statement, connection(),
                      "SELECT id, path, num_entries FROM playlists WHERE id = ?");
    DbStatement_bind_int(&statement, 1, id);
    bool found = DbStatement_step(&statement);
    if (found) copy_playlist_row(&statement, &result->playlist);
    DbStatement_close(&statement);
    if (!statement.ok) LOG_error("[Db] failed to read playlist: %s\n", statement.errmsg);

    if (!found || !statement.ok) {
        free_playlist_result(result);
        result = NULL;
    }
    return result;
}

DbPlaylistsResult* Db_readPlaylists(void) {
    DbPlaylistsResult* result = allocate_playlists_result();
    if (!result) return NULL;

    if (!begin_snapshot("read_playlists")) {
        LOG_error("[Db] failed to read playlists: no snapshot\n");
        return result;
    }

    DbStatement statement;
    int count = DbStatement_query_int(&statement, connection(),
                                      "SELECT COUNT(*) FROM playlists");

    if (statement.ok && count > 0) {
        DbStatement_begin(&statement, connection(),
                          "SELECT id, path, num_entries FROM playlists "
                          "ORDER BY path COLLATE NOCASE, id");
        result->items = DbStatement_calloc(&statement, (size_t)count * sizeof(*result->items));
        while (result->count < count && DbStatement_step(&statement)) {
            copy_playlist_row(&statement, &result->items[result->count++]);
        }
        DbStatement_close(&statement);
    }

    end_snapshot("read_playlists");

    if (!statement.ok) {
        free_playlists_result(result);
        result = allocate_playlists_result();
        LOG_error("[Db] failed to read playlists: %s\n", statement.errmsg);
    }
    return result;
}

// Last played reads

static void free_last_played_result(DbLastPlayedResult* result) {
    free(result);
}

static DbLastPlayedResult* allocate_last_played_result(void) {
    DbLastPlayedResult* result = calloc(1, sizeof(*result));
    if (result) {
        result->base.data_version = Db_dataVersion();
        result->base.free = (DbResultDestructor) free_last_played_result;
    }
    return result;
}

static const char* last_played_type_name(DbLastPlayedType type) {
    return type == DB_LAST_PLAYED_PLAYLIST ? "playlist" : "folder";
}

static DbLastPlayedType last_played_type_from_name(const char* name) {
    if (!name) return (DbLastPlayedType) -1;
    if (strcmp(name, "folder") == 0) return DB_LAST_PLAYED_FOLDER;
    if (strcmp(name, "playlist") == 0) return DB_LAST_PLAYED_PLAYLIST;
    return (DbLastPlayedType) -1;
}

// Reads a last played row: type, id1, id2, position, track_name.
static void copy_last_played_row(DbStatement* statement, DbLastPlayed* row) {
    row->type = last_played_type_from_name(DbStatement_get_text(statement, 0));
    if (row->type == (DbLastPlayedType) -1) {
        DbStatement_mark_failed(statement, "Unknown last played type");
    }
    row->id1 = DbStatement_get_int(statement, 1);
    row->id2 = DbStatement_get_int(statement, 2);
    row->position = DbStatement_get_int(statement, 3);
    const char* track_name = DbStatement_get_text_or_null(statement, 4);
    snprintf(row->track_name, sizeof(row->track_name), "%s", track_name ? track_name : "");
}

DbLastPlayedResult* Db_readLastPlayed(void) {
    DbLastPlayedResult* result = allocate_last_played_result();
    if (!result) return NULL;

    DbStatement statement;
    DbStatement_begin(&statement, connection(),
                      "SELECT type, id1, id2, position, track_name "
                      "FROM last_played WHERE is_last = 'true'");
    bool found = DbStatement_step(&statement);
    if (found) copy_last_played_row(&statement, &result->last_played);
    DbStatement_close(&statement);
    if (!statement.ok) LOG_error("[Db] failed to read last played: %s\n", statement.errmsg);

    if (!found || !statement.ok) {
        free_last_played_result(result);
        result = NULL;
    }
    return result;
}

// Writes

// Advances the data version after a write that succeeded, and logs the error of a write that
// failed. Pass __func__ in function. Returns whether the write succeeded.
static bool finish_write(const DbStatement* statement, const char* function) {
    if (statement->ok) {
        increment_data_version();
    } else {
        LOG_error("[Db] %s failed: %s\n", function, statement->errmsg);
    }
    return statement->ok;
}

// Directory writes

int Db_addDir(int parent_id, const char* path, const char* filename) {
    if (!path || !filename) return 0;

    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "INSERT INTO dirs (parent_id, path, filename) VALUES (?, ?, ?)",
        INT_ARG(parent_id), TEXT_ARG(path), TEXT_ARG(filename));
    return finish_write(&statement, __func__) ? statement.last_insert_id : 0;
}

bool Db_deleteDir(int id) {
    DbStatement statement;
    DbStatement_exec(&statement, connection(), "DELETE FROM dirs WHERE id = ?", INT_ARG(id));
    return finish_write(&statement, __func__);
}

// File writes

int Db_addFile(int parent_id, const char* filename) {
    if (!filename) return 0;

    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "INSERT INTO files (parent_id, filename) VALUES (?, ?)",
        INT_ARG(parent_id), TEXT_ARG(filename));
    return finish_write(&statement, __func__) ? statement.last_insert_id : 0;
}

bool Db_deleteFile(int id) {
    DbStatement statement;
    DbStatement_exec(&statement, connection(), "DELETE FROM files WHERE id = ?", INT_ARG(id));
    return finish_write(&statement, __func__);
}

bool Db_updateFileType(int id, DbFileType type) {
    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "UPDATE files SET type = ? WHERE id = ?", TEXT_ARG(file_type_name(type)), INT_ARG(id));
    return finish_write(&statement, __func__);
}

// Playlist writes

int Db_getOrCreatePlaylist(const char* path) {
    if (!path) return 0;

    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "INSERT OR IGNORE INTO playlists (path) VALUES (?)", TEXT_ARG(path));
    if (!finish_write(&statement, __func__)) return 0;

    int id = DbStatement_query_int(&statement, connection(),
        "SELECT id FROM playlists WHERE path = ?", TEXT_ARG(path));
    if (!statement.ok) LOG_error("[Db] %s failed: %s\n", __func__, statement.errmsg);
    return id;
}

bool Db_deletePlaylist(int id) {
    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "DELETE FROM playlists WHERE id = ?", INT_ARG(id));
    return finish_write(&statement, __func__);
}

bool Db_updatePlaylist(int id, int num_entries) {
    if (id <= 0) return false;

    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "UPDATE playlists SET num_entries = ? WHERE id = ?", INT_ARG(num_entries), INT_ARG(id));
    return finish_write(&statement, __func__);
}

// Last played writes

bool Db_saveLastPlayed(DbLastPlayedType type, int id1, int id2, const char* track_name,
                       int position) {
    if (!Db_begin()) return false;

    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "UPDATE last_played SET is_last = 'false' WHERE is_last = 'true'");
    if (statement.ok) {
        DbStatement_exec(&statement, connection(),
            "INSERT OR REPLACE INTO last_played "
            "(type, id1, id2, position, is_last, track_name) "
            "VALUES (?, ?, ?, ?, 'true', ?)",
            TEXT_ARG(last_played_type_name(type)), INT_ARG(id1), INT_ARG(id2), INT_ARG(position),
            track_name && track_name[0] ? TEXT_ARG(track_name) : NULL_ARG);
    }
    if (!statement.ok) LOG_error("[Db] %s failed: %s\n", __func__, statement.errmsg);
    // The commit advances the data version.
    bool success = statement.ok && Db_commit();
    if (!success) Db_rollback();
    return success;
}

bool Db_saveLastPlayedPosition(int position) {
    DbStatement statement;
    DbStatement_exec(&statement, connection(),
        "UPDATE last_played SET position = ? WHERE is_last = 'true'", INT_ARG(position));
    // note: last position is restoration-only but saved often.
    // Saving it does not trigger a data version update
    if (!statement.ok) LOG_error("[Db] %s failed: %s\n", __func__, statement.errmsg);
    return statement.ok;
}

bool Db_clearLastPlayed(void) {
    DbStatement statement;
    DbStatement_exec(&statement, connection(), "DELETE FROM last_played");
    return finish_write(&statement, __func__);
}
