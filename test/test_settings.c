#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#include "file_utils.h"
#include "settings.h"
#include "test.h"

static char temp_dir[512];
static char database_path[1024];

void LOG_note(int level, const char* format, ...) {
    (void)level;
    (void)format;
}

static const IntSetting test_int = {
    .name     = "test_int",
    .fallback = 7,
};

static const BoolSetting test_bool = {
    .name     = "test_bool",
    .fallback = true,
};

static const StringSetting test_string = {
    .name     = "test_string",
    .fallback = "fallback",
};

static bool start_test(void) {
    if (!mk_tempdir("settings-test", temp_dir, sizeof(temp_dir))) return false;
    snprintf(database_path, sizeof(database_path), "%s/music-player.db", temp_dir);
    return Db_initInternal(database_path);
}

static void stop_test(void) {
    Settings_quit();
    Db_quit();
    rm_rf(temp_dir);
    temp_dir[0] = '\0';
    database_path[0] = '\0';
}

static const DbSetting* find_setting(const DbSettingsResult* result, const char* name) {
    for (int index = 0; result && index < result->count; index++) {
        if (strcmp(result->items[index].name, name) == 0) {
            return &result->items[index];
        }
    }
    return NULL;
}

TEST(empty_table_returns_fallbacks) {
    CHECK(start_test());
    Settings_init();

    CHECK_EQ_INT(Settings_getInt(&SETTING_SCREEN_OFF_TIMEOUT), 60);
    CHECK(Settings_getBool(&SETTING_LYRICS_ENABLED));
    CHECK_EQ_INT(Settings_getInt(&SETTING_BASS_FILTER_HZ), 120);
    CHECK_EQ_INT(Settings_getInt(&SETTING_SOFT_LIMITER), 2);
    CHECK(Settings_getBool(&SETTING_AUTO_UPDATE));
    CHECK_EQ_INT(Settings_getInt(&test_int), 7);
    CHECK(Settings_getBool(&test_bool));
    char* string = Settings_getString(&test_string);
    CHECK(string != NULL);
    CHECK(string && strcmp(string, "fallback") == 0);
    free(string);

    DbSettingsResult* result = Db_readSettings();
    CHECK(result != NULL);
    CHECK(result && result->count == 0);
    Db_freeSettingsResult(result);
    stop_test();
}

static int thread_int_value;

static void* read_int_worker(void* unused) {
    (void)unused;
    thread_int_value = Settings_getInt(&test_int);
    return NULL;
}

TEST(setter_reaches_all_threads) {
    CHECK(start_test());
    Settings_init();
    Settings_setInt(&test_int, 42);
    CHECK_EQ_INT(Settings_getInt(&test_int), 42);

    pthread_t worker;
    thread_int_value = 0;
    CHECK(pthread_create(&worker, NULL, read_int_worker, NULL) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK_EQ_INT(thread_int_value, 42);

    DbSettingsResult* result = Db_readSettings();
    const DbSetting* setting = find_setting(result, "test_int");
    CHECK(setting != NULL);
    CHECK(setting && setting->type == DB_SETTING_INT && setting->int_value == 42);
    Db_freeSettingsResult(result);
    stop_test();
}

TEST(zero_int_is_stored) {
    CHECK(start_test());
    Settings_init();
    Settings_setInt(&test_int, 0);

    DbSettingsResult* result = Db_readSettings();
    const DbSetting* setting = find_setting(result, "test_int");
    CHECK(setting != NULL);
    CHECK(setting && setting->type == DB_SETTING_INT && setting->int_value == 0);
    Db_freeSettingsResult(result);
    stop_test();
}

TEST(equal_value_does_not_write) {
    CHECK(start_test());
    Settings_init();
    Settings_setInt(&test_int, 42);
    int before = Db_dataVersion();
    Settings_setInt(&test_int, 42);
    CHECK_EQ_INT(Db_dataVersion(), before);
    stop_test();
}

TEST(toggle_writes_opposite_of_fallback) {
    CHECK(start_test());
    Settings_init();
    CHECK(!Settings_toggleBool(&test_bool));
    CHECK(!Settings_getBool(&test_bool));

    DbSettingsResult* result = Db_readSettings();
    const DbSetting* setting = find_setting(result, "test_bool");
    CHECK(setting != NULL);
    CHECK(setting && setting->type == DB_SETTING_BOOL && !setting->bool_value);
    Db_freeSettingsResult(result);
    stop_test();
}

TEST(string_copy_survives_set) {
    CHECK(start_test());
    Settings_init();
    Settings_setString(&test_string, "before");
    char* copy = Settings_getString(&test_string);
    CHECK(copy != NULL);
    Settings_setString(&test_string, "after");
    CHECK(copy && strcmp(copy, "before") == 0);
    free(copy);
    stop_test();
}

TEST(string_buffer_returns_required_count) {
    CHECK(start_test());
    Settings_init();
    Settings_setString(&test_string, "complete value");

    char buffer[5] = "";
    CHECK_EQ_INT(Settings_getStringInto(&test_string, buffer, sizeof(buffer)), 14);
    CHECK(strcmp(buffer, "comp") == 0);
    stop_test();
}

TEST(second_init_reads_stored_values) {
    CHECK(start_test());
    Settings_init();
    Settings_setInt(&test_int, 42);
    Settings_setBool(&test_bool, false);
    Settings_setString(&test_string, "stored");

    Settings_init();
    CHECK_EQ_INT(Settings_getInt(&test_int), 42);
    CHECK(!Settings_getBool(&test_bool));
    char* string = Settings_getString(&test_string);
    CHECK(string && strcmp(string, "stored") == 0);
    free(string);
    stop_test();
}

TEST(unknown_type_is_skipped) {
    CHECK(start_test());
    CHECK(Db_execute(
        "INSERT INTO settings (name, type, value) VALUES ('unknown', 'other', 1)"));
    Settings_init();

    static const IntSetting unknown = {
        .name     = "unknown",
        .fallback = 99,
    };
    CHECK_EQ_INT(Settings_getInt(&unknown), 99);
    stop_test();
}

int main(void) {
    RUN(empty_table_returns_fallbacks);
    RUN(setter_reaches_all_threads);
    RUN(zero_int_is_stored);
    RUN(equal_value_does_not_write);
    RUN(toggle_writes_opposite_of_fallback);
    RUN(string_copy_survives_set);
    RUN(string_buffer_returns_required_count);
    RUN(second_init_reads_stored_values);
    RUN(unknown_type_is_skipped);
    return test_summary();
}
