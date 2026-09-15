#ifndef NEXTUI_MUSIC_PLAYER_DB_STATEMENT_H
#define NEXTUI_MUSIC_PLAYER_DB_STATEMENT_H

#include <stdbool.h>
#include <sqlite3.h>

typedef struct {
    // Indicator whether the result has data that could be (valid after step only).
    bool has_data;
    // Status flag, false whenever a statement operation encounters an error.
    // All subsequent operations (except close) on the statement are no-op.
    bool ok;
    // Sqlite result code - SQLITE_OK or the result from the failed operation.
    int result;
    // Number of operations performed (including begin).
    int op;
    // The number of failed operation (negative if neither failed).
    int error_op;
    // \0-terminated error from the last failed operation, empty if none.
    char errmsg[256];

    // internal
    sqlite3* database;
    sqlite3_stmt* statement;
} DbStatement;

// Lifetime management
bool DbStatement_begin(DbStatement* statement, sqlite3* database, const char* sql);
bool DbStatement_close(DbStatement* statement);

// Data binding. Values are copied by sqlite and safe to free after the bind call.
bool DbStatement_bind_text(DbStatement* statement, int index, const char* value);
bool DbStatement_bind_int(DbStatement* statement, int index, int value);
bool DbStatement_bind_null(DbStatement* statement, int index);

// Execute the statement or tries to get the next result.
// Returns true when there are data to read.
bool DbStatement_step(DbStatement* statement);

// Data readers, available only when statement has the data to read
const char* DbStatement_get_text(DbStatement* statement, int column);
char* DbStatement_dup_ext(DbStatement* statement, int column);
int DbStatement_get_int(DbStatement* statement, int column);

// Helper function to do begin/step/close cycle
bool DbStatement_exec(DbStatement* statement, sqlite3* database, const char* sql);

#endif
