// Tests for src/file_utils.c - the shell-out helpers and the recursive copy.
//
// Unlike the other tests here this one touches the filesystem, because that is
// what the module is. Everything it makes goes under its own mk_tempdir() and
// is removed again, so the suite stays repeatable.
//
// Two properties are worth the extra machinery. shell_escape() is checked by
// round-tripping through a real shell rather than by inspecting its output -
// the claim is "the shell sees this back unchanged", and only a shell can
// settle that. And cp_rf() is checked against a *running* binary, because
// replacing one is why it writes-then-renames: opening a busy executable for
// writing fails with ETXTBSY, and a plain copy silently broke every self-update
// until that was found.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "test.h"
#include <zip.h>

#include "file_utils.h"

// ---------------------------------------------------------------- helpers

static void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static bool file_says(const char* path, const char* text) {
    char buf[256] = "";
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

// What a real shell makes of a string once escaped and wrapped in double quotes.
static void through_shell(const char* raw, char* out, size_t out_size) {
    char escaped[512];
    shell_escape(raw, escaped, sizeof(escaped));

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "printf '%%s' \"%s\"", escaped);

    out[0] = '\0';
    FILE* p = popen(cmd, "r");
    if (!p) return;
    size_t n = fread(out, 1, out_size - 1, p);
    out[n] = '\0';
    pclose(p);
}

static int copied_files = 0;
static void count_copied(const char* rel_path, void* ctx) {
    (void)rel_path;
    (void)ctx;
    copied_files++;
}

// ---------------------------------------------------------------- shell_escape

// The four characters a shell still reads inside double quotes, and nothing
// else. Apostrophes in particular must pass through untouched - they are the
// reason the quoting is double rather than single.
TEST(shell_escape_neutralizes_what_double_quotes_do_not) {
    const char* cases[] = {
        "plain",
        "with space",
        "it's",
        "a\"b",
        "$HOME",
        "$(id)",
        "`id`",
        "back\\slash",
        "all $of `them` \"at\" once",
        NULL
    };

    for (int i = 0; cases[i]; i++) {
        char seen[512];
        through_shell(cases[i], seen, sizeof(seen));
        if (strcmp(seen, cases[i]) != 0) {
            printf("    (escaped %s -> shell saw %s)\n", cases[i], seen);
        }
        CHECK_EQ_INT(strcmp(seen, cases[i]), 0);
    }
}

TEST(shell_escape_stays_inside_its_buffer) {
    char small[8];
    shell_escape("$$$$$$$$$$$$$$$$$$$$", small, sizeof(small));
    CHECK(strlen(small) < sizeof(small));

    // Every escape is a backslash plus the character, so nothing may end on a
    // lone trailing backslash - that would escape the closing quote.
    size_t len = strlen(small);
    int trailing = 0;
    while (len > 0 && small[len - 1] == '\\') { trailing++; len--; }
    CHECK_EQ_INT(trailing % 2, 0);
}

// ---------------------------------------------------------------- mk_tempdir

TEST(mk_tempdir_creates_a_fresh_directory_each_time) {
    char a[256], b[256];
    CHECK(mk_tempdir("fu_test", a, sizeof(a)));
    CHECK(mk_tempdir("fu_test", b, sizeof(b)));

    CHECK_EQ_INT(access(a, F_OK), 0);
    CHECK_EQ_INT(access(b, F_OK), 0);
    CHECK(strcmp(a, b) != 0);

    rm_rf(a);
    rm_rf(b);
    CHECK(access(a, F_OK) != 0);
    CHECK(access(b, F_OK) != 0);
}

// A directory that is already there belonged to someone else - staging into it
// would mix their leftovers into whatever we are about to install.
TEST(mk_tempdir_reports_failure_and_clears_the_path) {
    char out[256] = "untouched";
    CHECK(!mk_tempdir("no_such_parent/deeper", out, sizeof(out)));
    CHECK_EQ_INT(out[0], '\0');
}

// ---------------------------------------------------------------- find_file

TEST(find_file_walks_the_tree) {
    char root[256];
    CHECK(mk_tempdir("fu_find", root, sizeof(root)));

    char deep[512], leaf[600];
    snprintf(deep, sizeof(deep), "%s/a/b/c", root);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", deep);
    CHECK_EQ_INT(system(cmd), 0);
    snprintf(leaf, sizeof(leaf), "%s/launch.sh", deep);
    write_file(leaf, "#!/bin/sh\n");

    char found[600] = "untouched";
    CHECK(find_file(root, "launch.sh", found, sizeof(found)));
    CHECK_EQ_INT(strcmp(found, leaf), 0);

    // A miss leaves the caller's buffer alone, so a stale path is never used
    char missed[600] = "untouched";
    CHECK(!find_file(root, "nothing.here", missed, sizeof(missed)));
    CHECK_EQ_INT(strcmp(missed, "untouched"), 0);

    rm_rf(root);
}

// ---------------------------------------------------------------- cp_rf

TEST(cp_rf_copies_a_tree_and_reports_each_file) {
    char src[256], dst[256];
    CHECK(mk_tempdir("fu_src", src, sizeof(src)));
    CHECK(mk_tempdir("fu_dst", dst, sizeof(dst)));

    char cmd[700], path[600];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/nested/deep'", src);
    CHECK_EQ_INT(system(cmd), 0);

    snprintf(path, sizeof(path), "%s/top.txt", src);
    write_file(path, "top");
    snprintf(path, sizeof(path), "%s/nested/mid.txt", src);
    write_file(path, "mid");
    snprintf(path, sizeof(path), "%s/nested/deep/low.txt", src);
    write_file(path, "low");

    copied_files = 0;
    CHECK(cp_rf(src, dst, count_copied, NULL));
    CHECK_EQ_INT(copied_files, 3);

    snprintf(path, sizeof(path), "%s/top.txt", dst);
    CHECK(file_says(path, "top"));
    snprintf(path, sizeof(path), "%s/nested/deep/low.txt", dst);
    CHECK(file_says(path, "low"));

    rm_rf(src);
    rm_rf(dst);
}

// The copy stages beside the target and renames over it, so a half-written file
// is never visible at the destination and no staging file is left behind.
TEST(cp_rf_overwrites_without_leaving_staging_files) {
    char src[256], dst[256];
    CHECK(mk_tempdir("fu_src", src, sizeof(src)));
    CHECK(mk_tempdir("fu_dst", dst, sizeof(dst)));

    char spath[600], dpath[600], staged[700];
    snprintf(spath, sizeof(spath), "%s/thing.txt", src);
    snprintf(dpath, sizeof(dpath), "%s/thing.txt", dst);
    snprintf(staged, sizeof(staged), "%s/thing.txt.new", dst);

    write_file(spath, "new contents");
    write_file(dpath, "old contents");

    CHECK(cp_rf(src, dst, NULL, NULL));
    CHECK(file_says(dpath, "new contents"));
    CHECK(access(staged, F_OK) != 0);

    rm_rf(src);
    rm_rf(dst);
}

TEST(cp_rf_fails_on_a_missing_source) {
    char dst[256];
    CHECK(mk_tempdir("fu_dst", dst, sizeof(dst)));
    CHECK(!cp_rf("/no/such/source/tree", dst, NULL, NULL));
    rm_rf(dst);
}

// The reason copy_file renames instead of writing in place. Opening a running
// executable with O_WRONLY fails with ETXTBSY; renaming onto its path does not,
// because the running process keeps the old inode.
TEST(cp_rf_replaces_a_running_binary) {
    char self[512] = "";
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) {
        printf("    (no /proc/self/exe, skipped)\n");
        return;
    }
    self[n] = '\0';

    char src[256], dst[256];
    CHECK(mk_tempdir("fu_busy_src", src, sizeof(src)));
    CHECK(mk_tempdir("fu_busy_dst", dst, sizeof(dst)));

    char victim[600], replacement[600], cmd[2600];
    snprintf(victim, sizeof(victim), "%s/victim", dst);
    snprintf(replacement, sizeof(replacement), "%s/victim", src);
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s' && cp '%s' '%s'", self, victim, self, replacement);
    CHECK_EQ_INT(system(cmd), 0);
    chmod(victim, 0755);

    pid_t running = fork();
    if (running == 0) {
        execl(victim, victim, "--sleep", (char*)NULL);
        _exit(127);
    }
    // Bail out rather than carry on: a failed fork leaves -1 here, and -1 is
    // every process this user may signal
    if (running < 0) {
        CHECK(running > 0);
        rm_rf(src);
        rm_rf(dst);
        return;
    }
    usleep(300000);  // let it get as far as exec

    CHECK(cp_rf(src, dst, NULL, NULL));

    char staged[700];
    snprintf(staged, sizeof(staged), "%s/victim.new", dst);
    CHECK(access(staged, F_OK) != 0);

    kill(running, SIGTERM);
    waitpid(running, NULL, 0);

    rm_rf(src);
    rm_rf(dst);
}

// ---------------------------------------------------------------- extract_zip

// Build an archive in place. The buffers must outlive zip_close(), so callers
// pass string literals.
static bool make_zip(const char* path, const char* const names[], const char* const bodies[],
                     const mode_t modes[]) {
    int err = 0;
    zip_t* z = zip_open(path, ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!z) return false;

    for (int i = 0; names[i]; i++) {
        zip_source_t* src = zip_source_buffer(z, bodies[i], strlen(bodies[i]), 0);
        zip_int64_t idx = src ? zip_file_add(z, names[i], src, ZIP_FL_OVERWRITE) : -1;
        if (idx < 0) {
            zip_discard(z);
            return false;
        }
        if (modes && modes[i]) {
            zip_file_set_external_attributes(z, (zip_uint64_t)idx, 0, ZIP_OPSYS_UNIX,
                                             ((zip_uint32_t)modes[i]) << 16);
        }
    }

    return zip_close(z) == 0;
}

static long seen_done = 0, seen_total = 0;
static int progress_calls = 0;
static void note_entry(long done, long total, void* ctx) {
    (void)ctx;
    seen_done = done;
    seen_total = total;
    progress_calls++;
}

TEST(extract_zip_unpacks_and_reports_progress) {
    char dir[256];
    CHECK(mk_tempdir("fu_zip", dir, sizeof(dir)));

    char zip_path[512];
    snprintf(zip_path, sizeof(zip_path), "%s/good.zip", dir);
    const char* const names[]  = {"pak/launch.sh", "pak/res/font.ttf", NULL};
    const char* const bodies[] = {"#!/bin/sh\n", "fontdata", NULL};
    const mode_t modes[]       = {0755, 0644, 0};
    CHECK(make_zip(zip_path, names, bodies, modes));

    char dest[512];
    snprintf(dest, sizeof(dest), "%s/out", dir);

    seen_done = seen_total = progress_calls = 0;
    CHECK_EQ_INT(extract_zip(zip_path, dest, note_entry, NULL), 2);
    CHECK_EQ_INT(progress_calls, 2);
    CHECK_EQ_INT(seen_done, 2);
    CHECK_EQ_INT(seen_total, 2);

    char p[700];
    snprintf(p, sizeof(p), "%s/pak/launch.sh", dest);
    CHECK(file_says(p, "#!/bin/sh\n"));
    snprintf(p, sizeof(p), "%s/pak/res/font.ttf", dest);
    CHECK(file_says(p, "fontdata"));

    // The archive records Unix modes, and they come back - which is what saves
    // the caller from keeping a list of which paths need to be executable
    struct stat st;
    snprintf(p, sizeof(p), "%s/pak/launch.sh", dest);
    CHECK_EQ_INT(stat(p, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0755);
    snprintf(p, sizeof(p), "%s/pak/res/font.ttf", dest);
    CHECK_EQ_INT(stat(p, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0644);

    rm_rf(dir);
}

// setuid is not something a downloaded archive gets to ask for.
TEST(extract_zip_drops_setuid_from_an_entry) {
    char dir[256];
    CHECK(mk_tempdir("fu_zip", dir, sizeof(dir)));

    char zip_path[512], dest[512];
    snprintf(zip_path, sizeof(zip_path), "%s/suid.zip", dir);
    snprintf(dest, sizeof(dest), "%s/out", dir);

    const char* const names[]  = {"greedy", NULL};
    const char* const bodies[] = {"x", NULL};
    const mode_t modes[]       = {04755, 0};
    CHECK(make_zip(zip_path, names, bodies, modes));

    CHECK_EQ_INT(extract_zip(zip_path, dest, NULL, NULL), 1);

    char p[700];
    struct stat st;
    snprintf(p, sizeof(p), "%s/greedy", dest);
    CHECK_EQ_INT(stat(p, &st), 0);
    CHECK_EQ_INT(st.st_mode & 07777, 0755);

    rm_rf(dir);
}

// An entry naming a path outside the destination fails the whole extraction,
// rather than being skipped - a tree missing files reads as complete to whatever
// installs it next.
TEST(extract_zip_refuses_an_escaping_entry) {
    char dir[256];
    CHECK(mk_tempdir("fu_zip", dir, sizeof(dir)));

    char zip_path[512], dest[512], escaped[700];
    snprintf(zip_path, sizeof(zip_path), "%s/evil.zip", dir);
    snprintf(dest, sizeof(dest), "%s/out", dir);
    snprintf(escaped, sizeof(escaped), "%s/escaped.txt", dir);

    const char* const names[]  = {"pak/ok.txt", "../escaped.txt", NULL};
    const char* const bodies[] = {"fine", "pwned", NULL};
    CHECK(make_zip(zip_path, names, bodies, NULL));

    CHECK_EQ_INT(extract_zip(zip_path, dest, NULL, NULL), -1);
    CHECK(access(escaped, F_OK) != 0);

    rm_rf(dir);
}

TEST(extract_zip_refuses_a_corrupt_archive) {
    char dir[256];
    CHECK(mk_tempdir("fu_zip", dir, sizeof(dir)));

    char zip_path[512], dest[512];
    snprintf(zip_path, sizeof(zip_path), "%s/torn.zip", dir);
    snprintf(dest, sizeof(dest), "%s/out", dir);

    const char* const names[]  = {"pak/a.txt", NULL};
    const char* const bodies[] = {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", NULL};
    CHECK(make_zip(zip_path, names, bodies, NULL));

    // Lop off the tail, taking the central directory with it
    struct stat st;
    CHECK_EQ_INT(stat(zip_path, &st), 0);
    CHECK_EQ_INT(truncate(zip_path, st.st_size / 2), 0);

    CHECK_EQ_INT(extract_zip(zip_path, dest, NULL, NULL), -1);

    rm_rf(dir);
}

TEST(extract_zip_refuses_a_missing_archive) {
    char dir[256];
    CHECK(mk_tempdir("fu_zip", dir, sizeof(dir)));
    char dest[512];
    snprintf(dest, sizeof(dest), "%s/out", dir);
    CHECK_EQ_INT(extract_zip("/no/such/archive.zip", dest, NULL, NULL), -1);
    rm_rf(dir);
}

// ---------------------------------------------------------------- rm_rf

// Paths reach rm_rf from archive entry names, so a name a shell would read as
// a command must be deleted, not run.
TEST(rm_rf_removes_a_hostile_name) {
    char root[256];
    CHECK(mk_tempdir("fu_rm", root, sizeof(root)));

    char cwd[512] = "";
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL);
    CHECK_EQ_INT(chdir(root), 0);

    // No slashes: this has to be one directory name, not a path. If the shell
    // ever gets to read it, $(touch) leaves the witness behind.
    const char* nasty = "we$(touch pwned)ird `id` \"q\" 'a'";
    CHECK_EQ_INT(mkdir(nasty, 0755), 0);

    rm_rf(nasty);
    CHECK(access(nasty, F_OK) != 0);
    CHECK(access("pwned", F_OK) != 0);  // the substitution never ran

    CHECK_EQ_INT(chdir(cwd), 0);
    rm_rf(root);
    CHECK(access(root, F_OK) != 0);
}

int main(int argc, char** argv) {
    // Re-exec'd by cp_rf_replaces_a_running_binary as the busy executable
    if (argc > 1 && strcmp(argv[1], "--sleep") == 0) {
        sleep(30);
        return 0;
    }

    RUN(shell_escape_neutralizes_what_double_quotes_do_not);
    RUN(shell_escape_stays_inside_its_buffer);
    RUN(mk_tempdir_creates_a_fresh_directory_each_time);
    RUN(mk_tempdir_reports_failure_and_clears_the_path);
    RUN(find_file_walks_the_tree);
    RUN(cp_rf_copies_a_tree_and_reports_each_file);
    RUN(cp_rf_overwrites_without_leaving_staging_files);
    RUN(cp_rf_fails_on_a_missing_source);
    RUN(cp_rf_replaces_a_running_binary);
    RUN(extract_zip_unpacks_and_reports_progress);
    RUN(extract_zip_drops_setuid_from_an_entry);
    RUN(extract_zip_refuses_an_escaping_entry);
    RUN(extract_zip_refuses_a_corrupt_archive);
    RUN(extract_zip_refuses_a_missing_archive);
    RUN(rm_rf_removes_a_hostile_name);
    return test_summary();
}
