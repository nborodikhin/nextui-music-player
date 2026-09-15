#ifndef NEXTUI_MUSIC_PLAYER_DB_STATEMENT_H
#define NEXTUI_MUSIC_PLAYER_DB_STATEMENT_H

#include <stdbool.h>
#include <sqlite3.h>

typedef struct {
    bool has_more;
    bool ok;
    sqlite3* database;
    sqlite3_stmt* statement;
    int result;
    int op;
    int error_op;
    char errmsg[256];
} DbStatement;

/* Prepares one SQL statement in a new or closed wrapper. */
bool DbStatement_begin(DbStatement* statement, sqlite3* database, const char* sql);
/* Binds a string and copies it before the next call. */
bool DbStatement_bind_text(DbStatement* statement, int index, const char* value);
/* Binds an integer. */
bool DbStatement_bind_int(DbStatement* statement, int index, int value);
/* Binds a SQL NULL. */
bool DbStatement_bind_null(DbStatement* statement, int index);
/* Returns text in the current row until the next step or close. */
const char* DbStatement_get_text(DbStatement* statement, int column);
/* Returns the integer in the current row. */
int DbStatement_get_int(DbStatement* statement, int column);
/* Steps the statement and returns true when a row is available. */
bool DbStatement_step(DbStatement* statement);
/* Finalizes the statement and returns whether all operations succeeded. */
bool DbStatement_close(DbStatement* statement);

/* Executes one SQL statement without returning rows. */
bool DbStatement_exec(DbStatement* statement, sqlite3* database, const char* sql);

#endif
