// Tests for src/icy.c.

#include <string.h>

#include "test.h"
#include "icy.h"

static bool parse(const char* block, char* artist, size_t artist_size, char* title,
                  size_t title_size) {
    return ICY_parseStreamTitle((const uint8_t*)block, strlen(block), artist, artist_size,
                                title, title_size);
}

TEST(utf8_artist_and_title) {
    char artist[64], title[64];

    CHECK(parse("StreamTitle='周杰伦 - 双截棍';StreamUrl='';", artist, sizeof(artist), title,
                sizeof(title)));
    CHECK(strcmp(artist, "周杰伦") == 0);
    CHECK(strcmp(title, "双截棍") == 0);
}

TEST(latin1_title) {
    char artist[64], title[64];

    CHECK(parse("StreamTitle='Caf\xE9';", artist, sizeof(artist), title, sizeof(title)));
    CHECK(artist[0] == '\0');
    CHECK(strcmp(title, "Café") == 0);
}

TEST(long_title_keeps_whole_characters) {
    char block[128] = "StreamTitle='";
    for (int index = 0; index < 10; index++) strcat(block, "周");
    strcat(block, "';");

    char artist[64], title[8];
    CHECK(parse(block, artist, sizeof(artist), title, sizeof(title)));
    CHECK(strcmp(title, "周周") == 0);
}

TEST(long_artist_keeps_whole_characters) {
    char artist[8], title[64];

    CHECK(parse("StreamTitle='周杰伦周杰伦 - Song';", artist, sizeof(artist), title,
                sizeof(title)));
    CHECK(strcmp(artist, "周杰") == 0);
    CHECK(strcmp(title, "Song") == 0);
}

TEST(title_attribute) {
    char artist[64], title[64];

    CHECK(parse("StreamTitle='text=\"Song\" song_spot=\"M\"';", artist, sizeof(artist), title,
                sizeof(title)));
    CHECK(strcmp(title, "Song") == 0);

    CHECK(parse("StreamTitle='a=\"b\"';", artist, sizeof(artist), title, sizeof(title)));
    CHECK(title[0] == '\0');
}

TEST(no_stream_title) {
    char artist[64] = "kept", title[64] = "kept";

    CHECK(!parse("StreamUrl='x';", artist, sizeof(artist), title, sizeof(title)));
    CHECK(!parse("StreamTitle='Song", artist, sizeof(artist), title, sizeof(title)));
    CHECK(strcmp(artist, "kept") == 0);
    CHECK(strcmp(title, "kept") == 0);
}

TEST(latin1_expansion_keeps_the_attribute) {
    // 125 Latin-1 bytes become 250 bytes of UTF-8
    char block[256] = "StreamTitle='text=\"";
    for (int index = 0; index < 125; index++) strcat(block, "\xE9");
    strcat(block, "\"';");

    char artist[256], title[256];
    CHECK(parse(block, artist, sizeof(artist), title, sizeof(title)));
    CHECK_EQ_SZ(strlen(title), 250);
    CHECK(strncmp(title, "é", 2) == 0);
}

TEST(latin1_expansion_keeps_the_separator) {
    char block[256] = "StreamTitle='";
    for (int index = 0; index < 125; index++) strcat(block, "\xE9");
    strcat(block, " - Song';");

    char artist[256], title[256];
    CHECK(parse(block, artist, sizeof(artist), title, sizeof(title)));
    CHECK_EQ_SZ(strlen(artist), 250);
    CHECK(strcmp(title, "Song") == 0);

    // A small artist field takes whole characters and leaves the title whole
    char short_artist[8];
    CHECK(parse(block, short_artist, sizeof(short_artist), title, sizeof(title)));
    CHECK(strcmp(short_artist, "ééé") == 0);
    CHECK(strcmp(title, "Song") == 0);
}

int main(void) {
    RUN(utf8_artist_and_title);
    RUN(latin1_title);
    RUN(long_title_keeps_whole_characters);
    RUN(long_artist_keeps_whole_characters);
    RUN(title_attribute);
    RUN(no_stream_title);
    RUN(latin1_expansion_keeps_the_attribute);
    RUN(latin1_expansion_keeps_the_separator);
    return test_summary();
}
