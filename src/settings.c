#include "settings.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "db.h"

#define SETTINGS_CACHE_CAPACITY 32

const IntSetting SETTING_SCREEN_OFF_TIMEOUT = {
    .name     = "screen_off_timeout",
    .fallback = 60,
};

const BoolSetting SETTING_LYRICS_ENABLED = {
    .name     = "lyrics_enabled",
    .fallback = true,
};

const IntSetting SETTING_BASS_FILTER_HZ = {
    .name     = "bass_filter_hz",
    .fallback = 120,
};

const IntSetting SETTING_SOFT_LIMITER = {
    .name     = "soft_limiter",
    .fallback = 2,
};

const BoolSetting SETTING_AUTO_UPDATE = {
    .name     = "auto_update",
    .fallback = true,
};

typedef struct {
    const char*   name;
    DbSettingType type;
    int           int_value;
    bool          bool_value;
    char*         string_value;
} SettingCacheEntry;

static SettingCacheEntry cache[SETTINGS_CACHE_CAPACITY];
static int cache_count;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static int find_entry(const char* name) {
    for (int index = 0; index < cache_count; index++) {
        if (strcmp(cache[index].name, name) == 0) return index;
    }
    return -1;
}

static bool save_entry(const SettingCacheEntry* source) {
    int index = find_entry(source->name);
    bool new_entry = index < 0;
    if (new_entry && cache_count >= SETTINGS_CACHE_CAPACITY) {
        LOG_error("[Settings] cache is full for %s\n", source->name);
        return false;
    }

    char* name = NULL;
    if (new_entry) {
        name = strdup(source->name);
        if (!name) {
            LOG_error("[Settings] failed to copy key %s\n", source->name);
            return false;
        }
    }

    char* string_value = NULL;
    if (source->type == DB_SETTING_STRING) {
        string_value = strdup(source->string_value ? source->string_value : "");
        if (!string_value) {
            free(name);
            LOG_error("[Settings] failed to copy value for %s\n", source->name);
            return false;
        }
    }

    if (new_entry) {
        index = cache_count++;
        cache[index] = (SettingCacheEntry){
            .name = name,
        };
    }

    SettingCacheEntry* entry = &cache[index];
    entry->type = source->type;
    entry->int_value = source->int_value;
    entry->bool_value = source->bool_value;
    free(entry->string_value);
    entry->string_value = string_value;
    return true;
}

static void clear_cache(void) {
    for (int index = 0; index < cache_count; index++) {
        free((void*)cache[index].name);
        free(cache[index].string_value);
    }
    memset(cache, 0, sizeof(cache));
    cache_count = 0;
}

static int get_int(const IntSetting* key) {
    int index = find_entry(key->name);
    return index >= 0 && cache[index].type == DB_SETTING_INT
               ? cache[index].int_value
               : key->fallback;
}

static bool get_bool(const BoolSetting* key) {
    int index = find_entry(key->name);
    return index >= 0 && cache[index].type == DB_SETTING_BOOL
               ? cache[index].bool_value
               : key->fallback;
}

static const char* get_string(const StringSetting* key) {
    int index = key->name ? find_entry(key->name) : -1;
    return index >= 0 && cache[index].type == DB_SETTING_STRING
               ? cache[index].string_value
               : key->fallback;
}

static bool set_int(const char* name, int value) {
    int index = find_entry(name);
    if (index >= 0 && cache[index].type == DB_SETTING_INT &&
        cache[index].int_value == value) {
        return false;
    }

    SettingCacheEntry entry = {
        .name      = name,
        .type      = DB_SETTING_INT,
        .int_value = value,
    };
    return save_entry(&entry);
}

static bool set_bool(const char* name, bool value) {
    int index = find_entry(name);
    if (index >= 0 && cache[index].type == DB_SETTING_BOOL &&
        cache[index].bool_value == value) {
        return false;
    }

    SettingCacheEntry entry = {
        .name       = name,
        .type       = DB_SETTING_BOOL,
        .bool_value = value,
    };
    return save_entry(&entry);
}

static bool set_string(const char* name, const char* value) {
    int index = find_entry(name);
    if (index >= 0 && cache[index].type == DB_SETTING_STRING &&
        cache[index].string_value && strcmp(cache[index].string_value, value) == 0) {
        return false;
    }

    SettingCacheEntry entry = {
        .name         = name,
        .type         = DB_SETTING_STRING,
        .string_value = (char*)value,
    };
    return save_entry(&entry);
}

void Settings_init(void) {
    pthread_mutex_lock(&cache_mutex);
    clear_cache();
    pthread_mutex_unlock(&cache_mutex);

    DbSettingsResult* result = Db_readSettings();
    if (!result) return;

    pthread_mutex_lock(&cache_mutex);
    for (int index = 0; index < result->count; index++) {
        const DbSetting* setting = &result->items[index];
        SettingCacheEntry entry = {
            .name         = setting->name,
            .type         = setting->type,
            .int_value    = setting->int_value,
            .bool_value   = setting->bool_value,
            .string_value = setting->string_value,
        };
        save_entry(&entry);
    }
    pthread_mutex_unlock(&cache_mutex);

    Db_freeSettingsResult(result);
}

void Settings_quit(void) {
    pthread_mutex_lock(&cache_mutex);
    clear_cache();
    pthread_mutex_unlock(&cache_mutex);
}

int Settings_getInt(const IntSetting* key) {
    if (!key || !key->name) return 0;

    pthread_mutex_lock(&cache_mutex);
    int value = get_int(key);
    pthread_mutex_unlock(&cache_mutex);
    return value;
}

void Settings_setInt(const IntSetting* key, int value) {
    if (!key || !key->name) return;

    pthread_mutex_lock(&cache_mutex);
    bool updated = set_int(key->name, value);
    pthread_mutex_unlock(&cache_mutex);

    if (updated) Db_saveIntSetting(key->name, value);
}

bool Settings_getBool(const BoolSetting* key) {
    if (!key || !key->name) return false;

    pthread_mutex_lock(&cache_mutex);
    bool value = get_bool(key);
    pthread_mutex_unlock(&cache_mutex);
    return value;
}

void Settings_setBool(const BoolSetting* key, bool value) {
    if (!key || !key->name) return;

    pthread_mutex_lock(&cache_mutex);
    bool updated = set_bool(key->name, value);
    pthread_mutex_unlock(&cache_mutex);

    if (updated) Db_saveBoolSetting(key->name, value);
}

bool Settings_toggleBool(const BoolSetting* key) {
    if (!key || !key->name) return false;

    pthread_mutex_lock(&cache_mutex);
    bool current = get_bool(key);
    bool value = !current;
    bool updated = set_bool(key->name, value);
    pthread_mutex_unlock(&cache_mutex);

    if (updated) Db_saveBoolSetting(key->name, value);
    return value;
}

char* Settings_getString(const StringSetting* key) {
    if (!key) return NULL;

    pthread_mutex_lock(&cache_mutex);
    const char* value = get_string(key);
    pthread_mutex_unlock(&cache_mutex);
    return strdup(value ? value : "");
}

int Settings_getStringInto(const StringSetting* key, char* buf, size_t size) {
    if (!key || (!buf && size > 0)) return -1;

    pthread_mutex_lock(&cache_mutex);
    const char* value = get_string(key);
    int required = snprintf(buf, size, "%s", value ? value : "");
    pthread_mutex_unlock(&cache_mutex);
    return required;
}

void Settings_setString(const StringSetting* key, const char* value) {
    if (!key || !key->name || !value) return;

    pthread_mutex_lock(&cache_mutex);
    bool updated = set_string(key->name, value);
    pthread_mutex_unlock(&cache_mutex);

    if (updated) Db_saveStringSetting(key->name, value);
}
