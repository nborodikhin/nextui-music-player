// crash_handler.c — async-signal-safe diagnostic bundle writer.
//
// Layout per spec/crash-reporting.md:
//
//   <SHARED_USERDATA_PATH>/music-player/crash-reports/<yyyy-mm-dd_HH-MM-SS>/
//       log.txt    — ring buffer dump
//       screen.bmp — framebuffer snapshot (signal-safe), converted to
//                    screen.png at next startup
//       meta.txt   — environment metadata
//
// SIGUSR1 produces the same bundle layout as a real crash (only the signal
// name in meta.txt differs); the process keeps running afterward. From the
// menu and report-recipient's point of view, USR1 dumps and fatal-signal
// crashes are interchangeable.
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
// The handler short-circuits to _exit(1) (crash) or return (SIGUSR1) if
// collection_enabled is false.

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
#include <SDL2/SDL_image.h>

#include "defines.h"     // SHARED_USERDATA_PATH — must come before api.h
#include "api.h"         // LOG_*
#include "background.h"
#include "fb_capture.h"
#include "player.h"
#include "ring_log.h"
#include "settings.h"
#include "watchdog.h"

#define BUNDLE_ROOT      SHARED_USERDATA_PATH "/music-player/crash-reports"
#define PARENT_DIR       SHARED_USERDATA_PATH "/music-player"

// App version recorded in meta.txt. Captured at CrashHandler_init() from the
// value SelfUpdate already read out of state/app_version.txt. "unknown" until
// init (and if no usable version was passed).
static char app_version[32] = "unknown";

// Pre-allocated buffers — all writable from the handler, never freed.
static char bundle_dir[256];     // BUNDLE_ROOT "/yyyy-mm-dd_HH-MM-SS"
static char log_path[320];       // bundle_dir "/log.txt"
static char meta_path[320];      // bundle_dir "/meta.txt"
static char screen_path[320];    // bundle_dir "/screen.bmp"
static char meta_buf[2048];      // formatted meta.txt content (audio_track path can be ~512 chars)

static atomic_int collection_enabled = 0;
static atomic_int handler_installed = 0;

// App-start tick captured at init so meta.txt can report uptime in ms.
// CLOCK_MONOTONIC ms (not SDL) so the handler stays async-signal-safe.
static uint32_t app_start_ms = 0;

// Async-signal-safe millisecond clock — replaces SDL_GetTicks() in the handler.
static uint32_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static const char* signal_name(int signo) {
    switch (signo) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGUSR1: return "SIGUSR1";
        default:      return "SIGNAL";
    }
}

static const char* player_state_name(PlayerState s) {
    switch (s) {
        case PLAYER_STATE_PLAYING: return "playing";
        case PLAYER_STATE_PAUSED:  return "paused";
        case PLAYER_STATE_STOPPED: return "stopped";
        default:                   return "unknown";
    }
}

static const char* background_name(BackgroundPlayerType t) {
    switch (t) {
        case BG_MUSIC:   return "music";
        case BG_RADIO:   return "radio";
        case BG_PODCAST: return "podcast";
        case BG_NONE:    return "none";
        default:         return "unknown";
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
//
// Audio fields are best-effort racy snapshots — the player getters are simple
// reads with no mutex, so a torn read is possible but won't deadlock or crash.
// PII consideration: audio_track is the raw filesystem path; gated by the same
// "Collect crash reports" opt-in as the rest of the bundle.
static size_t format_meta(int signo) {
    uint32_t now = monotonic_ms();
    uint32_t uptime = now - app_start_ms;
    uint32_t hb = (uint32_t)Watchdog_lastHeartbeatMs();
    uint32_t hb_age = (hb == 0) ? 0 : (now - hb);

    PlayerState ps = Player_getState();
    BackgroundPlayerType bg = Background_getActive();
    int pos_ms = Player_getPosition();
    int dur_ms = Player_getDuration();

    // Snapshot the track path into a local fixed buffer before printing it.
    // The source is a fixed-size, always-NUL-terminated struct field
    // (player.current_file[512]) — never freed heap — so this can't fault on a
    // dangling pointer. The strnlen cap also bounds the read to track_buf size
    // even if a concurrent writer leaves the source momentarily un-terminated,
    // which matters for the SIGUSR1 dump that must return and keep running.
    char track_buf[512];
    const char* track = Player_getCurrentFile();
    if (track) {
        size_t tlen = strnlen(track, sizeof(track_buf) - 1);
        memcpy(track_buf, track, tlen);
        track_buf[tlen] = '\0';
    } else {
        track_buf[0] = '\0';
    }

    int n = snprintf(meta_buf, sizeof(meta_buf),
        "version: %s\n"
        "platform: " PLATFORM "\n"
        "signal: %s (%d)\n"
        "uptime_ms: %u\n"
        "heartbeat_age_ms: %u\n"
        "ring_total_bytes: %zu\n"
        "screen_width: %d\n"
        "screen_height: %d\n"
        "screen_bpp: %d\n"
        "audio_state: %s\n"
        "audio_background: %s\n"
        "audio_position_ms: %d\n"
        "audio_duration_ms: %d\n"
        "audio_track: %s\n",
        app_version,
        signal_name(signo), signo,
        uptime,
        hb_age,
        RingLog_totalAppended(),
        FbCapture_width(),
        FbCapture_height(),
        FbCapture_bpp(),
        player_state_name(ps),
        background_name(bg),
        pos_ms,
        dur_ms,
        track_buf
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

// Shared bundle writer used by both the fatal-signal handler and the SIGUSR1
// diagnostic-dump handler. Order: log.txt → screen.bmp → meta.txt so a partial
// bundle still has the highest-value file first.
static void write_bundle(int signo) {
    mkdir_p(PARENT_DIR);
    mkdir(BUNDLE_ROOT, 0755);
    build_bundle_dir();
    mkdir(bundle_dir, 0755);

    snprintf(log_path, sizeof(log_path), "%s/log.txt", bundle_dir);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd >= 0) {
        RingLog_dump(log_fd);
        close(log_fd);
    }

    // BMP is the signal-safe choice; CrashHandler_convertPendingScreenshots()
    // converts BMP → PNG at next startup once SDL_image is usable.
    snprintf(screen_path, sizeof(screen_path), "%s/screen.bmp", bundle_dir);
    int screen_fd = open(screen_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (screen_fd >= 0) {
        FbCapture_writeBmp(screen_fd);
        close(screen_fd);
    }

    snprintf(meta_path, sizeof(meta_path), "%s/meta.txt", bundle_dir);
    int meta_fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (meta_fd >= 0) {
        size_t meta_len = format_meta(signo);
        write_all(meta_fd, meta_buf, meta_len);
        close(meta_fd);
    }
}

static void crash_handler(int signo) {
    if (!atomic_load_explicit(&collection_enabled, memory_order_relaxed)) {
        _exit(1);
    }
    write_bundle(signo);
    _exit(1);
}

// SIGUSR1 — on-demand dump. Writes the same bundle layout as a real crash and
// then *returns* to whatever the main thread was doing. The bundle is
// indistinguishable from a fatal-signal crash at the filesystem level (only
// `signal: SIGUSR1` in meta.txt reveals the difference); it surfaces in the
// menu as a normal "Send Crash Report" entry. Self-masked at install time so
// a second SIGUSR1 cannot re-enter while a dump is in flight.
static void diagnostic_handler(int signo) {
    if (!atomic_load_explicit(&collection_enabled, memory_order_relaxed)) {
        return;
    }
    write_bundle(signo);
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

int CrashHandler_convertPendingScreenshots(void) {
    DIR* dir = opendir(BUNDLE_ROOT);
    if (!dir) return -1;

    int converted = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char bundle[320];
        snprintf(bundle, sizeof(bundle), "%s/%s", BUNDLE_ROOT, ent->d_name);

        struct stat st;
        if (stat(bundle, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char bmp[384];
        char png[384];
        snprintf(bmp, sizeof(bmp), "%s/screen.bmp", bundle);
        snprintf(png, sizeof(png), "%s/screen.png", bundle);

        if (access(bmp, F_OK) != 0) continue;       // no BMP: incomplete or already converted
        if (access(png, F_OK) == 0) continue;       // already converted

        SDL_Surface* surf = SDL_LoadBMP(bmp);
        if (!surf) {
            LOG_warn("crash_handler: SDL_LoadBMP(%s) failed: %s\n", bmp, SDL_GetError());
            continue;
        }

        int rc = IMG_SavePNG(surf, png);
        SDL_FreeSurface(surf);

        if (rc != 0) {
            LOG_warn("crash_handler: IMG_SavePNG(%s) failed: %s\n", png, IMG_GetError());
            unlink(png);   // partial file may exist
            continue;
        }

        if (unlink(bmp) != 0) {
            LOG_warn("crash_handler: unlink(%s) failed\n", bmp);
        }
        LOG_info("crash_handler: converted screen.bmp → screen.png in %s\n", ent->d_name);
        converted++;
    }
    closedir(dir);
    return converted;
}

bool CrashHandler_hasAnyBundle(void) {
    DIR* dir = opendir(BUNDLE_ROOT);
    if (!dir) return false;

    bool found = false;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[320];
        snprintf(p, sizeof(p), "%s/%s", BUNDLE_ROOT, ent->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

// Walk one bundle directory: unlink every regular file, then rmdir the bundle.
// Best-effort — individual failures don't stop the wipe.
static void delete_bundle_dir(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
            continue;
        }
        char filepath[384];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, ent->d_name);
        unlink(filepath);
    }
    closedir(dir);
    rmdir(path);
}

int CrashHandler_deleteAllBundles(void) {
    DIR* dir = opendir(BUNDLE_ROOT);
    if (!dir) return -1;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[320];
        snprintf(p, sizeof(p), "%s/%s", BUNDLE_ROOT, ent->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            delete_bundle_dir(p);
        } else {
            unlink(p);
        }
    }
    closedir(dir);
    rmdir(BUNDLE_ROOT);
    return 0;
}

void CrashHandler_getBundleRootDisplayPath(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const char* full = BUNDLE_ROOT;
    const char* userdata = strstr(full, ".userdata/");
    const char* src = userdata ? userdata : full;
    size_t n = strnlen(src, out_size - 1);
    memcpy(out, src, n);
    out[n] = '\0';
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

void CrashHandler_init(const char* version) {
    if (atomic_exchange_explicit(&handler_installed, 1, memory_order_acq_rel)) {
        return;  // already installed
    }

    // Capture the app version for meta.txt. Keep the "unknown" default if the
    // caller passed nothing usable (e.g. state/app_version.txt was unreadable).
    if (version && version[0]) {
        strncpy(app_version, version, sizeof(app_version) - 1);
        app_version[sizeof(app_version) - 1] = '\0';
    }

    app_start_ms = monotonic_ms();

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
    // Block the other fatal signals while handling one, so a secondary fault
    // can't re-enter write_bundle() and clobber the shared bundle_dir/meta_buf
    // scratch mid-write. (The signal being handled is auto-blocked already.)
    sigaddset(&sa.sa_mask, SIGSEGV);
    sigaddset(&sa.sa_mask, SIGABRT);
    sigaddset(&sa.sa_mask, SIGBUS);
    sigaddset(&sa.sa_mask, SIGFPE);
    sigaddset(&sa.sa_mask, SIGILL);
    sa.sa_flags = 0;   // no SA_RESTART — let interrupted syscalls fail naturally

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);

    // SIGUSR1 — diagnostic dump that *returns* to normal execution. Self-mask
    // so a second SIGUSR1 while we're writing cannot re-enter and clobber the
    // shared bundle_dir / meta_buf scratch. SA_RESTART so any syscall the
    // signal interrupts is restarted instead of returning EINTR — the audio
    // thread and the main loop would not all handle EINTR gracefully, and a
    // diagnostic dump must not disrupt normal app execution.
    struct sigaction diag_sa;
    memset(&diag_sa, 0, sizeof(diag_sa));
    diag_sa.sa_handler = diagnostic_handler;
    sigemptyset(&diag_sa.sa_mask);
    sigaddset(&diag_sa.sa_mask, SIGUSR1);
    diag_sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &diag_sa, NULL);
}
