#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#include "db_statement.h"
#include "debug.h"

static void set_error_va(DbStatement* statement, const char* format, va_list args) {
    vsnprintf(statement->errmsg, sizeof(statement->errmsg), format, args);
    statement->ok = false;
}

static void set_error(DbStatement* statement, const char* format, ...) {
    va_list args;
    va_start(args, format);
    set_error_va(statement, format, args);
    va_end(args);
}

// Records a label for the operation, then the message of the last SQLite call on the
// connection: "bind 3: column index out of range".
static void set_sqlite_error(DbStatement* statement, const char* format, ...) {
    char label[64];
    va_list args;
    va_start(args, format);
    vsnprintf(label, sizeof(label), format, args);
    va_end(args);
    set_error(statement, "%s: %s", label, sqlite3_errmsg(statement->database));
}

void DbStatement_mark_failed(DbStatement* statement, const char* format, ...) {
    if (!statement->ok) return;
    va_list args;
    va_start(args, format);
    set_error_va(statement, format, args);
    va_end(args);
}

void* DbStatement_calloc(DbStatement* statement, size_t size) {
    if (!statement->ok) return NULL;
    void* memory = calloc(1, size);
    if (!memory) set_error(statement, "calloc: out of memory");
    return memory;
}

char* DbStatement_strdup(DbStatement* statement, const char* value) {
    if (!statement->ok || !value) return NULL;
    char* copy = strdup(value);
    if (!copy) set_error(statement, "strdup: out of memory");
    return copy;
}

bool DbStatement_begin(DbStatement *statement, sqlite3 *database, const char *sql) {
    statement->database = database;
    statement->statement = NULL;
    statement->has_data = false;
    statement->errmsg[0] = '\0';
    statement->last_insert_id = 0;
    statement->ok = false;

    if (!statement->database) {
        set_error(statement, "begin: no database");
        return false;
    }
    if (!sql) {
        set_error(statement, "begin: no SQL");
        return false;
    }

    int result = sqlite3_prepare_v2(statement->database, sql, -1,
                                    &statement->statement, NULL);
    if (result != SQLITE_OK) {
        set_sqlite_error(statement, "begin");
        return false;
    }
    if (!statement->statement) {
        set_error(statement, "begin: SQL contains no statement");
        return false;
    }

    statement->ok = true;
    return statement->ok;
}

int DbStatement_parameter_count(DbStatement* statement) {
    if (!statement->ok) return 0;
    return sqlite3_bind_parameter_count(statement->statement);
}

bool DbStatement_bind(DbStatement* statement, int index, DbArg arg) {
    if (!statement->ok) {
        return false;
    }

    int result;
    switch (arg.type) {
        case DB_ARG_INT:
            result = sqlite3_bind_int(statement->statement, index, arg.int_value);
            break;
        case DB_ARG_TEXT:
            result = sqlite3_bind_text(statement->statement, index, arg.text_value, -1,
                                       SQLITE_TRANSIENT);
            break;
        case DB_ARG_NULL:
            result = sqlite3_bind_null(statement->statement, index);
            break;
        default:
            set_error(statement, "bind %d: unknown argument type %d", index, (int)arg.type);
            return false;
    }
    if (result != SQLITE_OK) {
        set_sqlite_error(statement, "bind %d", index);
    }

    return statement->ok;
}

// Returns true when the current row has a value in the column. Marks the statement
// failed for a NULL value, thus a strict reader stops the flow.
static bool strict_value(DbStatement* statement, int column) {
    if (sqlite3_column_type(statement->statement, column) != SQLITE_NULL) return true;
    set_error(statement, "get %d: NULL in a NOT NULL column", column);
    return false;
}

const char* DbStatement_get_text(DbStatement* statement, int column) {
    if (!statement->ok) return NULL;
    const char* value = statement->has_data && strict_value(statement, column)
        ? (const char*)sqlite3_column_text(statement->statement, column)
        : NULL;
    return value;
}

char* DbStatement_dup_text(DbStatement* statement, int column) {
    return DbStatement_strdup(statement, DbStatement_get_text(statement, column));
}

int DbStatement_get_int(DbStatement* statement, int column) {
    if (!statement->ok) return 0;
    int value = statement->has_data && strict_value(statement, column)
        ? sqlite3_column_int(statement->statement, column)
        : 0;
    return value;
}

const char* DbStatement_get_text_or_null(DbStatement* statement, int column) {
    if (!statement->ok) return NULL;
    const char* value = statement->has_data
        ? (const char*)sqlite3_column_text(statement->statement, column)
        : NULL;
    return value;
}

char* DbStatement_dup_text_or_null(DbStatement* statement, int column) {
    return DbStatement_strdup(statement, DbStatement_get_text_or_null(statement, column));
}

int DbStatement_get_int_or(DbStatement* statement, int column, int default_value) {
    if (!statement->ok) return default_value;
    int value = statement->has_data &&
                sqlite3_column_type(statement->statement, column) != SQLITE_NULL
        ? sqlite3_column_int(statement->statement, column)
        : default_value;
    return value;
}

bool DbStatement_step(DbStatement *statement) {
    if (!statement->ok) {
        return false;
    }

    int result = sqlite3_step(statement->statement);
    statement->has_data = result == SQLITE_ROW;
    if (result == SQLITE_DONE) {
        statement->last_insert_id = (int)sqlite3_last_insert_rowid(statement->database);
    } else if (result != SQLITE_ROW) {
        set_sqlite_error(statement, "step");
    }

    return statement->has_data;
}

bool DbStatement_close(DbStatement *statement) {
    statement->has_data = false;

    sqlite3_stmt* sqlite_statement = statement->statement;
    statement->statement = NULL;
    if (!sqlite_statement) return statement->ok;

    int result = sqlite3_finalize(sqlite_statement);
    if (result != SQLITE_OK && statement->ok) {
        set_sqlite_error(statement, "close");
    }

    return statement->ok;
}

// Binds one DbArg from args for each parameter of the statement, in order.
static void bind_all(DbStatement* statement, va_list args) {
    int count = DbStatement_parameter_count(statement);
    for (int index = 1; index <= count; index++) {
        DbStatement_bind(statement, index, va_arg(args, DbArg));
    }
}

bool DbStatement_exec(DbStatement* statement, sqlite3* database, const char* sql, ...) {
    DbStatement_begin(statement, database, sql);
    va_list args;
    va_start(args, sql);
    bind_all(statement, args);
    va_end(args);
    DbStatement_step(statement);
    return DbStatement_close(statement);
}

int DbStatement_query_int(DbStatement* statement, sqlite3* database, const char* sql, ...) {
    DbStatement_begin(statement, database, sql);
    va_list args;
    va_start(args, sql);
    bind_all(statement, args);
    va_end(args);
    if (!DbStatement_step(statement)) DbStatement_mark_failed(statement, "query_int: no row");
    int value = DbStatement_get_int(statement, 0);
    DbStatement_close(statement);
    return statement->ok ? value : 0;
}

int DbStatement_query_int_or(DbStatement* statement, sqlite3* database, int default_value,
                             const char* sql, ...) {
    DbStatement_begin(statement, database, sql);
    va_list args;
    va_start(args, sql);
    bind_all(statement, args);
    va_end(args);
    DbStatement_step(statement);
    int value = DbStatement_get_int_or(statement, 0, default_value);
    DbStatement_close(statement);
    return statement->ok ? value : default_value;
}
