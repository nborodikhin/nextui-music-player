#include <stdbool.h>
#include <stddef.h>
#include <sqlite3.h>

#include "db_statement.h"

static void set_error(DbStatement* statement, int result, const char* message) {
    statement->result = result;
    statement->error_op = statement->op;
    sqlite3_snprintf(sizeof(statement->errmsg), statement->errmsg, "%s",
                     message ? message : sqlite3_errstr(result));
    statement->ok = false;
}

bool DbStatement_begin(DbStatement *statement, sqlite3 *database, const char *sql) {
    statement->database = database;
    statement->statement = NULL;
    statement->has_more = false;
    statement->result = SQLITE_ERROR;
    statement->op = 0;
    statement->error_op = -1;
    statement->errmsg[0] = '\0';
    statement->ok = false;

    if (!statement->database) {
        set_error(statement, SQLITE_ERROR, "No database");
        return false;
    }
    if (!sql) {
        set_error(statement, SQLITE_MISUSE, "No SQL");
        return false;
    }

    statement->result = sqlite3_prepare_v2(
        statement->database,
        sql, -1,
        &statement->statement, NULL);
    if (statement->result != SQLITE_OK) {
        set_error(statement, statement->result,
                  sqlite3_errmsg(statement->database));
        return false;
    }
    if (!statement->statement) {
        set_error(statement, SQLITE_MISUSE, "SQL contains no statement");
        return false;
    }

    statement->ok = true;
    return statement->ok;
}

bool DbStatement_bind_text(DbStatement *statement, int index, const char *value) {
    statement->op++;
    if (!statement->ok) {
        return false;
    }

    statement->result = sqlite3_bind_text(
        statement->statement, index, value, -1, SQLITE_TRANSIENT
    );

    if (statement->result != SQLITE_OK) {
        set_error(statement, statement->result,
                  sqlite3_errmsg(statement->database));
    }

    return statement->ok;
}

bool DbStatement_bind_int(DbStatement *statement, int index, int value) {
    statement->op++;
    if (!statement->ok) {
        return false;
    }

    statement->result = sqlite3_bind_int(statement->statement, index, value);

    if (statement->result != SQLITE_OK) {
        set_error(statement, statement->result,
                  sqlite3_errmsg(statement->database));
    }

    return statement->ok;
}

bool DbStatement_bind_null(DbStatement *statement, int index) {
    statement->op++;
    if (!statement->ok) {
        return false;
    }

    statement->result = sqlite3_bind_null(statement->statement, index);

    if (statement->result != SQLITE_OK) {
        set_error(statement, statement->result,
                  sqlite3_errmsg(statement->database));
    }

    return statement->ok;
}

const char *DbStatement_get_text(DbStatement *statement, int column) {
    statement->op++;
    if (!statement->ok || !statement->has_more) {
        return NULL;
    }

    return (const char *) sqlite3_column_text(statement->statement, column);
}

int DbStatement_get_int(DbStatement *statement, int column) {
    statement->op++;
    if (!statement->ok || !statement->has_more) {
        return 0;
    }

    return sqlite3_column_int(statement->statement, column);
}

bool DbStatement_step(DbStatement *statement) {
    statement->op++;
    if (!statement->ok) {
        return false;
    }

    statement->result = sqlite3_step(statement->statement);

    if (statement->result == SQLITE_ROW) {
        statement->has_more = true;
    } else if (statement->result == SQLITE_DONE) {
        statement->has_more = false;
    } else {
        set_error(statement, statement->result,
                  sqlite3_errmsg(statement->database));
        statement->has_more = false;
    }

    return statement->has_more;
}

bool DbStatement_close(DbStatement *statement) {
    statement->op++;
    statement->has_more = false;

    sqlite3_stmt* sqlite_statement = statement->statement;
    statement->statement = NULL;
    if (!sqlite_statement) return statement->ok;

    int result = sqlite3_finalize(sqlite_statement);
    if (result != SQLITE_OK && statement->ok) {
        set_error(statement, result, sqlite3_errmsg(statement->database));
    }

    return statement->ok;
}

bool DbStatement_exec(DbStatement* statement, sqlite3* database, const char* sql) {
    if (!DbStatement_begin(statement, database, sql)) {
        DbStatement_close(statement);
        return false;
    }
    DbStatement_step(statement);
    return DbStatement_close(statement);
}
