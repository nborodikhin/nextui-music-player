#include "db_schema.h"

#define schema_migration(FROM, TO, SQL) \
    {                                      \
        .from_version = (FROM),            \
        .to_version   = (TO),              \
        .sql          = (SQL),             \
    }

const DbSchemaMigration db_migrations[] = {
    schema_migration(0, 1, "CREATE TABLE data_migrations (name TEXT PRIMARY KEY)"),
};

const size_t db_migrations_count = sizeof(db_migrations) / sizeof(db_migrations[0]);

#undef schema_migration
