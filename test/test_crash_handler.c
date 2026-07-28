// Host end-to-end tests for crash_handler.c's write_bundle() path.
//
// This is the suite that makes the bundle *contract* enforceable: that a fatal
// signal really produces log.txt + meta.txt on disk, that log.txt carries the
// ring-log tail leading up to the crash, that meta.txt parses as `key: value`
// with the frozen key set, and that the reentrancy guard and the
// collection-disabled short-circuit behave as documented.
//
// Strategy: fork(). The child retargets the bundle root at a temp dir, installs
// the real handlers, seeds the ring log with a marker, and raises a real signal.
// The parent reaps it and inspects the filesystem. Forking is what lets us test
// a handler that ends in _exit(1) without taking the test runner down with it.
//
// The heavy dependencies of the crash_handler TU (player, background,
// fb_capture, settings, watchdog) are satisfied by the link-time stubs at the
// bottom of this file, so no device and no real subsystems are involved.
// SDL/SDL_image are linked for real — crash_handler.c uses them only in
// CrashHandler_convertPendingScreenshots(), which is off the crash path.

#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// api.h must precede log_trace.h — log_trace.h #errors otherwise (LOG_* must
// already be defined). The stub api.h under test/stubs/ satisfies that here.
#include "api.h"
#include "log_trace.h"

// Real headers, for the enum types the link-time stubs at the bottom return.
#include "background.h"
#include "player.h"

#include "crash_handler.h"
#include "ring_log.h"
#include "test.h"

// ---------------------------------------------------------------- test helpers

static char g_tmpdir[160];   // the mkdtemp'd parent
static char g_root[192];     // "<g_tmpdir>/crash-reports"

// Fresh empty scratch dir per test, so bundle discovery can't see a prior
// bundle. Deliberately under bin/ (relative to the test cwd) rather than /tmp:
// it keeps the suite off system temp and `make -C test clean` sweeps it up.
static void make_root(void) {
    char tmpl[] = "bin/mp_crash_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (!d) { perror("mkdtemp"); exit(2); }
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s", d);
    snprintf(g_root, sizeof(g_root), "%s/crash-reports", d);
}

static void rm_rf(const char* path) {
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* e;
        while ((e = readdir(dir)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[512];
            snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
            struct stat st;
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) rm_rf(p);
            else unlink(p);
        }
        closedir(dir);
    }
    rmdir(path);
}

static void cleanup_root(void) {
    rm_rf(g_root);
    rmdir(g_tmpdir);
}

// Path of the single bundle directory under g_root; empty string if none.
// Fails the calling test if more than one exists.
static void find_only_bundle(char* out, size_t n) {
    out[0] = '\0';
    DIR* dir = opendir(g_root);
    if (!dir) return;

    int count = 0;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", g_root, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        count++;
        snprintf(out, n, "%s", p);
    }
    closedir(dir);
    CHECK(count <= 1);
}

static int read_file(const char* path, char* buf, size_t n) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t got = read(fd, buf, n - 1);
    close(fd);
    if (got < 0) return -1;
    buf[got] = '\0';
    return (int)got;
}

static bool file_exists(const char* dir, const char* name) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

// Run `body` in a forked child and return its raw wait status.
// The child never returns — it either _exit()s itself or dies in the handler.
static int run_in_child(void (*body)(void)) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        body();
        _exit(0);   // reached only if the signal did not terminate us
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); exit(2); }
    return status;
}

// The marker the child writes into the ring log immediately before crashing;
// finding it in log.txt proves the ring tail survived into the bundle.
static const char* MARKER = "CRASH_MARKER_canary_42";

// Shared child setup: retarget the bundle root, bring up the ring + log tee,
// enable collection, install the real handlers.
static void child_init(void) {
    if (!CrashHandler_setBundleRootForTesting(g_root)) _exit(3);
    RingLog_init();
    LogTrace_init();
    CrashHandler_init("9.9.9");
    CrashHandler_setCollectionEnabled(true);
}

// ---------------------------------------------------------------------- tests

// A real SIGSEGV must produce log.txt and meta.txt, and exit(1) via the handler.
static void child_segv(void) {
    child_init();
    CrashHandler_noteInput("BTN_A");
    LOG_trace("%s", MARKER);
    raise(SIGSEGV);
}

TEST(sigsegv_produces_log_and_meta_and_exits_one) {
    make_root();
    int status = run_in_child(child_segv);

    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 1);   // handler's _exit(1)

    char bundle[512];
    find_only_bundle(bundle, sizeof(bundle));
    CHECK(bundle[0] != '\0');
    if (!bundle[0]) { cleanup_root(); return; }

    CHECK(file_exists(bundle, "log.txt"));
    CHECK(file_exists(bundle, "meta.txt"));
    // fb_capture is stubbed unavailable, so screen.bmp must be skipped entirely
    // rather than left as a 0-byte unloadable file.
    CHECK(!file_exists(bundle, "screen.bmp"));

    cleanup_root();
}

TEST(log_txt_contains_the_pre_crash_ring_tail) {
    make_root();
    run_in_child(child_segv);

    char bundle[512], path[576], buf[8192];
    find_only_bundle(bundle, sizeof(bundle));
    if (!bundle[0]) { CHECK(0); cleanup_root(); return; }

    snprintf(path, sizeof(path), "%s/log.txt", bundle);
    int n = read_file(path, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, MARKER) != NULL);

    cleanup_root();
}

// The frozen meta.txt key set, per spec/crash-reporting.md.
static const char* META_KEYS[] = {
    "version", "platform", "signal", "uptime_ms",
    "last_input_button", "last_input_age_ms", "heartbeat_age_ms",
    "ring_total_bytes", "screen_width", "screen_height", "screen_bpp",
    "audio_state", "audio_background", "audio_position_ms",
    "audio_duration_ms", "audio_track",
};
#define META_KEY_COUNT (sizeof(META_KEYS) / sizeof(META_KEYS[0]))

TEST(meta_txt_parses_as_key_value_with_the_frozen_key_set) {
    make_root();
    run_in_child(child_segv);

    char bundle[512], path[576], buf[4096];
    find_only_bundle(bundle, sizeof(bundle));
    if (!bundle[0]) { CHECK(0); cleanup_root(); return; }

    snprintf(path, sizeof(path), "%s/meta.txt", bundle);
    CHECK(read_file(path, buf, sizeof(buf)) > 0);

    size_t idx = 0;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char* sep = strstr(line, ": ");
        // "audio_track: " with an empty value has no trailing space, so accept a
        // bare "key:" line too.
        size_t klen;
        if (sep) klen = (size_t)(sep - line);
        else {
            char* colon = strchr(line, ':');
            CHECK(colon != NULL);
            if (!colon) continue;
            klen = (size_t)(colon - line);
        }
        CHECK(idx < META_KEY_COUNT);
        if (idx >= META_KEY_COUNT) break;
        CHECK(klen == strlen(META_KEYS[idx]) && strncmp(line, META_KEYS[idx], klen) == 0);
        if (klen != strlen(META_KEYS[idx]) || strncmp(line, META_KEYS[idx], klen) != 0) {
            printf("    line %zu: got '%.*s', want '%s'\n",
                   idx, (int)klen, line, META_KEYS[idx]);
        }
        idx++;
    }
    CHECK_EQ_SZ(idx, META_KEY_COUNT);

    cleanup_root();
}

TEST(meta_txt_records_signal_version_platform_and_last_input) {
    make_root();
    run_in_child(child_segv);

    char bundle[512], path[576], buf[4096];
    find_only_bundle(bundle, sizeof(bundle));
    if (!bundle[0]) { CHECK(0); cleanup_root(); return; }

    snprintf(path, sizeof(path), "%s/meta.txt", bundle);
    CHECK(read_file(path, buf, sizeof(buf)) > 0);

    CHECK(strstr(buf, "version: 9.9.9\n") != NULL);         // passed to CrashHandler_init
    CHECK(strstr(buf, "platform: hosttest\n") != NULL);     // from the stub api.h
    CHECK(strstr(buf, "signal: SIGSEGV") != NULL);
    CHECK(strstr(buf, "last_input_button: BTN_A\n") != NULL);
    // The ring had content before the crash, so the lifetime counter is non-zero.
    CHECK(strstr(buf, "ring_total_bytes: 0\n") == NULL);

    cleanup_root();
}

// SIGUSR1 is the diagnostic-dump path: same bundle layout, but the process must
// survive and keep running.
static void child_usr1(void) {
    child_init();
    LOG_trace("%s", MARKER);
    raise(SIGUSR1);
    _exit(7);   // proves we came back from the handler
}

TEST(sigusr1_dumps_a_bundle_and_the_process_survives) {
    make_root();
    int status = run_in_child(child_usr1);

    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 7);   // returned, not _exit(1)

    char bundle[512], path[576], buf[4096];
    find_only_bundle(bundle, sizeof(bundle));
    CHECK(bundle[0] != '\0');
    if (!bundle[0]) { cleanup_root(); return; }

    CHECK(file_exists(bundle, "log.txt"));
    CHECK(file_exists(bundle, "meta.txt"));

    snprintf(path, sizeof(path), "%s/meta.txt", bundle);
    CHECK(read_file(path, buf, sizeof(buf)) > 0);
    CHECK(strstr(buf, "signal: SIGUSR1") != NULL);

    cleanup_root();
}

// With collection disabled the handler must short-circuit before touching the
// filesystem — no bundle root, no bundle, and still _exit(1).
static void child_collection_disabled(void) {
    child_init();
    CrashHandler_setCollectionEnabled(false);
    LOG_trace("%s", MARKER);
    raise(SIGSEGV);
}

TEST(collection_disabled_writes_nothing) {
    make_root();
    int status = run_in_child(child_collection_disabled);

    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 1);

    // The root directory itself is never even created.
    CHECK(access(g_root, F_OK) != 0);

    cleanup_root();
}

// Two SIGUSR1 dumps in a row must both succeed: the reentrancy guard is
// released at the end of write_bundle(), so it must not wedge after one use.
//
// SECOND_MARKER enters the ring only *between* the two dumps, so it can appear
// in a log.txt only if the second dump actually wrote one. Asserting merely
// that a bundle exists would pass even with a permanently-stuck guard, since
// the first dump already created it.
static const char* SECOND_MARKER = "CRASH_MARKER_second_dump_99";

static void child_two_usr1(void) {
    child_init();
    LOG_trace("%s", MARKER);
    raise(SIGUSR1);
    LOG_trace("%s", SECOND_MARKER);
    raise(SIGUSR1);
    _exit(7);
}

// True if any bundle under g_root has a log.txt containing `needle`. The two
// dumps usually share a bundle (same second) but may straddle a second
// boundary, so scan them all rather than assuming a single directory.
static bool any_bundle_log_contains(const char* needle) {
    DIR* dir = opendir(g_root);
    if (!dir) return false;

    bool found = false;
    struct dirent* e;
    while (!found && (e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[576], buf[8192];
        snprintf(path, sizeof(path), "%s/%s/log.txt", g_root, e->d_name);
        if (read_file(path, buf, sizeof(buf)) > 0 && strstr(buf, needle)) found = true;
    }
    closedir(dir);
    return found;
}

TEST(reentrancy_guard_releases_between_dumps) {
    make_root();
    int status = run_in_child(child_two_usr1);

    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 7);

    CHECK(any_bundle_log_contains(MARKER));
    CHECK(any_bundle_log_contains(SECOND_MARKER));   // proves the 2nd dump ran

    cleanup_root();
}

// The bundle root override is the seam this whole suite rests on; guard its
// contract so a future change can't silently make the tests write to the real
// SD-card path.
TEST(bundle_root_override_is_rejected_after_init) {
    make_root();

    // In-process (no fork): handlers are not installed here, so the first call
    // succeeds and a bogus one is rejected.
    CHECK(CrashHandler_setBundleRootForTesting(g_root) == true);
    CHECK(CrashHandler_setBundleRootForTesting(NULL) == false);
    CHECK(CrashHandler_setBundleRootForTesting("") == false);

    char toolong[512];
    memset(toolong, 'x', sizeof(toolong) - 1);
    toolong[sizeof(toolong) - 1] = '\0';
    CHECK(CrashHandler_setBundleRootForTesting(toolong) == false);

    cleanup_root();
}

// After init, the setter must refuse — verified in a child so installing the
// real handlers doesn't affect the rest of the suite.
static void child_override_after_init(void) {
    child_init();
    _exit(CrashHandler_setBundleRootForTesting("/tmp/somewhere-else") ? 1 : 0);
}

TEST(bundle_root_override_refuses_once_handlers_are_installed) {
    make_root();
    int status = run_in_child(child_override_after_init);

    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 0);   // 0 == correctly refused

    cleanup_root();
}

// findUnsentBundle must see the bundle the crash just wrote, and stop seeing it
// once it is skipped — the menu row's whole lifecycle.
static void child_segv_for_discovery(void) {
    child_init();
    LOG_trace("%s", MARKER);
    raise(SIGSEGV);
}

TEST(written_bundle_is_discoverable_and_skippable) {
    make_root();
    run_in_child(child_segv_for_discovery);

    // Point this process at the same root so the discovery API can see it.
    CHECK(CrashHandler_setBundleRootForTesting(g_root) == true);

    char found[512] = {0};
    CHECK(CrashHandler_findUnsentBundle(found, sizeof(found)) == true);
    CHECK(found[0] != '\0');
    CHECK(CrashHandler_hasAnyBundle() == true);

    CHECK(CrashHandler_skipBundle(found) == 0);
    CHECK(file_exists(found, "skipped.txt"));
    // Skipped is now the only bundle, so nothing unsent remains...
    CHECK(CrashHandler_findUnsentBundle(NULL, 0) == false);
    // ...but it still counts as deletable content.
    CHECK(CrashHandler_hasAnyBundle() == true);

    CHECK(CrashHandler_deleteAllBundles() == 0);
    CHECK(CrashHandler_hasAnyBundle() == false);

    cleanup_root();
}

// ------------------------------------------------------------- link-time stubs
//
// Everything crash_handler.c calls that would otherwise drag in the player,
// the display, or the settings store. Values are fixed so meta.txt assertions
// are deterministic.

PlayerState Player_getState(void)        { return PLAYER_STATE_STOPPED; }
int         Player_getPosition(void)     { return 0; }
int         Player_getDuration(void)     { return 0; }
const char* Player_getCurrentFile(void)  { return NULL; }

BackgroundPlayerType Background_getActive(void) { return BG_NONE; }

// Capture reports unavailable: exercises the "skip screen.bmp entirely" branch
// without needing a framebuffer.
bool    FbCapture_init(void)         { return false; }
bool    FbCapture_isAvailable(void)  { return false; }
ssize_t FbCapture_writeBmp(int fd)   { (void)fd; return -1; }
int     FbCapture_width(void)        { return 0; }
int     FbCapture_height(void)       { return 0; }
int     FbCapture_bpp(void)          { return 0; }

// CrashHandler_init() seeds from this and registers a listener; the tests drive
// collection explicitly via CrashHandler_setCollectionEnabled() afterwards.
bool Settings_getCollectCrashReports(void) { return true; }
void Settings_setCollectCrashReportsListener(void (*listener)(bool)) { (void)listener; }

unsigned int Watchdog_lastHeartbeatMs(void) { return 0; }

// ------------------------------------------------------------------------ main

int main(void) {
    printf("crash_handler:\n");
    RUN(sigsegv_produces_log_and_meta_and_exits_one);
    RUN(log_txt_contains_the_pre_crash_ring_tail);
    RUN(meta_txt_parses_as_key_value_with_the_frozen_key_set);
    RUN(meta_txt_records_signal_version_platform_and_last_input);
    RUN(sigusr1_dumps_a_bundle_and_the_process_survives);
    RUN(collection_disabled_writes_nothing);
    RUN(reentrancy_guard_releases_between_dumps);
    RUN(bundle_root_override_is_rejected_after_init);
    RUN(bundle_root_override_refuses_once_handlers_are_installed);
    RUN(written_bundle_is_discoverable_and_skippable);
    return test_summary();
}
