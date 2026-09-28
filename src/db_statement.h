#ifndef NEXTUI_MUSIC_PLAYER_DB_STATEMENT_H
#define NEXTUI_MUSIC_PLAYER_DB_STATEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <sqlite3.h>

//
// Linear code helper for SQL query results.
//
// Design:
// - The state of the flow is captured in DbStatement.
// - An operation on an errored DbStatement is a no-op.
// - An errored statement keeps the error.
// - The state can always be checked.
//
// This allows user to write a linear code, deferring the error check to
// the very end:
// ```
//   DbStatement statement;
//   DbStatement_begin(...);
//   DbStatement_bind_text(...);
//   DbStatement_step(...);
//   DbStatement_get_int(...);
//   DbStatement_close(...);
//   if (!statement.ok) { print; return error; }
// ```

typedef struct {
    // Indicator whether the result has data that could be read (valid after step only).
    bool has_data;
    // Status flag, false whenever a statement operation encounters an error.
    // All subsequent operations (except close) on the statement are no-op.
    bool ok;
    // \0-terminated error from the failed operation, "label: message", empty if none.
    // The label names the operation, such as "bind 3" or "step".
    char errmsg[256];
    // Rowid of the last INSERT on the connection, obtained when a step completes.
    int last_insert_id;

    // internal
    sqlite3* database;
    sqlite3_stmt* statement;
} DbStatement;

// Lifetime management
bool DbStatement_begin(DbStatement* statement, sqlite3* database, const char* sql);
bool DbStatement_close(DbStatement* statement);

// Data binding. Values are copied by sqlite and safe to free after the bind call.

typedef enum {
    DB_ARG_NULL,
    DB_ARG_INT,
    DB_ARG_TEXT,
} DbArgType;

// One value to bind, of the type that its macro gives.
typedef struct {
    DbArgType   type;
    int         int_value;
    const char* text_value;
} DbArg;

#define INT_ARG(value)  \
    ((DbArg){ .type = DB_ARG_INT, .int_value = (value) })
#define TEXT_ARG(value) \
    ((DbArg){ .type = DB_ARG_TEXT, .text_value = (value) })
#define NULL_ARG \
    ((DbArg){ .type = DB_ARG_NULL })

// Get the number of bindable parameters of the statement's SQL.
int DbStatement_parameter_count(DbStatement* statement);

// Binds one value at an index.
bool DbStatement_bind(DbStatement* statement, int index, DbArg arg);

static inline bool DbStatement_bind_text(DbStatement* statement, int index,
                                         const char* value) {
    return DbStatement_bind(statement, index, TEXT_ARG(value));
}

static inline bool DbStatement_bind_int(DbStatement* statement, int index, int value) {
    return DbStatement_bind(statement, index, INT_ARG(value));
}

static inline bool DbStatement_bind_null(DbStatement* statement, int index) {
    return DbStatement_bind(statement, index, NULL_ARG);
}

// Execute the statement or tries to get the next result.
// Returns true when there are data to read.
bool DbStatement_step(DbStatement* statement);

// Data readers (non-nullable, use for NOT NULL columns).
// Return a value (normal case).
// Return 0/NULL and mark statement failed in case of error or if the column is NULL.
const char* DbStatement_get_text(DbStatement* statement, int column);
char* DbStatement_dup_text(DbStatement* statement, int column);
int DbStatement_get_int(DbStatement* statement, int column);

// Data readers (nullable, use for columns that can carry NULL).
// Return a value, or default_value/NULL for a NULL column or when there is no row.
// Return default_value/NULL and mark statement failed in case of other errors.
const char* DbStatement_get_text_or_null(DbStatement* statement, int column);
char* DbStatement_dup_text_or_null(DbStatement* statement, int column);
int DbStatement_get_int_or(DbStatement* statement, int column, int default_value);

// Helper function to do begin/step/close cycle
// Give one DbArg for each parameter of the SQL, in order.
bool DbStatement_exec(DbStatement* statement, sqlite3* database, const char* sql, ...);

// Runs begin/bind/step/close for a query that returns one integer. Give one DbArg for each
// parameter of the SQL, in order. Returns the value of the first column of the first row. Marks
// the statement failed and returns 0 when there is no row or the value is NULL.
int DbStatement_query_int(DbStatement* statement, sqlite3* database, const char* sql, ...);

// As DbStatement_query_int, but returns default_value when there is no row or the value is
// NULL, and leaves the statement ok. Returns default_value when the query failed.
int DbStatement_query_int_or(DbStatement* statement, sqlite3* database, int default_value,
                             const char* sql, ...);

// Helper functions to add external operations to DbStatement flow

// Marks the statement failed with a printf-style message, as a failed SQLite call does. Keeps
// the first error when the statement already failed.
void DbStatement_mark_failed(DbStatement* statement, const char* format, ...)
    __attribute__((format(printf, 2, 3)));

// Returns zeroed memory, or NULL in case of error.
void* DbStatement_calloc(DbStatement* statement, size_t size);

// Returns a copy of a string, or NULL for a NULL string or in case of error.
char* DbStatement_strdup(DbStatement* statement, const char* value);

#endif
