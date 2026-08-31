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

int main(void) {
    RUN(char_bytes_by_lead);
    RUN(char_bytes_rejects_malformed);
    RUN(codepoint);
    RUN(copy_keeps_whole_characters);
    RUN(copy_stops_at_malformed_input);
    RUN(copy_handles_combining_marks);
    RUN(copy_always_terminates);
    RUN(last_char_bytes);
    return test_summary();
}
