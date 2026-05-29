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

// Lyrics enabled setting
bool Settings_getLyricsEnabled(void);
void Settings_setLyricsEnabled(bool enabled);
void Settings_toggleLyrics(void);

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

// Collect crash reports (opt-in; default false)
// Controls whether the signal handler writes diagnostic bundles to SD card.
// The setter also notifies the registered listener (see Settings_setCollectCrashReportsListener)
// so the signal handler's atomic flag is kept in sync.
bool Settings_getCollectCrashReports(void);
void Settings_setCollectCrashReports(bool enabled);
void Settings_toggleCollectCrashReports(void);
const char* Settings_getCollectCrashReportsDisplayStr(void);

// Register a listener that is invoked whenever the collect-crash-reports value changes.
// The crash handler registers itself here to keep its in-memory atomic in sync.
// Set to NULL to clear. At most one listener at a time.
void Settings_setCollectCrashReportsListener(void (*listener)(bool enabled));

// Save settings to file (auto-called on change)
void Settings_save(void);

#endif
