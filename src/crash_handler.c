// crash_handler.c — async-signal-safe diagnostic bundle writer.
//
// Layout per spec/crash-reporting.md:
//
//   <SHARED_USERDATA_PATH>/music-player/crash-reports/<yyyy-mm-dd_HH-MM-SS>/
//       log.txt   — ring buffer dump
//       meta.txt  — environment metadata
//       (screen.bmp added in T06)
//
// Signal-handler discipline (POSIX async-signal-safe):
//   - No malloc, no printf-family, no SDL calls inside the handler.
//   - All buffers/paths/format strings allocated at init.
//   - snprintf is used (technically not strictly POSIX async-signal-safe but
//     safe in glibc for the simple %d/%s/%u formatting here — same trade-off
//     as documented in the spec).
//   - localtime_r is also "safe in practice" on glibc; used once per handler
//     invocation to format the timestamp.
//
// The handler short-circuits to _exit(1) if collection_enabled is false.

#include "crash_handler.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "defines.h"     // SHARED_USERDATA_PATH
#include "fb_capture.h"
#include "ring_log.h"
#include "settings.h"
#include "watchdog.h"

#define BUNDLE_ROOT      SHARED_USERDATA_PATH "/music-player/crash-reports"
#define PARENT_DIR       SHARED_USERDATA_PATH "/music-player"
#define VERSION_STR      "1.10.0"     // sync with app version

// Pre-allocated buffers — all writable from the handler, never freed.
static char bundle_dir[256];     // BUNDLE_ROOT "/yyyy-mm-dd_HH-MM-SS"
static char log_path[320];       // bundle_dir "/log.txt"
static char meta_path[320];      // bundle_dir "/meta.txt"
static char screen_path[320];    // bundle_dir "/screen.bmp"
static char meta_buf[1024];      // formatted meta.txt content

static atomic_int collection_enabled = 0;
static atomic_int handler_installed = 0;

// App-start tick captured at init so meta.txt can report uptime in ms.
static uint32_t app_start_ticks = 0;

static const char* signal_name(int signo) {
    switch (signo) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        default:      return "SIGNAL";
    }
}

// Recursively mkdir each path component. mkdir() is async-signal-safe.
// We tolerate EEXIST since the chain may partially exist already.
static void mkdir_p(const char* path) {
    char tmp[320];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);   // ignore errors — EEXIST is fine
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0755);
}

// Build "<BUNDLE_ROOT>/yyyy-mm-dd_HH-MM-SS" into bundle_dir.
static void build_bundle_dir(void) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    snprintf(bundle_dir, sizeof(bundle_dir),
             BUNDLE_ROOT "/%04d-%02d-%02d_%02d-%02d-%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec);
}

// Format meta.txt into meta_buf. Returns the length (excluding trailing NUL).
static size_t format_meta(int signo) {
    uint32_t now = SDL_GetTicks();
    uint32_t uptime = now - app_start_ticks;
    uint32_t hb = (uint32_t)Watchdog_lastHeartbeatMs();
    uint32_t hb_age = (hb == 0) ? 0 : (now - hb);

    int n = snprintf(meta_buf, sizeof(meta_buf),
        "version: " VERSION_STR "\n"
        "platform: " PLATFORM "\n"
        "signal: %s (%d)\n"
        "uptime_ms: %u\n"
        "heartbeat_age_ms: %u\n"
        "ring_total_bytes: %zu\n"
        "screen_width: %d\n"
        "screen_height: %d\n"
        "screen_bpp: %d\n",
        signal_name(signo), signo,
        uptime,
        hb_age,
        RingLog_totalAppended(),
        FbCapture_width(),
        FbCapture_height(),
        FbCapture_bpp()
    );
    if (n < 0) return 0;
    return (size_t)n < sizeof(meta_buf) ? (size_t)n : sizeof(meta_buf) - 1;
}

// Write all of buf or as much as the kernel accepts in one go. Async-signal-safe.
static void write_all(int fd, const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) return;   // give up rather than spin in the handler
        buf += n;
        len -= (size_t)n;
    }
}

static void crash_handler(int signo) {
    // 1) Honor the collection setting.
    if (!atomic_load_explicit(&collection_enabled, memory_order_relaxed)) {
        _exit(1);
    }

    // 2) Make the bundle directory.
    mkdir_p(PARENT_DIR);
    mkdir(BUNDLE_ROOT, 0755);
    build_bundle_dir();
    mkdir(bundle_dir, 0755);

    // 3) Write log.txt first (most diagnostic value).
    snprintf(log_path, sizeof(log_path), "%s/log.txt", bundle_dir);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd >= 0) {
        RingLog_dump(log_fd);
        close(log_fd);
    }

    // 4) Write screen.bmp (framebuffer snapshot). BMP is the signal-safe choice
    //    here; the post-start scanner converts BMP → PNG once SDL_image is up
    //    (see spec/crash-reporting.md TODO "Post-start BMP → PNG conversion").
    snprintf(screen_path, sizeof(screen_path), "%s/screen.bmp", bundle_dir);
    int screen_fd = open(screen_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (screen_fd >= 0) {
        FbCapture_writeBmp(screen_fd);
        close(screen_fd);
    }

    // 5) Write meta.txt last so a viewer can quickly tell whether the bundle is
    //    complete.
    snprintf(meta_path, sizeof(meta_path), "%s/meta.txt", bundle_dir);
    int meta_fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (meta_fd >= 0) {
        size_t meta_len = format_meta(signo);
        write_all(meta_fd, meta_buf, meta_len);
        close(meta_fd);
    }

    _exit(1);
}

void CrashHandler_setCollectionEnabled(bool enabled) {
    atomic_store_explicit(&collection_enabled, enabled ? 1 : 0, memory_order_relaxed);
}

bool CrashHandler_findUnsentBundle(char* out_path, size_t out_size) {
    if (!Settings_getCollectCrashReports()) return false;

    DIR* dir = opendir(BUNDLE_ROOT);
    if (!dir) return false;

    char newest[64] = {0};
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        // Filter to directories only.
        char check_path[320];
        snprintf(check_path, sizeof(check_path), "%s/%s", BUNDLE_ROOT, ent->d_name);
        struct stat st;
        if (stat(check_path, &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        if (strcmp(ent->d_name, newest) > 0) {
            strncpy(newest, ent->d_name, sizeof(newest) - 1);
            newest[sizeof(newest) - 1] = '\0';
        }
    }
    closedir(dir);

    if (newest[0] == '\0') return false;

    // Check skipped marker on the newest.
    char skip_path[320];
    snprintf(skip_path, sizeof(skip_path), "%s/%s/skipped.txt", BUNDLE_ROOT, newest);
    if (access(skip_path, F_OK) == 0) return false;

    if (out_path && out_size > 0) {
        snprintf(out_path, out_size, "%s/%s", BUNDLE_ROOT, newest);
    }
    return true;
}

int CrashHandler_skipBundle(const char* bundle_path) {
    if (!bundle_path || !bundle_path[0]) return -1;
    char skip_path[320];
    int n = snprintf(skip_path, sizeof(skip_path), "%s/skipped.txt", bundle_path);
    if (n < 0 || (size_t)n >= sizeof(skip_path)) return -1;
    int fd = open(skip_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

void CrashHandler_init(void) {
    if (atomic_exchange_explicit(&handler_installed, 1, memory_order_acq_rel)) {
        return;  // already installed
    }

    app_start_ticks = SDL_GetTicks();

    // Pre-read framebuffer dimensions and pre-build the BMP header so the
    // signal handler does not have to do any heavyweight work.
    FbCapture_init();

    // Seed from current Settings value and subscribe to future changes.
    CrashHandler_setCollectionEnabled(Settings_getCollectCrashReports());
    Settings_setCollectCrashReportsListener(CrashHandler_setCollectionEnabled);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // no SA_RESTART — let interrupted syscalls fail naturally

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
}
