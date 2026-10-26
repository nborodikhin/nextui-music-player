// Tests for src/id3.c.
//
// The frames of the sample file are those of a real MP3 whose tags showed as
// "???": ID3v2.3 text in UTF-16 with a byte order mark, and an ID3v1 tag in which
// the tagger wrote '?' for each CJK character.

#include <string.h>

#include "test.h"
#include "id3.h"

// Frame bodies of TPE1, TIT2 and TALB: the encoding byte, then the text
static const uint8_t SAMPLE_ARTIST[] = {0x01, 0xFF, 0xFE, 0x68, 0x54, 0x70, 0x67, 0x26, 0x4F};
static const uint8_t SAMPLE_TITLE[]  = {0x01, 0xFF, 0xFE, 0xCC, 0x53, 0x2A, 0x62, 0xCD, 0x68};
static const uint8_t SAMPLE_ALBUM[]  = {0x01, 0xFF, 0xFE, 0x03, 0x83, 0x79, 0x72, 0x7F, 0x89};

static size_t decode_frame(char* dst, size_t dst_size, const uint8_t* frame, size_t frame_size) {
    return ID3_textToUtf8(dst, dst_size, frame[0], frame + 1, frame_size - 1);
}

TEST(text_of_the_sample_file) {
    char out[64];

    decode_frame(out, sizeof(out), SAMPLE_ARTIST, sizeof(SAMPLE_ARTIST));
    CHECK(strcmp(out, "周杰伦") == 0);
    decode_frame(out, sizeof(out), SAMPLE_TITLE, sizeof(SAMPLE_TITLE));
    CHECK(strcmp(out, "双截棍") == 0);
    decode_frame(out, sizeof(out), SAMPLE_ALBUM, sizeof(SAMPLE_ALBUM));
    CHECK(strcmp(out, "范特西") == 0);
}

TEST(text_byte_order) {
    char out[64];

    // Encoding 1 with a big-endian mark
    const uint8_t marked[] = {0xFE, 0xFF, 0x00, 'A', 0x00, 0xE9};
    ID3_textToUtf8(out, sizeof(out), 1, marked, sizeof(marked));
    CHECK(strcmp(out, "Aé") == 0);

    // Encoding 1 with no mark is little-endian
    const uint8_t little[] = {'A', 0x00, 0xE9, 0x00};
    ID3_textToUtf8(out, sizeof(out), 1, little, sizeof(little));
    CHECK(strcmp(out, "Aé") == 0);

    // Encoding 2 with no mark is big-endian
    const uint8_t big[] = {0x00, 'A', 0x00, 0xE9};
    ID3_textToUtf8(out, sizeof(out), 2, big, sizeof(big));
    CHECK(strcmp(out, "Aé") == 0);

    // A mark also wins in encoding 2
    const uint8_t little_mark[] = {0xFF, 0xFE, 'A', 0x00};
    ID3_textToUtf8(out, sizeof(out), 2, little_mark, sizeof(little_mark));
    CHECK(strcmp(out, "A") == 0);
}

TEST(text_of_one_byte_encodings) {
    char out[64];

    const uint8_t latin1[] = {'C', 'a', 'f', 0xE9};
    ID3_textToUtf8(out, sizeof(out), 0, latin1, sizeof(latin1));
    CHECK(strcmp(out, "Café") == 0);

    // Taggers write UTF-8 and declare ISO-8859-1
    const char* utf8 = "Café";
    ID3_textToUtf8(out, sizeof(out), 0, (const uint8_t*)utf8, strlen(utf8));
    CHECK(strcmp(out, "Café") == 0);

    ID3_textToUtf8(out, sizeof(out), 3, (const uint8_t*)utf8, strlen(utf8));
    CHECK(strcmp(out, "Café") == 0);

    // Bytes after the terminator do not change the text
    const uint8_t after_nul[] = {0xC3, 0xA9, 0x00, 0xFF};
    ID3_textToUtf8(out, sizeof(out), 3, after_nul, sizeof(after_nul));
    CHECK(strcmp(out, "é") == 0);
}

TEST(skip_text) {
    // "Hi" and a terminator in each encoding, then a value
    const uint8_t latin1[] = {'H', 'i', 0x00, 'v'};
    CHECK_EQ_SZ(ID3_skipText(0, latin1, sizeof(latin1)), 3);

    // The zero byte of the last character sits next to the terminator: the
    // two NUL bytes at offsets 3 and 4 are not one code unit
    const uint8_t little[] = {0xFF, 0xFE, 'H', 0x00, 'i', 0x00, 0x00, 0x00, 'v', 0x00};
    CHECK_EQ_SZ(ID3_skipText(1, little, sizeof(little)), 8);

    const uint8_t big[] = {0x00, 'H', 0x00, 'i', 0x00, 0x00, 0x00, 'v'};
    CHECK_EQ_SZ(ID3_skipText(2, big, sizeof(big)), 6);

    // No terminator: the text takes all of the bytes
    const uint8_t open_text[] = {0x00, 'H', 0x00, 'i'};
    CHECK_EQ_SZ(ID3_skipText(2, open_text, sizeof(open_text)), 4);
    CHECK_EQ_SZ(ID3_skipText(0, latin1, 2), 2);

    // An odd byte at the end is not a terminator
    const uint8_t odd[] = {'H', 0x00, 0x00};
    CHECK_EQ_SZ(ID3_skipText(1, odd, sizeof(odd)), 3);
}

static void make_v1_tag(uint8_t* tag, const char* title, const char* artist, const char* album) {
    memset(tag, 0, ID3_V1_TAG_SIZE);
    memcpy(tag, "TAG", 3);
    memcpy(tag + 3, title, strlen(title));
    memcpy(tag + 33, artist, strlen(artist));
    memcpy(tag + 63, album, strlen(album));
}

TEST(v1_fields) {
    uint8_t tag[ID3_V1_TAG_SIZE];
    char out[64];

    make_v1_tag(tag, "Song   ", "Artist", "Caf\xE9");
    CHECK(ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
    CHECK(strcmp(out, "Song") == 0);
    CHECK(ID3_readV1Field(out, sizeof(out), tag, ID3_V1_ARTIST));
    CHECK(strcmp(out, "Artist") == 0);
    CHECK(ID3_readV1Field(out, sizeof(out), tag, ID3_V1_ALBUM));
    CHECK(strcmp(out, "Café") == 0);

    make_v1_tag(tag, "Caf\xC3\xA9", "", "");
    CHECK(ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
    CHECK(strcmp(out, "Café") == 0);
}

TEST(v1_fields_with_no_text) {
    uint8_t tag[ID3_V1_TAG_SIZE];
    char out[64];

    // The ID3v1 tag of the sample file
    make_v1_tag(tag, "???", "???", "???");
    strcpy(out, "kept");
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_ARTIST));
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_ALBUM));
    CHECK(strcmp(out, "kept") == 0);

    make_v1_tag(tag, "? ?  ", "   ", "");
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_ARTIST));
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_ALBUM));

    // A real question mark is text
    make_v1_tag(tag, "Who?", "", "");
    CHECK(ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
    CHECK(strcmp(out, "Who?") == 0);

    // No "TAG" mark
    make_v1_tag(tag, "Song", "", "");
    tag[0] = 'X';
    CHECK(!ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
}

TEST(v1_field_fills_all_thirty_bytes) {
    uint8_t tag[ID3_V1_TAG_SIZE];
    char out[64];

    // A field with no terminator ends at the next field
    make_v1_tag(tag, "abcdefghijabcdefghijabcdefghij", "Artist", "");
    CHECK(ID3_readV1Field(out, sizeof(out), tag, ID3_V1_TITLE));
    CHECK(strcmp(out, "abcdefghijabcdefghijabcdefghij") == 0);
}

int main(void) {
    RUN(text_of_the_sample_file);
    RUN(text_byte_order);
    RUN(text_of_one_byte_encodings);
    RUN(skip_text);
    RUN(v1_fields);
    RUN(v1_fields_with_no_text);
    RUN(v1_field_fills_all_thirty_bytes);
    return test_summary();
}
