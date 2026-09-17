#ifndef DB_SCHEMA_H
#define DB_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DB_SCHEMA_SQL,
    DB_SCHEMA_FUNCTION,
} DbSchemaActionType;

typedef bool (*DbSchemaActionFunction)(void);

typedef struct {
    int                    version;
    DbSchemaActionType     type;
    const char*            sql;
    DbSchemaActionFunction function;
    const char*            text;
} DbSchemaAction;

DbSchemaAction* DbSchema_getActions(void);

#endif
