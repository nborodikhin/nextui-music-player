#ifndef __TEST_H__
#define __TEST_H__

// Minimal host-side test framework — no deps beyond stdio.
// Usage:
//   TEST(name) { ... CHECK(cond); CHECK_EQ_SZ(a, b); }
//   in main(): RUN(name); then return test_summary();

#include <stdio.h>

static int   tests_run    = 0;
static int   tests_failed = 0;
static int   checks_failed_in_test = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            checks_failed_in_test++;                                       \
            printf("    FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

// Size_t equality with value diagnostics.
#define CHECK_EQ_SZ(actual, expected)                                      \
    do {                                                                   \
        size_t _a = (size_t)(actual), _e = (size_t)(expected);            \
        if (_a != _e) {                                                    \
            checks_failed_in_test++;                                       \
            printf("    FAIL %s:%d  %s: got %zu, want %zu\n",             \
                   __FILE__, __LINE__, #actual, _a, _e);                   \
        }                                                                  \
    } while (0)

#define TEST(name) static void name(void)

#define RUN(name)                                                          \
    do {                                                                   \
        checks_failed_in_test = 0;                                         \
        tests_run++;                                                       \
        name();                                                            \
        if (checks_failed_in_test) {                                       \
            tests_failed++;                                                 \
            printf("  [FAIL] %s (%d check(s))\n", #name, checks_failed_in_test); \
        } else {                                                           \
            printf("  [ok]   %s\n", #name);                                \
        }                                                                  \
    } while (0)

static int test_summary(void) {
    printf("\n%d test(s), %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}

#endif
