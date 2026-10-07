// Tests for src/selfupdate_install.c - the removal of the files that an update no longer
// carries. Each test makes a pak directory and an install directory under its own
// mk_tempdir(), and removes them again.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

#include "file_utils.h"
#include "selfupdate.h"

static void write_file(const char* root, const char* rel, const char* text) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    char* slash = strrchr(path, '/');
    *slash = '\0';
    mkdir_p(path);
    *slash = '/';
    FILE* f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static bool exists(const char* root, const char* rel) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    return access(path, F_OK) == 0;
}

static bool file_says(const char* root, const char* rel, const char* text) {
    char path[600], buf[256] = "";
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

// The pak of this build: an empty state/, and no version file. The old updater
// goes into state/ because the pak carries it.
TEST(pak_with_empty_state_keeps_the_queue) {
    char pak[256], install[256], dir[600];
    CHECK(mk_tempdir("pak_src", pak, sizeof(pak)));
    CHECK(mk_tempdir("install_dst", install, sizeof(install)));
    write_file(pak, "launch.sh", "new");
    snprintf(dir, sizeof(dir), "%s/state", pak);
    CHECK(mkdir_p(dir));
    write_file(install, "launch.sh", "new");
    write_file(install, "state/youtube_queue.txt", "queue");
    write_file(install, "state/app_version.txt", "v1.17.0");
    write_file(install, "old.txt", "old");

    SelfUpdate_removeObsoleteFiles(pak, install);

    CHECK(file_says(install, "state/youtube_queue.txt", "queue"));
    CHECK(!exists(install, "state/app_version.txt"));
    CHECK(!exists(install, "old.txt"));
    CHECK(exists(install, "launch.sh"));
    rm_rf(pak);
    rm_rf(install);
}

TEST(pak_without_state_keeps_the_queue) {
    char pak[256], install[256];
    CHECK(mk_tempdir("pak_src", pak, sizeof(pak)));
    CHECK(mk_tempdir("install_dst", install, sizeof(install)));
    write_file(pak, "launch.sh", "new");
    write_file(install, "state/youtube_queue.txt", "queue");
    write_file(install, "state/yt-dlp_version.txt", "2026.01.01");

    SelfUpdate_removeObsoleteFiles(pak, install);

    CHECK(file_says(install, "state/youtube_queue.txt", "queue"));
    CHECK(!exists(install, "state/yt-dlp_version.txt"));
    rm_rf(pak);
    rm_rf(install);
}

TEST(obsolete_files_without_a_preserved_path_are_removed) {
    char pak[256], install[256];
    CHECK(mk_tempdir("pak_src", pak, sizeof(pak)));
    CHECK(mk_tempdir("install_dst", install, sizeof(install)));
    write_file(pak, "bin/tg5040/musicplayer.elf", "new");
    write_file(install, "bin/tg5040/musicplayer.elf", "new");
    write_file(install, "bin/tg5040/libold.so", "old");
    write_file(install, "bin/yt-dlp", "tool");
    write_file(install, "fonts/old.ttf", "old");

    SelfUpdate_removeObsoleteFiles(pak, install);

    CHECK(exists(install, "bin/tg5040/musicplayer.elf"));
    CHECK(!exists(install, "bin/tg5040/libold.so"));
    CHECK(file_says(install, "bin/yt-dlp", "tool"));
    CHECK(!exists(install, "fonts"));
    rm_rf(pak);
    rm_rf(install);
}

int main(void) {
    RUN(pak_with_empty_state_keeps_the_queue);
    RUN(pak_without_state_keeps_the_queue);
    RUN(obsolete_files_without_a_preserved_path_are_removed);
    return test_summary();
}
