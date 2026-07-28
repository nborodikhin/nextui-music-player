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
//   - NO localtime_r. It takes tzset_lock and mallocs on first call. glibc's
//     malloc corruption detector calls abort() *while holding the arena lock*,
//     so a SIGABRT handler that allocates deadlocks against it — precisely the
//     heap-corruption case where the bundle matters most. Bundle directories
//     are named in UTC via time(2) (which IS async-signal-safe) plus the
//     integer civil-time conversion in utc_time.c.
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
#include "log_trace.h"   // LogTrace_uptimeMs / LogTrace_setCaptureEnabled (after api.h)
#include "background.h"
#include "crash_meta.h"
#include "fb_capture.h"
#include "player.h"
#include "ring_log.h"
#include "settings.h"
#include "utc_time.h"
#include "watchdog.h"

#define BUNDLE_ROOT_DEFAULT  SHARED_USERDATA_PATH "/music-player/crash-reports"

// Root directory holding every crash bundle. A mutable file-static rather than a
// macro so host tests can retarget it at a temp dir (see
// CrashHandler_setBundleRootForTesting) — that is the only supported override.
//
// Read from the signal handler, so it must never be written once handlers are
// installed; the setter enforces that. Sized so bundle_dir[256] can always hold
// "<bundle_root>/yyyy-mm-dd_HH-MM-SS" without truncation.
#define BUNDLE_ROOT_MAX 160
static char bundle_root[BUNDLE_ROOT_MAX] = BUNDLE_ROOT_DEFAULT;

// "<bundle_root>/<dirent name>" — bundle_root is a runtime value now, so the
// path buffers are sized against its declared maximum plus NAME_MAX rather than
// against the (much shorter) compile-time default.
#define BUNDLE_PATH_MAX (BUNDLE_ROOT_MAX + 1 + 255 + 1)
// The same, plus room for a "/screen.bmp"-sized leaf.
#define BUNDLE_FILE_MAX (BUNDLE_PATH_MAX + 32)

// App version recorded in meta.txt. Captured at CrashHandler_init() from the
// value SelfUpdate already read out of state/app_version.txt. "unknown" until
// init (and if no usable version was passed).
static char app_version[32] = "unknown";

// Pre-allocated buffers — all writable from the handler, never freed.
static char bundle_dir[256];     // bundle_root "/yyyy-mm-dd_HH-MM-SS"
static char log_path[320];       // bundle_dir "/log.txt"
static char meta_path[320];      // bundle_dir "/meta.txt"
static char screen_path[320];    // bundle_dir "/screen.bmp"
static char meta_buf[2048];      // formatted meta.txt content (audio_track path can be ~512 chars)

static atomic_int collection_enabled = 0;
static atomic_int handler_installed = 0;

// Last button press observed by the main loop, for meta.txt. Written by
// ModuleCommon_traceButtons() via CrashHandler_noteInput(); read best-effort
// from the handler. A torn read is possible but bounded — format_meta() copies
// with a strnlen cap, the same trade-off documented for audio_track below.
static char last_input_button[24] = "";
static _Atomic uint32_t last_input_ms = 0;

// Async-signal-safe millisecond clock — replaces SDL_GetTicks() in the handler.
static uint32_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

bool CrashHandler_setBundleRootForTesting(const char* path) {
    // Refuse once handlers are live: bundle_root is read from the signal
    // handler, and a concurrent write would be a data race in exactly the
    // context that cannot tolerate one.
    if (atomic_load_explicit(&handler_installed, memory_order_acquire)) return false;
    if (!path || !path[0]) return false;

    size_t n = strnlen(path, sizeof(bundle_root));
    if (n >= sizeof(bundle_root)) return false;   // would truncate — reject loudly

    memcpy(bundle_root, path, n);
    bundle_root[n] = '\0';
    return true;
}

void CrashHandler_noteInput(const char* label) {
    if (!label || !label[0]) return;
    size_t n = strnlen(label, sizeof(last_input_button) - 1);
    memcpy(last_input_button, label, n);
    last_input_button[n] = '\0';
    atomic_store_explicit(&last_input_ms, monotonic_ms(), memory_order_relaxed);
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
//
// Sized to BUNDLE_ROOT_MAX because bundle_root is the only thing ever passed
// here — keeping the strnlen bound at or below the source size is what stops
// -Wstringop-overread from firing.
static void mkdir_p(const char* path) {
    char tmp[BUNDLE_ROOT_MAX];
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

// Build "<bundle_root>/yyyy-mm-dd_HH-MM-SS" into bundle_dir, in UTC.
//
// UTC rather than local time on purpose: it keeps the handler allocation-free
// (see the file header), and the names stay ISO-sortable, which is what
// CrashHandler_findUnsentBundle()'s strcmp ordering relies on. Nobody
// correlates a crash bundle against local wall-clock time.
static void build_bundle_dir(void) {
    UtcTime t;
    UtcTime_fromUnix((int64_t)time(NULL), &t);

    snprintf(bundle_dir, sizeof(bundle_dir),
             "%s/%04d-%02d-%02d_%02d-%02d-%02d",
             bundle_root, t.year, t.mon, t.day, t.hour, t.min, t.sec);
}

// Gather the live subsystem state into a CrashMeta and format it into meta_buf.
// Returns the length (excluding trailing NUL).
//
// This is the *gathering* half only — the exact bytes are produced by the pure
// CrashMeta_format() in crash_meta.c, which is host-testable. Keep it that way:
// no formatting decisions belong here.
//
// Audio fields are best-effort racy snapshots — the player getters are simple
// reads with no mutex, so a torn read is possible but won't deadlock or crash.
// PII consideration: audio_track is the raw filesystem path; gated by the same
// "Collect crash reports" opt-in as the rest of the bundle.
static size_t format_meta(int signo) {
    uint32_t now = monotonic_ms();
    uint32_t hb = (uint32_t)Watchdog_lastHeartbeatMs();
    uint32_t last_in = atomic_load_explicit(&last_input_ms, memory_order_relaxed);

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

    // Same bounded-copy treatment as track_buf — the main loop may be mid-write.
    char btn_buf[sizeof(last_input_button)];
    size_t blen = strnlen(last_input_button, sizeof(btn_buf) - 1);
    memcpy(btn_buf, last_input_button, blen);
    btn_buf[blen] = '\0';

    CrashMeta m = {
        .version           = app_version,
        .platform          = PLATFORM,
        .signal_name       = CrashMeta_signalName(signo),
        .signal_number     = signo,
        // uptime shares LogTrace's epoch so it agrees with log.txt's timestamps.
        .uptime_ms         = LogTrace_uptimeMs(),
        .last_input_button = btn_buf,
        .last_input_age_ms = (last_in == 0) ? 0 : (now - last_in),
        .heartbeat_age_ms  = (hb == 0) ? 0 : (now - hb),
        .ring_total_bytes  = RingLog_totalAppended(),
        .screen_width      = FbCapture_width(),
        .screen_height     = FbCapture_height(),
        .screen_bpp        = FbCapture_bpp(),
        .audio_state       = player_state_name(Player_getState()),
        .audio_background  = background_name(Background_getActive()),
        .audio_position_ms = Player_getPosition(),
        .audio_duration_ms = Player_getDuration(),
        .audio_track       = track_buf,
    };

    return CrashMeta_format(&m, meta_buf, sizeof(meta_buf));
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

// Re-entry guard for write_bundle(). Signal masks are PER-THREAD, so sa_mask
// cannot stop two threads from faulting at once and both entering write_bundle()
// — a realistic case under heap corruption, where the stream thread and the main
// thread walk the same bad pointer. Without this, both interleave writes into the
// shared bundle_dir / meta_buf / *_path scratch and neither bundle is coherent.
static atomic_int bundle_in_progress = 0;

// Shared bundle writer used by both the fatal-signal handler and the SIGUSR1
// diagnostic-dump handler. Order: log.txt → screen.bmp → meta.txt so a partial
// bundle still has the highest-value file first.
//
// First caller wins; a concurrent or nested caller returns immediately without
// writing. On the fatal path that means the loser proceeds straight to _exit(1),
// which can truncate an in-flight SIGUSR1 dump — an acceptable trade, since
// blocking inside a signal handler to wait for the winner is strictly worse.
static void write_bundle(int signo) {
    if (atomic_exchange_explicit(&bundle_in_progress, 1, memory_order_acq_rel)) {
        return;
    }

    // Dead-man's switch. The bundle writes up to ~3 MB to the SD card on a
    // blocking fd, and the very hang the watchdog just caught may BE a wedged SD
    // card (a flaky card stalls the VFS layer). Without this, the handler would
    // block in write() with fatal signals masked and never reach _exit(1) — the
    // device appears bricked until the user holds power. SIGALRM keeps its
    // default disposition (terminate), so if we don't finish within the budget
    // the process dies without a bundle instead of hanging forever. Disarmed at
    // the end so the returning SIGUSR1 path doesn't get killed later.
    alarm(10);

    mkdir_p(bundle_root);
    build_bundle_dir();
    mkdir(bundle_dir, 0755);

    snprintf(log_path, sizeof(log_path), "%s/log.txt", bundle_dir);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd >= 0) {
        RingLog_dump(log_fd);
        close(log_fd);
    }

    // BMP is the signal-safe choice; CrashHandler_convertPendingScreenshots()
    // converts BMP → PNG at next startup once SDL_image is usable. Skip it
    // entirely when capture never initialized, and delete the file if the
    // capture came back incomplete — a 0-byte or truncated screen.bmp is
    // unloadable and would be retried and warned about on every future launch.
    if (FbCapture_isAvailable()) {
        snprintf(screen_path, sizeof(screen_path), "%s/screen.bmp", bundle_dir);
        int screen_fd = open(screen_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (screen_fd >= 0) {
            ssize_t wrote = FbCapture_writeBmp(screen_fd);
            close(screen_fd);
            if (wrote <= 0) unlink(screen_path);
        }
    }

    snprintf(meta_path, sizeof(meta_path), "%s/meta.txt", bundle_dir);
    int meta_fd = open(meta_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (meta_fd >= 0) {
        size_t meta_len = format_meta(signo);
        write_all(meta_fd, meta_buf, meta_len);
        close(meta_fd);
    }

    alarm(0);   // completed in time — disarm the dead-man's switch

    // Only matters for the SIGUSR1 path, which returns and may dump again.
    // The fatal path _exit(1)s before this is ever observed.
    atomic_store_explicit(&bundle_in_progress, 0, memory_order_release);
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
    // Ring capture keys off the same setting: when collection is off the ring
    // can never be dumped, so there is no reason to pay for populating it.
    LogTrace_setCaptureEnabled(enabled);
}

// True if `name` is a non-dotfile entry under bundle_root that stats as a
// directory; on success fills `out` with "<bundle_root>/<name>".
static bool bundle_dir_path(const char* name, char* out, size_t out_size) {
    if (name[0] == '.') return false;
    snprintf(out, out_size, "%s/%s", bundle_root, name);
    struct stat st;
    return stat(out, &st) == 0 && S_ISDIR(st.st_mode);
}

// True if the bundle named `name` has a skipped.txt marker inside it.
static bool bundle_is_skipped(const char* name) {
    char skip_path[BUNDLE_FILE_MAX];
    snprintf(skip_path, sizeof(skip_path), "%s/%s/skipped.txt", bundle_root, name);
    return access(skip_path, F_OK) == 0;
}

bool CrashHandler_findUnsentBundle(char* out_path, size_t out_size) {
    if (!Settings_getCollectCrashReports()) return false;

    DIR* dir = opendir(bundle_root);
    if (!dir) return false;

    // Track the newest *unskipped* bundle. Skipping the newest must not hide the
    // ones behind it, so the skipped check belongs inside the loop — filtering
    // only the max would make every older unsent bundle permanently unreachable.
    char newest[64] = {0};
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        char path[320];
        if (!bundle_dir_path(ent->d_name, path, sizeof(path))) continue;
        if (strcmp(ent->d_name, newest) <= 0) continue;  // not newer than best
        if (bundle_is_skipped(ent->d_name)) continue;    // dismissed, keep looking

        strncpy(newest, ent->d_name, sizeof(newest) - 1);
        newest[sizeof(newest) - 1] = '\0';
    }
    closedir(dir);

    if (newest[0] == '\0') return false;

    if (out_path && out_size > 0) {
        snprintf(out_path, out_size, "%s/%s", bundle_root, newest);
    }
    return true;
}

int CrashHandler_convertPendingScreenshots(void) {
    DIR* dir = opendir(bundle_root);
    if (!dir) return -1;

    int converted = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        char bundle[BUNDLE_PATH_MAX];
        if (!bundle_dir_path(ent->d_name, bundle, sizeof(bundle))) continue;

        char bmp[BUNDLE_FILE_MAX];
        char png[BUNDLE_FILE_MAX];
        snprintf(bmp, sizeof(bmp), "%s/screen.bmp", bundle);
        snprintf(png, sizeof(png), "%s/screen.png", bundle);

        if (access(png, F_OK) == 0) continue;       // already converted

        struct stat bst;
        if (stat(bmp, &bst) != 0) continue;         // no BMP: incomplete or already converted
        if (bst.st_size == 0) {
            // Empty BMP — a failed capture from an older build that didn't clean
            // up after itself. Unloadable; remove it so it stops being retried.
            unlink(bmp);
            continue;
        }

        SDL_Surface* surf = SDL_LoadBMP(bmp);
        if (!surf) {
            // Corrupt or truncated BMP. It will never load, so deleting it is the
            // only way to stop warning about it on every launch.
            LOG_warn("crash_handler: SDL_LoadBMP(%s) failed: %s; removing\n", bmp, SDL_GetError());
            unlink(bmp);
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
    DIR* dir = opendir(bundle_root);
    if (!dir) return false;

    bool found = false;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        char p[BUNDLE_PATH_MAX];
        if (bundle_dir_path(ent->d_name, p, sizeof(p))) {
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
        char filepath[BUNDLE_FILE_MAX + 256];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, ent->d_name);
        unlink(filepath);
    }
    closedir(dir);
    rmdir(path);
}

int CrashHandler_deleteAllBundles(void) {
    DIR* dir = opendir(bundle_root);
    if (!dir) return -1;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[BUNDLE_PATH_MAX];
        snprintf(p, sizeof(p), "%s/%s", bundle_root, ent->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            delete_bundle_dir(p);
        } else {
            unlink(p);
        }
    }
    closedir(dir);
    rmdir(bundle_root);
    return 0;
}

void CrashHandler_getBundleRootDisplayPath(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const char* full = bundle_root;
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
    // Block the other fatal signals, and SIGUSR1, while handling one — a
    // *delivered* signal then cannot re-enter write_bundle() on this thread.
    // (The signal being handled is auto-blocked already.)
    //
    // This is defence in depth, not the actual protection: masks are per-thread,
    // so they do nothing about two threads faulting at once, and a hardware
    // SIGSEGV raised while SIGSEGV is blocked is undefined per POSIX — Linux
    // force-delivers it with default disposition and kills the process, which is
    // the right outcome anyway. The bundle_in_progress guard in write_bundle()
    // is what actually keeps the shared scratch coherent.
    sigaddset(&sa.sa_mask, SIGSEGV);
    sigaddset(&sa.sa_mask, SIGABRT);
    sigaddset(&sa.sa_mask, SIGBUS);
    sigaddset(&sa.sa_mask, SIGFPE);
    sigaddset(&sa.sa_mask, SIGILL);
    sigaddset(&sa.sa_mask, SIGUSR1);
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
    // Also block the fatal signals: without these, a fault during a diagnostic
    // dump re-enters via crash_handler() on this same thread.
    sigaddset(&diag_sa.sa_mask, SIGSEGV);
    sigaddset(&diag_sa.sa_mask, SIGABRT);
    sigaddset(&diag_sa.sa_mask, SIGBUS);
    sigaddset(&diag_sa.sa_mask, SIGFPE);
    sigaddset(&diag_sa.sa_mask, SIGILL);
    diag_sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &diag_sa, NULL);
}
