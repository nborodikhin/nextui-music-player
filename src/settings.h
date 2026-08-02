#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>

// Music Player app-specific settings
// These are separate from the global NextUI settings (CFG_*)

// Initialize settings (loads from file if exists)
void Settings_init(void);

// Cleanup settings (saves and frees resources)
void Settings_quit(void);

// Screen off timeout setting (in seconds)
// Values: 60, 90, 120, 0 (off)
int Settings_getScreenOffTimeout(void);
void Settings_setScreenOffTimeout(int seconds);

// Cycle through screen off timeout values
void Settings_cycleScreenOffNext(void);  // 60 -> 90 -> 120 -> Off -> 60
void Settings_cycleScreenOffPrev(void);  // 60 -> Off -> 120 -> 90 -> 60

// Get display string for current screen off timeout
// Returns: "60s", "90s", "120s", or "Off"
const char* Settings_getScreenOffDisplayStr(void);

// Sleep timer setting (runtime only, in minutes)
// Values: 0 (off), 15, 30, 45, 60, 90, 120
#include <time.h>
time_t Settings_getSleepTimerEnd(void);
void Settings_setSleepTimerMinutes(int minutes);
void Settings_cycleSleepTimerNext(void);
void Settings_cycleSleepTimerPrev(void);
const char* Settings_getSleepTimerDisplayStr(void);

// Lyrics enabled setting
bool Settings_getLyricsEnabled(void);
void Settings_setLyricsEnabled(bool enabled);
void Settings_toggleLyrics(void);

// Lockscreen Controls setting
bool Settings_getLockscreenControls(void);
void Settings_toggleLockscreenControls(void);


// Speaker bass filter (high-pass cutoff in Hz, 0 = off)
int Settings_getBassFilterHz(void);
void Settings_cycleBassFilterNext(void);
void Settings_cycleBassFilterPrev(void);
const char* Settings_getBassFilterDisplayStr(void);

// Speaker soft limiter (0 = off, 1=mild, 2=medium, 3=strong)
int Settings_getSoftLimiter(void);
float Settings_getSoftLimiterThreshold(void);
void Settings_cycleSoftLimiterNext(void);
void Settings_cycleSoftLimiterPrev(void);
const char* Settings_getSoftLimiterDisplayStr(void);

// Save settings to file (auto-called on change)
void Settings_save(void);

#endif
