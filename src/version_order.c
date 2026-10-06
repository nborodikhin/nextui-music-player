#include "version_order.h"

#include <stddef.h>
#include <string.h>

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_identifier_char(char c) {
    return is_digit(c) || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '-';
}

static bool read_number(const char** text) {
    const char* start = *text;
    while (is_digit(**text)) (*text)++;
    return *text != start && !(*text - start > 1 && *start == '0');
}

static bool read_identifiers(const char** text, bool prerelease) {
    do {
        const char* start = *text;
        bool numeric = true;
        while (is_identifier_char(**text)) {
            if (!is_digit(**text)) numeric = false;
            (*text)++;
        }
        if (*text == start) return false;
        if (prerelease && numeric && *text - start > 1 && *start == '0') return false;
        if (**text != '.') return true;
        (*text)++;
    } while (true);
}

bool Version_isValid(const char* version) {
    if (!version) return false;
    const char* text = version;
    if (*text == 'v') text++;
    for (int i = 0; i < 3; i++) {
        if (!read_number(&text)) return false;
        if (i < 2 && *text++ != '.') return false;
    }
    if (*text == '-') {
        text++;
        if (!read_identifiers(&text, true)) return false;
    }
    if (*text == '+') {
        text++;
        if (!read_identifiers(&text, false)) return false;
    }
    return *text == '\0';
}

static int compare_span(const char* left, size_t left_length,
                        const char* right, size_t right_length, bool numeric) {
    if (numeric && left_length != right_length) return left_length > right_length ? 1 : -1;
    size_t shared_length = left_length < right_length ? left_length : right_length;
    int order = memcmp(left, right, shared_length);
    if (order) return order;
    return (left_length > right_length) - (left_length < right_length);
}

static const char* identifier_end(const char* text, bool* numeric) {
    *numeric = true;
    while (*text && *text != '.' && *text != '+') {
        if (!is_digit(*text)) *numeric = false;
        text++;
    }
    return text;
}

int Version_compare(const char* left, const char* right) {
    bool left_valid = Version_isValid(left);
    bool right_valid = Version_isValid(right);
    if (!left_valid || !right_valid) return (int)left_valid - (int)right_valid;

    // skip "v" prefix
    if (*left == 'v') left++;
    if (*right == 'v') right++;

    // Semver comparison, see https://semver.org/

    // 3-part version MAJOR, MINOR, PATCH
    for (int part = 0; part < 3; part++) {
        const char* left_end = left;
        const char* right_end = right;
        while (is_digit(*left_end)) left_end++;
        while (is_digit(*right_end)) right_end++;
        int order = compare_span(left, left_end - left, right, right_end - right, true);
        if (order) return order;
        left = left_end;
        right = right_end;
        if (part < 2) { left++; right++; }
    }

    // Pre-release
    bool left_prerelease = *left == '-';
    bool right_prerelease = *right == '-';
    if (!left_prerelease || !right_prerelease) {
        return (int)right_prerelease - (int)left_prerelease;
    }
    left++;
    right++;
    while (true) {
        bool left_numeric, right_numeric;
        const char* left_end = identifier_end(left, &left_numeric);
        const char* right_end = identifier_end(right, &right_numeric);
        if (left_numeric != right_numeric) return left_numeric ? -1 : 1;
        int order = compare_span(left, left_end - left, right, right_end - right, left_numeric);
        if (order) return order;
        bool left_has_more = *left_end == '.';
        bool right_has_more = *right_end == '.';
        if (!left_has_more || !right_has_more) return (int)left_has_more - (int)right_has_more;
        left = left_end + 1;
        right = right_end + 1;
    }
}
