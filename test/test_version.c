#include "test.h"
#include "version.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

#define ASSET "Music.Player.pak.zip"

// Returns the JSON text of one release. Pass false in with_asset to leave the
// release asset out, and true in draft to mark the release as a draft.
static const char* release_json(char* buffer, size_t size, const char* tag,
                                bool with_asset, bool draft) {
    snprintf(buffer, size,
             "{\"tag_name\": \"%s\", \"draft\": %s, \"assets\": ["
             "{\"name\": \"other.zip\", \"browser_download_url\": \"https://x/other\"}%s]}",
             tag, draft ? "true" : "false",
             with_asset ? ", {\"name\": \"" ASSET "\", "
                          "\"browser_download_url\": \"https://x/pak\"}"
                        : "");
    return buffer;
}

// Returns the tag of the selected release, or NULL.
static const char* select_tag(const char* json, VersionReleaseStatus* status) {
    static char tag[64];
    JSON_Value* value = json_parse_string(json);
    if (status) *status = Version_bestReleaseExists(value, ASSET);
    const JSON_Object* best = Version_getBestRelease(value, ASSET);
    const char* result = NULL;
    if (best) {
        snprintf(tag, sizeof(tag), "%s", json_object_get_string(best, "tag_name"));
        result = tag;
    }
    json_value_free(value);
    return result;
}

static bool tag_is(const char* actual, const char* expected) {
    return actual && strcmp(actual, expected) == 0;
}

TEST(stable_after_its_pre_releases) {
    char a[256], b[256], c[256], json[1024];
    snprintf(json, sizeof(json), "[%s, %s, %s]",
             release_json(a, sizeof(a), "v1.18.0", true, false),
             release_json(b, sizeof(b), "v1.18.0-rc.1", true, false),
             release_json(c, sizeof(c), "v1.18.0-beta.1", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.18.0"));
}

TEST(pre_release_ahead_of_latest) {
    char a[256], b[256], json[1024];
    snprintf(json, sizeof(json), "[%s, %s]",
             release_json(a, sizeof(a), "v1.18.0-beta.1", true, false),
             release_json(b, sizeof(b), "v1.17.0", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.18.0-beta.1"));
}

TEST(newer_release_without_asset) {
    char a[256], b[256], json[1024];
    snprintf(json, sizeof(json), "[%s, %s]",
             release_json(a, sizeof(a), "v1.18.0-beta.2", false, false),
             release_json(b, sizeof(b), "v1.18.0-beta.1", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.18.0-beta.1"));
}

TEST(late_fix_of_older_version) {
    char a[256], b[256], json[1024];
    snprintf(json, sizeof(json), "[%s, %s]",
             release_json(a, sizeof(a), "v1.17.1", true, false),
             release_json(b, sizeof(b), "v1.18.0-beta.1", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.18.0-beta.1"));
}

TEST(tag_of_different_form_is_skipped) {
    char a[256], b[256], json[1024];
    snprintf(json, sizeof(json), "[%s, %s]",
             release_json(a, sizeof(a), "nightly", true, false),
             release_json(b, sizeof(b), "v1.17.0", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.17.0"));
}

TEST(draft_is_skipped) {
    char a[256], b[256], json[1024];
    snprintf(json, sizeof(json), "[%s, %s]",
             release_json(a, sizeof(a), "v1.19.0", true, true),
             release_json(b, sizeof(b), "v1.18.0", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.18.0"));
}

TEST(release_without_tag_is_skipped) {
    char b[256], json[1024];
    snprintf(json, sizeof(json),
             "[{\"draft\": false, \"assets\": [{\"name\": \"" ASSET "\", "
             "\"browser_download_url\": \"https://x/pak\"}]}, %s]",
             release_json(b, sizeof(b), "v1.18.0", true, false));
    CHECK(tag_is(select_tag(json, NULL), "v1.18.0"));
}

TEST(single_object_for_stable) {
    char a[256];
    CHECK(tag_is(select_tag(release_json(a, sizeof(a), "v1.17.0", true, false), NULL),
                 "v1.17.0"));
}

TEST(status_tells_the_reason) {
    char a[256];
    VersionReleaseStatus status;
    CHECK(select_tag(release_json(a, sizeof(a), "v1.17.0", true, false), &status));
    CHECK_EQ_INT(status, VERSION_RELEASE_FOUND);
    CHECK(!select_tag(release_json(a, sizeof(a), "nightly", true, false), &status));
    CHECK_EQ_INT(status, VERSION_RELEASE_NO_VERSION);
    CHECK(!select_tag(release_json(a, sizeof(a), "v1.17.0", false, false), &status));
    CHECK_EQ_INT(status, VERSION_RELEASE_NO_ASSET);
    CHECK(!select_tag("[]", &status));
    CHECK_EQ_INT(status, VERSION_RELEASE_NO_VERSION);
}

TEST(asset_url) {
    char a[256];
    JSON_Value* value = json_parse_string(release_json(a, sizeof(a), "v1.17.0", true, false));
    const char* url = Version_getAssetUrl(json_value_get_object(value), ASSET);
    CHECK(url && strcmp(url, "https://x/pak") == 0);
    CHECK(!Version_getAssetUrl(json_value_get_object(value), "missing.zip"));
    json_value_free(value);
}

int main(void) {
    RUN(valid_forms);
    RUN(invalid_forms);
    RUN(version_order);
    RUN(rc_forms);
    RUN(stable_after_its_pre_releases);
    RUN(pre_release_ahead_of_latest);
    RUN(newer_release_without_asset);
    RUN(late_fix_of_older_version);
    RUN(tag_of_different_form_is_skipped);
    RUN(draft_is_skipped);
    RUN(release_without_tag_is_skipped);
    RUN(single_object_for_stable);
    RUN(status_tells_the_reason);
    RUN(asset_url);
    return test_summary();
}
