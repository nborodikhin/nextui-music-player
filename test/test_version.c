#include "test.h"
#include "version.h"
#include <stddef.h>

TEST(valid_forms) {
    const char* values[] = {"v0.0.0", "1.2.3", "v1.18.0-beta.1+test.2",
        "v1.0.0-1", "v1.0.0-a-b", "v1.18.0-rc1", "v1.0.0+01", "v1.99999999999999999999.0"};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) CHECK(Version_isValid(values[i]));
}

TEST(invalid_forms) {
    const char* values[] = {NULL, "", "foo", "V1.0.0", "v1.01.0", "v01.0.0",
        "v1.0.00", "v1.0", "v1.0.0-", "v1.0.0+", "v1.0.0-a..b", "v1.0.0-a.01",
        "v1.0.0-a\"b", "v1.0.0+a_b", "v1.0.0+a+b", "v1.0.0-a.", "v1.0.0+é"};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) CHECK(!Version_isValid(values[i]));
}

TEST(version_order) {
    const char* pairs[][2] = {
        {"v1.9.0", "v1.10.0"}, {"v1.2.0", "v1.99999999999999999999.0"},
        {"v1.01.0", "v1.0.0"}, {"latest", "v0.0.1"}, {NULL, "v0.0.0"},
        {"v1.18.0-beta.1", "v1.18.0"}, {"v1.18.0-beta.9", "v1.18.0-beta.10"},
        {"v1.17.0", "v1.18.0-beta.1"}, {"v1.18.0-beta", "v1.18.0-beta.1"},
        {"v1.18.0-1", "v1.18.0-beta"}, {"v1.18.0-Beta", "v1.18.0-beta"},
        {"v1.0.0-a", "v1.0.0-aa"}, {"v1.0.0-9", "v1.0.0-100000000000000000000"}
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(*pairs); i++) {
        CHECK(Version_compare(pairs[i][0], pairs[i][1]) < 0);
        CHECK(Version_compare(pairs[i][1], pairs[i][0]) > 0);
    }
    CHECK_EQ_INT(Version_compare("v1.18.0+local", "v1.18.0"), 0);
    CHECK_EQ_INT(Version_compare("1.18.0-beta.1+x", "v1.18.0-beta.1+y"), 0);
    CHECK_EQ_INT(Version_compare(NULL, "bad"), 0);
    CHECK_EQ_INT(Version_compare(NULL, NULL), 0);
}

// A digit in a text identifier compares as text, thus each rcN is above each rc.N.
TEST(rc_forms) {
    const char* ascending[] = {"v1.18.0-rc.1", "v1.18.0-rc.2", "v1.18.0-rc1", "v1.18.0-rc2"};
    size_t count = sizeof(ascending) / sizeof(*ascending);
    for (size_t lower = 0; lower < count; lower++) {
        CHECK_EQ_INT(Version_compare(ascending[lower], ascending[lower]), 0);
        for (size_t higher = lower + 1; higher < count; higher++) {
            CHECK(Version_compare(ascending[lower], ascending[higher]) < 0);
            CHECK(Version_compare(ascending[higher], ascending[lower]) > 0);
        }
    }
}

int main(void) {
    RUN(valid_forms);
    RUN(invalid_forms);
    RUN(version_order);
    RUN(rc_forms);
    return test_summary();
}
