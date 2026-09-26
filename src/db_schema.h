#ifndef DB_SCHEMA_H
#define DB_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <sqlite3.h>

typedef enum {
    DB_MIGRATION_SQL,
    DB_MIGRATION_FUNCTION,
} DbMigrationStepType;

typedef bool (*DbMigrationStepFunction)(sqlite3* database);

typedef struct {
    int                     version;
    DbMigrationStepType     type;
    const char*             sql;
    DbMigrationStepFunction function;
    const char*             text;
} DbMigrationStep;

DbMigrationStep* DbSchema_getSteps(void);

#endif
