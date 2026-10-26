// Tests for the text readers of src/radio_hls.c: the segment titles of a playlist
// and the ID3 tag at the start of a segment.

#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "radio_hls.h"
#include "radio_net.h"

// radio_hls.c fetches the variant of a master playlist. These tests give media
// playlists only.
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size,
                    char* content_type, int ct_size) {
    (void)url;
    (void)buffer;
    (void)buffer_size;
    (void)content_type;
    (void)ct_size;
    return -1;
}

// Write an ID3v2.4 tag with one frame into tag, and return its size
static int make_tag(uint8_t* tag, const char* frame_id, const uint8_t* body, uint32_t body_size) {
    uint32_t size = 10 + body_size;
    memcpy(tag, "ID3\x04\x00\x00", 6);
    tag[6] = (uint8_t)((size >> 21) & 0x7F);
    tag[7] = (uint8_t)((size >> 14) & 0x7F);
    tag[8] = (uint8_t)((size >> 7) & 0x7F);
    tag[9] = (uint8_t)(size & 0x7F);

    uint8_t* frame = tag + 10;
    memcpy(frame, frame_id, 4);
    frame[4] = (uint8_t)((body_size >> 21) & 0x7F);
    frame[5] = (uint8_t)((body_size >> 14) & 0x7F);
    frame[6] = (uint8_t)((body_size >> 7) & 0x7F);
    frame[7] = (uint8_t)(body_size & 0x7F);
    frame[8] = 0;
    frame[9] = 0;
    memcpy(frame + 10, body, body_size);
    return (int)(10 + size);
}

TEST(playlist_title_keeps_whole_characters) {
    // 50 CJK characters are 150 bytes, more than the title of a segment holds
    char cjk[151] = "";
    for (int index = 0; index < 50; index++) strcat(cjk, "周");

    char playlist[512];
    snprintf(playlist, sizeof(playlist),
             "#EXTM3U\n#EXTINF:10,title=\"%s\",artist=\"Caf\xE9\"\nseg1.aac\n", cjk);

    HLSContext* ctx = (HLSContext*)calloc(1, sizeof(HLSContext));
    CHECK_EQ_INT(radio_hls_parse_playlist(ctx, playlist, "http://host/"), 1);

    // 42 characters of 3 bytes fit in 127 bytes
    const char* title = ctx->segments[0].title;
    CHECK_EQ_SZ(strlen(title), 126);
    CHECK(strncmp(title, cjk, 126) == 0);
    CHECK(strcmp(ctx->segments[0].artist, "Café") == 0);
    free(ctx);
}

TEST(utf16_title_frame) {
    // Encoding 1, a little-endian mark, "周杰伦"
    const uint8_t body[] = {0x01, 0xFF, 0xFE, 0x68, 0x54, 0x70, 0x67, 0x26, 0x4F};
    uint8_t tag[64];
    int size = make_tag(tag, "TIT2", body, sizeof(body));

    char artist[64], title[64];
    CHECK_EQ_INT(radio_hls_parse_id3_metadata(tag, size, artist, sizeof(artist), title,
                                              sizeof(title)), size);
    CHECK(strcmp(title, "周杰伦") == 0);
    CHECK(artist[0] == '\0');
}

TEST(utf16_txxx_stream_title) {
    // Encoding 1. The description "StreamTitle" ends with a zero byte next to its
    // terminator, then the value "A - Song".
    uint8_t body[128];
    size_t length = 0;
    body[length++] = 0x01;
    const char* parts[] = {"StreamTitle", "A - Song"};
    for (int part = 0; part < 2; part++) {
        body[length++] = 0xFF;
        body[length++] = 0xFE;
        for (const char* c = parts[part]; *c; c++) {
            body[length++] = (uint8_t)*c;
            body[length++] = 0x00;
        }
        if (part == 0) {
            body[length++] = 0x00;
            body[length++] = 0x00;
        }
    }

    uint8_t tag[192];
    int size = make_tag(tag, "TXXX", body, (uint32_t)length);

    char artist[64], title[64];
    radio_hls_parse_id3_metadata(tag, size, artist, sizeof(artist), title, sizeof(title));
    CHECK(strcmp(artist, "A") == 0);
    CHECK(strcmp(title, "Song") == 0);
}

TEST(priv_with_owner) {
    const char body[] = "com.example.owner\0StreamTitle='Caf\xE9';";
    uint8_t tag[128];
    int size = make_tag(tag, "PRIV", (const uint8_t*)body, sizeof(body) - 1);

    char artist[64], title[64];
    radio_hls_parse_id3_metadata(tag, size, artist, sizeof(artist), title, sizeof(title));
    CHECK(strcmp(title, "Café") == 0);
}

TEST(priv_with_no_owner) {
    const char body[] = "StreamTitle='Artist - Song';";
    uint8_t tag[128];
    int size = make_tag(tag, "PRIV", (const uint8_t*)body, sizeof(body) - 1);

    char artist[64], title[64];
    radio_hls_parse_id3_metadata(tag, size, artist, sizeof(artist), title, sizeof(title));
    CHECK(strcmp(artist, "Artist") == 0);
    CHECK(strcmp(title, "Song") == 0);
}

TEST(priv_with_no_closing_quote) {
    const char body[] = "StreamTitle='Song";
    uint8_t tag[128];
    int size = make_tag(tag, "PRIV", (const uint8_t*)body, sizeof(body) - 1);

    char artist[64], title[64];
    radio_hls_parse_id3_metadata(tag, size, artist, sizeof(artist), title, sizeof(title));
    CHECK(title[0] == '\0');
}

TEST(playlist_line_longer_than_the_line_buffer) {
    // 400 CJK characters are 1200 bytes, more than one line buffer holds
    char cjk[1201] = "";
    for (int index = 0; index < 400; index++) strcat(cjk, "周");

    char playlist[2600];
    snprintf(playlist, sizeof(playlist),
             "#EXTM3U\n#EXTINF:10,title=\"%s\",artist=\"%s\"\nseg1.aac\n", cjk, cjk);

    HLSContext* ctx = (HLSContext*)calloc(1, sizeof(HLSContext));
    CHECK_EQ_INT(radio_hls_parse_playlist(ctx, playlist, "http://host/"), 1);
    CHECK_EQ_SZ(strlen(ctx->segments[0].title), 126);
    CHECK(strncmp(ctx->segments[0].title, cjk, 126) == 0);
    CHECK_EQ_SZ(strlen(ctx->segments[0].artist), 126);
    CHECK(strncmp(ctx->segments[0].artist, cjk, 126) == 0);
    free(ctx);
}

// Write an ID3v2.3 tag with one frame whose header declares frame_size, but whose body
// holds only body_size bytes. Returns the size of the tag.
static int make_v23_tag(uint8_t* tag, const char* frame_id, uint32_t frame_size,
                        const uint8_t* body, uint32_t body_size) {
    uint32_t size = 10 + body_size;
    memcpy(tag, "ID3\x03\x00\x00", 6);
    tag[6] = 0;
    tag[7] = 0;
    tag[8] = (uint8_t)((size >> 7) & 0x7F);
    tag[9] = (uint8_t)(size & 0x7F);

    uint8_t* frame = tag + 10;
    memcpy(frame, frame_id, 4);
    frame[4] = (uint8_t)(frame_size >> 24);
    frame[5] = (uint8_t)(frame_size >> 16);
    frame[6] = (uint8_t)(frame_size >> 8);
    frame[7] = (uint8_t)frame_size;
    frame[8] = 0;
    frame[9] = 0;
    memcpy(frame + 10, body, body_size);
    return (int)(10 + size);
}

TEST(frame_size_past_the_tag) {
    const uint8_t text[] = {0x01, 'a', 0x00};
    const char* frame_ids[] = {"TIT2", "TPE1", "TXXX", "PRIV"};
    // 0xFFFFFFF0 wraps a 32-bit sum of the position and the size
    const uint32_t frame_sizes[] = {0xFFFFFFF0u, 0x7FFFFFFFu, 4};

    for (int id = 0; id < 4; id++) {
        for (int size = 0; size < 3; size++) {
            uint8_t tag[64];
            int length = make_v23_tag(tag, frame_ids[id], frame_sizes[size], text,
                                      sizeof(text));

            char artist[64], title[64];
            CHECK_EQ_INT(radio_hls_parse_id3_metadata(tag, length, artist, sizeof(artist),
                                                      title, sizeof(title)), length);
            CHECK(title[0] == '\0');
            CHECK(artist[0] == '\0');
        }
    }
}

// Parse a tag with one frame, and check that the artist is the 250 bytes of 125 'é' and
// the title is "Song"
static void check_expanded_artist(const char* frame_id, const uint8_t* body, size_t body_size) {
    uint8_t tag[256];
    int size = make_tag(tag, frame_id, body, (uint32_t)body_size);

    char artist[256], title[256];
    radio_hls_parse_id3_metadata(tag, size, artist, sizeof(artist), title, sizeof(title));
    CHECK_EQ_SZ(strlen(artist), 250);
    CHECK(strcmp(title, "Song") == 0);
}

// 125 Latin-1 bytes become 250 bytes of UTF-8, thus the separator falls past the end
// of a 256-byte field
TEST(latin1_expansion_keeps_the_separator) {
    char value[160] = "";
    for (int index = 0; index < 125; index++) strcat(value, "\xE9");
    strcat(value, " - Song");

    // TXXX in encoding 0: the encoding byte, the description and its terminator, the value
    uint8_t txxx[192];
    txxx[0] = 0x00;
    memcpy(txxx + 1, "StreamTitle", 12);
    memcpy(txxx + 13, value, strlen(value));
    check_expanded_artist("TXXX", txxx, 13 + strlen(value));

    char priv[192];
    snprintf(priv, sizeof(priv), "StreamTitle='%s';", value);
    check_expanded_artist("PRIV", (const uint8_t*)priv, strlen(priv));
}

int main(void) {
    RUN(playlist_title_keeps_whole_characters);
    RUN(utf16_title_frame);
    RUN(utf16_txxx_stream_title);
    RUN(priv_with_owner);
    RUN(priv_with_no_owner);
    RUN(priv_with_no_closing_quote);
    RUN(playlist_line_longer_than_the_line_buffer);
    RUN(frame_size_past_the_tag);
    RUN(latin1_expansion_keeps_the_separator);
    return test_summary();
}
