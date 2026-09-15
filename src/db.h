#ifndef __DB_H__
#define __DB_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct DbResult {
    void (*destroy)(struct DbResult*);
    int data_version;
} DbResult;

/* Initializes the database at the standard app data path. */
bool Db_init(void);
/* Closes the database connections of the process. */
void Db_quit(void);

/* Starts a transaction on the calling thread. */
bool Db_begin(void);
/* Commits the transaction on the calling thread. */
bool Db_commit(void);
/* Rolls back the transaction on the calling thread. */
bool Db_rollback(void);
/* Executes SQL without returning rows. */
bool Db_execute(const char* sql);

/* Returns whether a data migration is marked as done. */
bool Db_dataMigrationIsDone(const char* name);
/* Marks a data migration as done. */
bool Db_markDataMigrationDone(const char* name);

/* Returns the current process data version. */
int Db_dataVersion(void);
/* Returns whether a result has the current data version. */
bool Db_resultIsCurrent(const void* result);
/* Frees a result returned by a database query. */
void Db_freeResult(void* result);

// Temp example code

typedef struct {
    DbResult base;
    char* value;
} DbScratchResult;
bool Db_scratchSave(const char* key, const char* value);
DbScratchResult* Db_scratchRead(const char* key);

// Test support code
/* Initializes the database at an explicit path for internal tests. */
bool Db_initInternal(const char* path);

#endif
