#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char* name;
    int         fallback;
} IntSetting;

typedef struct {
    const char* name;
    bool        fallback;
} BoolSetting;

typedef struct {
    const char* name;
    const char* fallback;
} StringSetting;

extern const IntSetting   SETTING_SCREEN_OFF_TIMEOUT;
extern const BoolSetting  SETTING_LYRICS_ENABLED;
extern const IntSetting   SETTING_BASS_FILTER_HZ;
extern const IntSetting   SETTING_SOFT_LIMITER;
extern const BoolSetting  SETTING_AUTO_UPDATE;

int   Settings_getInt(const IntSetting* key);
void  Settings_setInt(const IntSetting* key, int value);

bool  Settings_getBool(const BoolSetting* key);
void  Settings_setBool(const BoolSetting* key, bool value);
bool  Settings_toggleBool(const BoolSetting* key);

char* Settings_getString(const StringSetting* key);
int   Settings_getStringInto(const StringSetting* key, char* buf, size_t size);
void  Settings_setString(const StringSetting* key, const char* value);

void Settings_init(void);
void Settings_quit(void);

#endif
