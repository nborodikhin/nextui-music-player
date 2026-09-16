#include "db_schema.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "db.h"
#include "file_utils.h"

typedef struct {
    char settings_file_path[512];
    bool settings_file_exists;
    char spectrum_settings_file_path[512];
    bool spectrum_settings_file_exists;
} LegacySettings;

// return: whether both legacy file paths could be prepared
static bool prepare_legacy_settings(LegacySettings *settings) {
    int length = userdata_snpath("settings.cfg", settings->settings_file_path,
                                 sizeof(settings->settings_file_path));
    if (length < 0 || (size_t) length >= sizeof(settings->settings_file_path)) {
        return false;
    }

    settings->settings_file_exists =
            access(settings->settings_file_path, F_OK) == 0;

    const char *old_spectrum_settings_file = SHARED_USERDATA_PATH "/spectrum_settings.txt";

    length = snprintf(settings->spectrum_settings_file_path,
                      sizeof(settings->spectrum_settings_file_path), "%s",
                      old_spectrum_settings_file);
    if (length < 0 || (size_t) length >= sizeof(settings->spectrum_settings_file_path)) {
        return false;
    }

    settings->spectrum_settings_file_exists =
            access(settings->spectrum_settings_file_path, F_OK) == 0;
    return true;
}

static bool value_is_in(const int *values, int count, int value) {
    for (int index = 0; index < count; index++) {
        if (values[index] == value) return true;
    }
    return false;
}

static bool copy_settings_data(void) {
    LegacySettings legacy_settings;
    if (!prepare_legacy_settings(&legacy_settings)) {
        return true;
    }
    if (!legacy_settings.settings_file_exists) {
        return true;
    }

    static const int screen_off_values[] = {60, 90, 120, 0};
    static const int screen_off_value_count = 4;
    static const int bass_filter_values[] = {0, 80, 100, 120, 150, 200};
    static const int bass_filter_value_count = 6;
    static const int soft_limiter_value_count = 4;

    bool success = true;

    FILE *file = fopen(legacy_settings.settings_file_path, "r");
    if (file) {
        char line[256];
        int value;
        while (fgets(line, sizeof(line), file)) {
            if (sscanf(line, "screen_off_timeout=%d", &value) == 1 &&
                value_is_in(screen_off_values, screen_off_value_count, value)) {
                success = success && Db_saveIntSetting("screen_off_timeout", value);
            } else if (sscanf(line, "lyrics_enabled=%d", &value) == 1 &&
                       (value == 0 || value == 1)) {
                success = success && Db_saveBoolSetting("lyrics_enabled", value != 0);
            } else if (sscanf(line, "bass_filter_hz=%d", &value) == 1 &&
                       value_is_in(bass_filter_values, bass_filter_value_count, value)) {
                success = success && Db_saveIntSetting("bass_filter_hz", value);
            } else if (sscanf(line, "soft_limiter=%d", &value) == 1 &&
                       value >= 0 && value < soft_limiter_value_count) {
                success = success && Db_saveIntSetting("soft_limiter", value);
            } else if (sscanf(line, "auto_update=%d", &value) == 1 &&
                       (value == 0 || value == 1)) {
                success = success && Db_saveBoolSetting("auto_update", value != 0);
            }
        }
        fclose(file);
    }
    return success;
}

static bool remove_settings_file(void) {
    LegacySettings legacy_settings;
    if (!prepare_legacy_settings(&legacy_settings)) {
        return true;
    }
    if (!legacy_settings.settings_file_exists) {
        return true;
    }

    if (remove(legacy_settings.settings_file_path) != 0 && errno != ENOENT) {
        LOG_error("[Db] failed to remove settings file: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static bool copy_spectrum_settings_data(void) {
    LegacySettings legacy_settings;
    if (!prepare_legacy_settings(&legacy_settings)) {
        return true;
    }
    if (!legacy_settings.settings_file_exists ||
        !legacy_settings.spectrum_settings_file_exists) {
        // spectrum settings file is off-appdir and could belong to a different app.
        // skip migration except when migrating old settings at the same time.
        return true;
    }

    int spectrum_style = 0;
    int spectrum_visible = 1;
    const int style_count = 4;

    FILE *spectrum_file = fopen(legacy_settings.spectrum_settings_file_path, "r");
    if (!spectrum_file) return true;

    fscanf(spectrum_file, "%d\n%d\n", &spectrum_style, &spectrum_visible);
    fclose(spectrum_file);

    if (spectrum_style < 0 || spectrum_style >= style_count) {
        spectrum_style = 0;
    }
    if (spectrum_visible != 0 && spectrum_visible != 1) {
        spectrum_visible = 1;
    }

    bool success = true;
    success = success && Db_saveIntSetting("spectrum_style", spectrum_style);
    success = success && Db_saveBoolSetting("spectrum_visible", spectrum_visible != 0);
    return success;
}

static bool remove_spectrum_settings_file(void) {
    LegacySettings legacy_settings;
    if (!prepare_legacy_settings(&legacy_settings)) {
        return true;
    }
    if (!legacy_settings.settings_file_exists ||
        !legacy_settings.spectrum_settings_file_exists) {
        // spectrum settings file is off-appdir and could belong to a different app.
        // skip migration except when migrating old settings at the same time.
        return true;
    }
    (void) remove(legacy_settings.spectrum_settings_file_path);
    return true;
}

static DbSchemaAction *migration_plan;
static size_t migration_count;
static bool count_only;

static void migration(DbSchemaAction action) {
    migration_count++;
    if (count_only) return;

    migration_plan[migration_count - 1] = (DbSchemaAction){
        .version = (int) migration_count,
        .type = action.type,
        .sql = action.sql,
        .function = action.function,
        .text = action.text,
    };
}

#define stringify_impl(VALUE) #VALUE
#define stringify(VALUE) stringify_impl(VALUE)

#define sql(TEXT)                         \
    migration((DbSchemaAction){            \
        .type = DB_SCHEMA_SQL,             \
        .sql = (TEXT),                     \
        .text = (TEXT),                    \
    })

#define function(FUNCTION)                \
    migration((DbSchemaAction){            \
        .type = DB_SCHEMA_FUNCTION,       \
        .function = (FUNCTION),           \
        .text = stringify(FUNCTION),       \
    })


static void migrations(void) {
    // 1
    sql("CREATE TABLE settings (name TEXT PRIMARY KEY, type TEXT NOT NULL, value)");
    function(copy_spectrum_settings_data);
    function(remove_spectrum_settings_file);
    function(copy_settings_data);
    function(remove_settings_file);
    // 5
}

DbSchemaAction *DbSchema_getActions(void) {
    count_only = true;
    migration_count = 0;
    migrations();

    size_t action_count = migration_count;
    migration_plan = calloc(action_count + 1, sizeof(*migration_plan));
    if (!migration_plan) {
        return NULL;
    }

    count_only = false;
    migration_count = 0;
    migrations();

    // add sentinel value to mark the end of migrations
    migration_plan[action_count].version = -1;

    DbSchemaAction *actions = migration_plan;
    migration_plan = NULL;
    return actions;
}

#undef function
#undef sql
#undef stringify
#undef stringify_impl
