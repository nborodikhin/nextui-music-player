// Tests for src/utf8.c.
//
// The copier is what stands between text from a file - or a program that cut a
// name in half - and a name the app writes to disk or draws.

#include <string.h>

#include "test.h"
#include "utf8.h"

TEST(char_bytes_by_lead) {
    CHECK(UTF8_charBytes("a") == 1);
    CHECK(UTF8_charBytes("é") == 2);
    CHECK(UTF8_charBytes("ф") == 2);
    CHECK(UTF8_charBytes("€") == 3);
    CHECK(UTF8_charBytes("😀") == 4);
    CHECK(UTF8_charBytes("") == 0);
    CHECK(UTF8_charBytes(NULL) == 0);
}

TEST(char_bytes_rejects_malformed) {
    // A continuation byte cannot start a character
    CHECK(UTF8_charBytes("\x80") == 0);
    // A lead byte promising more than the string holds
    CHECK(UTF8_charBytes("\xD1") == 0);
    CHECK(UTF8_charBytes("\xE2\x82") == 0);
    // A continuation byte that is not one
    CHECK(UTF8_charBytes("\xD1z") == 0);
    CHECK(UTF8_charBytes("\xFF") == 0);
}

TEST(codepoint) {
    CHECK(UTF8_codepoint("a") == 0x61);
    CHECK(UTF8_codepoint("é") == 0xE9);
    CHECK(UTF8_codepoint("ф") == 0x444);
    CHECK(UTF8_codepoint("€") == 0x20AC);
    CHECK(UTF8_codepoint("😀") == 0x1F600);

    // Malformed input reads as nothing, the same as the end of a string
    CHECK(UTF8_codepoint("\xD1") == 0);
    CHECK(UTF8_codepoint("\x80") == 0);
    CHECK(UTF8_codepoint("") == 0);
    CHECK(UTF8_codepoint(NULL) == 0);
}

TEST(copy_keeps_whole_characters) {
    char out[16];

    CHECK(UTF8_copy(out, sizeof(out), "abc") == 3);
    CHECK(strcmp(out, "abc") == 0);

    // "фыв" is six bytes; a five byte budget takes two characters
    CHECK(UTF8_copy(out, 5, "фыв") == 4);
    CHECK(strcmp(out, "фы") == 0);

    // One byte short of a character leaves it out rather than cutting it
    CHECK(UTF8_copy(out, 4, "aфы") == 3);
    CHECK(strcmp(out, "aф") == 0);
}

TEST(copy_stops_at_malformed_input) {
    char out[16];

    // A character cut short by the terminator is not copied
    CHECK(UTF8_copy(out, sizeof(out), "ab\xD1") == 2);
    CHECK(strcmp(out, "ab") == 0);

    // Nor is a stray continuation byte
    CHECK(UTF8_copy(out, sizeof(out), "ab\x80z") == 2);
    CHECK(strcmp(out, "ab") == 0);
}

TEST(copy_handles_combining_marks) {
    char out[16];

    // o + combining grave stays together
    CHECK(UTF8_copy(out, sizeof(out), "o\xCC\x80") == 3);
    CHECK(strcmp(out, "o\xCC\x80") == 0);

    // A mark with nothing to attach to is dropped
    CHECK(UTF8_copy(out, sizeof(out), "\xCC\x80o") == 1);
    CHECK(strcmp(out, "o") == 0);

    // A budget that fits the base but not its mark drops both, so the copy
    // never ends on a half-formed cluster
    CHECK(UTF8_copy(out, 3, "ao\xCC\x80") == 1);
    CHECK(strcmp(out, "a") == 0);
}

TEST(copy_always_terminates) {
    char out[8];

    memset(out, 'x', sizeof(out));
    CHECK(UTF8_copy(out, sizeof(out), NULL) == 0);
    CHECK(out[0] == '\0');

    memset(out, 'x', sizeof(out));
    CHECK(UTF8_copy(out, 1, "abc") == 0);
    CHECK(out[0] == '\0');

    CHECK(UTF8_copy(NULL, 4, "abc") == 0);
}

TEST(last_char_bytes) {
    CHECK(UTF8_lastCharBytes("abc") == 1);
    CHECK(UTF8_lastCharBytes("abф") == 2);
    CHECK(UTF8_lastCharBytes("ab€") == 3);

    // A mark goes with the character it sits on
    CHECK(UTF8_lastCharBytes("ao\xCC\x80") == 3);

    CHECK(UTF8_lastCharBytes("") == 0);
    CHECK(UTF8_lastCharBytes(NULL) == 0);
}

TEST(from_utf16_both_byte_orders) {
    char out[32];

    // "周杰伦" little-endian
    const uint8_t little[] = {0x68, 0x54, 0x70, 0x67, 0x26, 0x4F};
    CHECK_EQ_SZ(UTF8_fromUtf16(out, sizeof(out), little, sizeof(little), false), 9);
    CHECK(strcmp(out, "周杰伦") == 0);

    const uint8_t big[] = {0x00, 'C', 0x00, 'a', 0x00, 'f', 0x00, 0xE9};
    CHECK_EQ_SZ(UTF8_fromUtf16(out, sizeof(out), big, sizeof(big), true), 5);
    CHECK(strcmp(out, "Café") == 0);
}

TEST(from_utf16_surrogates) {
    char out[32];

    const uint8_t pair[] = {0xD8, 0x3D, 0xDE, 0x00};
    CHECK_EQ_SZ(UTF8_fromUtf16(out, sizeof(out), pair, sizeof(pair), true), 4);
    CHECK(strcmp(out, "😀") == 0);

    // A high surrogate with no low one, and a low one alone
    const uint8_t lone_high[] = {0x00, 0xD8, 'a', 0x00};
    UTF8_fromUtf16(out, sizeof(out), lone_high, sizeof(lone_high), false);
    CHECK(strcmp(out, "\xEF\xBF\xBD" "a") == 0);

    const uint8_t lone_low[] = {0x00, 0xDC};
    UTF8_fromUtf16(out, sizeof(out), lone_low, sizeof(lone_low), false);
    CHECK(strcmp(out, "\xEF\xBF\xBD") == 0);
}

TEST(from_utf16_stops) {
    char out[32];

    // At a NUL code unit, and not at a zero byte of a character
    const uint8_t text[] = {'A', 0x00, 0x00, 0x00, 'X', 0x00};
    CHECK_EQ_SZ(UTF8_fromUtf16(out, sizeof(out), text, sizeof(text), false), 1);
    CHECK(strcmp(out, "A") == 0);

    // At a byte of an odd length that is not a whole code unit
    CHECK_EQ_SZ(UTF8_fromUtf16(out, sizeof(out), text, 1, false), 0);
    CHECK(out[0] == '\0');

    // Before a character that does not fit, not inside it
    const uint8_t cjk[] = {0x68, 0x54, 0x70, 0x67};
    CHECK_EQ_SZ(UTF8_fromUtf16(out, 6, cjk, sizeof(cjk), false), 3);
    CHECK(strcmp(out, "周") == 0);
}

TEST(from_unknown_keeps_utf8) {
    char out[32];

    const char* cjk = "双截棍";
    CHECK_EQ_SZ(UTF8_fromUnknown(out, sizeof(out), (const uint8_t*)cjk, strlen(cjk)), 9);
    CHECK(strcmp(out, cjk) == 0);

    // A cut keeps whole characters
    CHECK_EQ_SZ(UTF8_fromUnknown(out, 7, (const uint8_t*)cjk, strlen(cjk)), 6);
    CHECK(strcmp(out, "双截") == 0);
}

TEST(from_unknown_reads_latin1) {
    char out[32];

    const uint8_t cafe[] = {'C', 'a', 'f', 0xE9};
    CHECK_EQ_SZ(UTF8_fromUnknown(out, sizeof(out), cafe, sizeof(cafe)), 5);
    CHECK(strcmp(out, "Café") == 0);

    // A sequence that src_len cuts short is not UTF-8
    const char* cjk = "周";
    UTF8_fromUnknown(out, sizeof(out), (const uint8_t*)cjk, 2);
    CHECK(strcmp(out, "\xC3\xA5\xC2\x91") == 0);

    // An overlong form, an encoded surrogate, a codepoint above U+10FFFF
    const uint8_t overlong[] = {0xC0, 0xAF};
    UTF8_fromUnknown(out, sizeof(out), overlong, sizeof(overlong));
    CHECK(strcmp(out, "\xC3\x80\xC2\xAF") == 0);

    const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    UTF8_fromUnknown(out, sizeof(out), surrogate, sizeof(surrogate));
    CHECK(strcmp(out, "\xC3\xAD\xC2\xA0\xC2\x80") == 0);

    const uint8_t too_high[] = {0xF4, 0x90, 0x80, 0x80};
    UTF8_fromUnknown(out, sizeof(out), too_high, sizeof(too_high));
    CHECK(strcmp(out, "\xC3\xB4\xC2\x90\xC2\x80\xC2\x80") == 0);
}

TEST(from_unknown_decides_on_the_whole_text) {
    char out[300];

    // Bytes after the NUL do not change the charset
    const uint8_t after_nul[] = {0xC3, 0xA9, 0x00, 0xFF};
    CHECK_EQ_SZ(UTF8_fromUnknown(out, sizeof(out), after_nul, sizeof(after_nul)), 2);
    CHECK(strcmp(out, "é") == 0);

    // A destination that cuts the text does not change the charset
    uint8_t long_text[256];
    memset(long_text, 'a', 254);
    long_text[254] = 0xC3;
    long_text[255] = 0xA9;
    CHECK_EQ_SZ(UTF8_fromUnknown(out, 256, long_text, sizeof(long_text)), 254);
    CHECK(strchr(out, '\xC3') == NULL);
}

TEST(from_converters_always_terminate) {
    char out[8];
    const uint8_t text[] = {'a', 0x00};

    memset(out, 'x', sizeof(out));
    CHECK_EQ_SZ(UTF8_fromUnknown(out, sizeof(out), NULL, 4), 0);
    CHECK(out[0] == '\0');

    memset(out, 'x', sizeof(out));
    CHECK_EQ_SZ(UTF8_fromUtf16(out, 1, text, sizeof(text), false), 0);
    CHECK(out[0] == '\0');

    CHECK_EQ_SZ(UTF8_fromUnknown(NULL, 4, text, sizeof(text)), 0);
    CHECK_EQ_SZ(UTF8_fromUtf16(NULL, 4, text, sizeof(text), false), 0);
}

int main(void) {
    RUN(char_bytes_by_lead);
    RUN(char_bytes_rejects_malformed);
    RUN(codepoint);
    RUN(copy_keeps_whole_characters);
    RUN(copy_stops_at_malformed_input);
    RUN(copy_handles_combining_marks);
    RUN(copy_always_terminates);
    RUN(last_char_bytes);
    RUN(from_utf16_both_byte_orders);
    RUN(from_utf16_surrogates);
    RUN(from_utf16_stops);
    RUN(from_unknown_keeps_utf8);
    RUN(from_unknown_reads_latin1);
    RUN(from_unknown_decides_on_the_whole_text);
    RUN(from_converters_always_terminate);
    return test_summary();
}
