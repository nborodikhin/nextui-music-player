#ifndef __CRASH_HANDLER_H__
#define __CRASH_HANDLER_H__

#include <stdbool.h>
#include <stddef.h>

// Crash signal handler — dumps a diagnostic bundle to SD card on SIGSEGV /
// SIGABRT / SIGBUS. See spec/crash-reporting.md.

// Initialize: install signal handlers, pre-allocate bundle path/scratch
// buffers, seed the collection_enabled atomic from
// Settings_getCollectCrashReports(), and register a settings listener so the
// atomic stays in sync when the user toggles the value at runtime.
// Idempotent. Call once after RingLog_init() + Settings_init().
// `app_version` (e.g. from SelfUpdate_getVersion(), which reads
// state/app_version.txt) is recorded in meta.txt; pass NULL/"" to leave it
// "unknown". The string is copied, so the caller need not keep it alive.
void CrashHandler_init(const char* app_version);

// Update the in-memory atomic the signal handler reads. Called by the
// Settings listener whenever Settings_setCollectCrashReports(...) runs.
// Async-signal-safe via __atomic_store_n.
void CrashHandler_setCollectionEnabled(bool enabled);

// Find the newest non-skipped crash bundle on disk.
// Returns true iff:
//   - Settings_getCollectCrashReports() is true, AND
//   - at least one subdirectory exists under crash-reports/, AND
//   - the newest such subdirectory (by name; timestamps sort lexicographically)
//     does NOT contain a skipped.txt file.
// On true, out_path (if non-NULL) is filled with the absolute path of that bundle.
// Older non-skipped bundles do not resurface (newest-only model).
bool CrashHandler_findUnsentBundle(char* out_path, size_t out_size);

// Mark the bundle at bundle_path as skipped by creating an empty skipped.txt
// inside it. Returns 0 on success, -1 on error.
int CrashHandler_skipBundle(const char* bundle_path);

// Scan crash-reports/ for bundle directories that contain screen.bmp but not
// screen.png and convert each one to PNG. Runs on the main thread; safe to call
// after SDL_image is usable. On success the source BMP is unlinked; on failure
// it is preserved untouched. Bundles without screen.bmp are skipped. Returns
// the number of successful conversions (>=0); negative means the directory
// could not be opened (typically because no bundles exist yet).
int CrashHandler_convertPendingScreenshots(void);

// Returns true iff at least one bundle subdirectory exists under crash-reports/.
// Skipped or empty bundles still count — the predicate only asks whether the
// directory has any content to delete.
bool CrashHandler_hasAnyBundle(void);

// Recursively delete every bundle directory under crash-reports/ and the
// crash-reports/ directory itself. Returns 0 on success, -1 if the root could
// not be opened (e.g., already absent). Best-effort: individual file/dir
// removal failures are tolerated so a partial wipe still makes progress.
int CrashHandler_deleteAllBundles(void);

// Write the SD-relative crash-reports path (e.g. ".userdata/shared/music-
// player/crash-reports") into out for display in user-visible UI. Truncates
// to out_size - 1 chars and always NUL-terminates.
void CrashHandler_getBundleRootDisplayPath(char* out, size_t out_size);

#endif
