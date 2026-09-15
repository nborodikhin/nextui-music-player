#ifndef DB_SCHEMA_H
#define DB_SCHEMA_H

#include <stddef.h>

typedef struct {
    int from_version;
    int to_version;
    const char* sql;
} DbSchemaMigration;

extern const DbSchemaMigration db_migrations[];
extern const size_t db_migrations_count;

#endif
