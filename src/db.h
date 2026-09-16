#ifndef __DB_H__
#define __DB_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct DbResult {
    int data_version;
} DbResult;

/* Initializes the database at the standard app data path. */
bool Db_init(void);
/* Closes the database connections of the process. */
void Db_quit(void);
/* Returns whether the database is open. */
bool Db_isAvailable(void);

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
bool Db_resultIsCurrent(const DbResult* result);

typedef enum {
    DB_SETTING_INT,
    DB_SETTING_BOOL,
    DB_SETTING_STRING,
} DbSettingType;

typedef struct {
    char*         name;
    DbSettingType type;
    int           int_value;
    bool          bool_value;
    char*         string_value;
} DbSetting;

typedef struct {
    DbResult   base;
    DbSetting* items;
    int        count;
} DbSettingsResult;

DbSettingsResult* Db_readSettings(void);
void               Db_freeSettingsResult(DbSettingsResult* result);
bool               Db_saveIntSetting(const char* name, int value);
bool               Db_saveBoolSetting(const char* name, bool value);
bool               Db_saveStringSetting(const char* name, const char* value);

// Test support code
/* Initializes the database at an explicit path for internal tests. */
bool Db_initInternal(const char* path);

#endif
