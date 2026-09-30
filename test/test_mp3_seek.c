// Tests for src/mp3_seek.c - the seek point from the table in an MP3 file.
//
// The files are synthetic: an ID3v2 tag, a header frame (Info, Xing or VBRI),
// then frames with real headers and empty data. The tests pin that a point is on
// a real frame header, that its index is the index of that frame, that it is not
// past the target, and that a file without a table, or with a table that does
// not agree with the decoder, gives no point.

#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "mp3_seek.h"

// MPEG-1 Layer III, 128 kbps, 44.1 kHz, mono: 417 or 418 bytes, 1152 samples
#define HDR_NO_PAD  0xFF, 0xFB, 0x90, 0xC4
#define HDR_PAD     0xFF, 0xFB, 0x92, 0xC4
#define SIDE_INFO   17
#define SPF         1152
#define FRAME_MEAN  (144.0 * 128000 / 44100)

#define ID3_BYTES   1000
#define FRAMES      4000

typedef struct {
    uint8_t* data;
    int64_t size;
    int64_t first_frame;     // the first audio frame
    int64_t frame_pos[FRAMES + 1];
} File;

static int64_t file_read(void* ctx, int64_t offset, uint8_t* buf, int64_t len) {
    File* f = (File*)ctx;
    if (offset >= f->size) return 0;
    if (offset + len > f->size) len = f->size - offset;
    memcpy(buf, f->data + offset, (size_t)len);
    return len;
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

// Write the ID3v2 tag, then `FRAMES` audio frames after `header_bytes` bytes of
// header frame, with the padding of a LAME encoder. The caller fills the header frame.
static void make_file(File* f, int header_bytes) {
    memset(f, 0, sizeof(*f));
    f->size = ID3_BYTES + header_bytes + (int64_t)(FRAMES * FRAME_MEAN) + 1000;
    f->data = calloc(1, (size_t)f->size);

    uint8_t* id3 = f->data;
    memcpy(id3, "ID3\x03\x00\x00", 6);
    int body = ID3_BYTES - 10;
    id3[6] = (body >> 21) & 0x7F; id3[7] = (body >> 14) & 0x7F;
    id3[8] = (body >> 7) & 0x7F;  id3[9] = body & 0x7F;

    f->first_frame = ID3_BYTES + header_bytes;
    int64_t pos = f->first_frame;
    double fraction = 0;
    for (int n = 0; n < FRAMES; n++) {
        static const uint8_t no_pad[4] = {HDR_NO_PAD}, pad[4] = {HDR_PAD};
        fraction += FRAME_MEAN - 417;
        bool padded = fraction >= 1.0;
        if (padded) fraction -= 1.0;
        memcpy(f->data + pos, padded ? pad : no_pad, 4);
        f->frame_pos[n] = pos;
        pos += padded ? 418 : 417;
    }
    f->frame_pos[FRAMES] = pos;
    f->size = pos;
}

static void make_info(File* f, const char* id, bool with_toc) {
    make_file(f, 417);
    uint8_t* h = f->data + ID3_BYTES;
    static const uint8_t hdr[4] = {HDR_NO_PAD};
    memcpy(h, hdr, 4);
    uint8_t* tag = h + 4 + SIDE_INFO;
    memcpy(tag, id, 4);
    put_be32(tag + 4, with_toc ? 0x7 : 0x3);
    put_be32(tag + 8, FRAMES);
    put_be32(tag + 12, (uint32_t)(f->size - ID3_BYTES));
    if (with_toc) {
        // The TOC of a constant bitrate: byte position is proportional to time
        for (int i = 0; i < 100; i++) tag[16 + i] = (uint8_t)(i * 256 / 100);
    }
}

// The index of the audio frame that starts at `pos`, or -1
static int frame_at(const File* f, int64_t pos) {
    for (int n = 0; n < FRAMES; n++) if (f->frame_pos[n] == pos) return n;
    return -1;
}

TEST(info_header_gives_cbr) {
    File f; make_info(&f, "Info", false);
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    CHECK_EQ_INT(info.kind, MP3_SEEK_CBR);
    CHECK_EQ_INT((int)info.first_frame, (int)f.first_frame);
    CHECK_EQ_INT((int)info.frame_count, FRAMES);
    CHECK_EQ_INT(info.samples_per_frame, SPF);
    Mp3Seek_free(&info);
    free(f.data);
}

// Each point is on a real frame header, carries the index of that frame, and
// is MP3_SEEK_LEAD_FRAMES or MP3_SEEK_LEAD_FRAMES + 1 frames before the target
TEST(cbr_point_is_the_exact_frame) {
    File f; make_info(&f, "Info", false);
    Mp3SeekInfo info;
    Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame);
    for (int target_mp3 = 10; target_mp3 < FRAMES - 10; target_mp3 += 37) {
        int64_t target = (int64_t)target_mp3 * SPF + 500;
        Mp3SeekPoint p;
        CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, target, &p));
        int n = frame_at(&f, p.byte_pos);
        CHECK(n >= 0);
        CHECK_EQ_INT((int)(p.pcm_frame / SPF), n);
        CHECK(p.pcm_frame <= target);
        CHECK(target_mp3 - n >= MP3_SEEK_LEAD_FRAMES && target_mp3 - n <= MP3_SEEK_LEAD_FRAMES + 1);
    }
    Mp3Seek_free(&info);
    free(f.data);
}

TEST(target_near_the_start_gives_no_point) {
    File f; make_info(&f, "Info", false);
    Mp3SeekInfo info;
    Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame);
    Mp3SeekPoint p;
    CHECK(!Mp3Seek_pointFor(&info, file_read, &f, f.size, (int64_t)MP3_SEEK_LEAD_FRAMES * SPF, &p));
    Mp3Seek_free(&info);
    free(f.data);
}

// The decoder counts frame 0 from another byte: each index would be wrong
TEST(stream_start_that_does_not_agree_gives_none) {
    File f; make_info(&f, "Info", false);
    Mp3SeekInfo info;
    CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame - 417));
    CHECK_EQ_INT(info.kind, MP3_SEEK_NONE);
    free(f.data);
}

TEST(file_without_a_header_frame_gives_none) {
    File f; make_file(&f, 0);
    Mp3SeekInfo info;
    CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    free(f.data);
}

TEST(xing_without_toc_gives_none) {
    File f; make_info(&f, "Xing", false);
    Mp3SeekInfo info;
    CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    free(f.data);
}

// The TOC gives the byte position, then the point moves to the frame header at
// or before it. The index comes from the TOC, thus it is near the real one.
TEST(xing_point_is_a_real_frame_near_the_toc_position) {
    File f; make_info(&f, "Xing", true);
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    CHECK_EQ_INT(info.kind, MP3_SEEK_XING);
    int64_t target = (int64_t)2000 * SPF;
    Mp3SeekPoint p;
    CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, target, &p));
    int n = frame_at(&f, p.byte_pos);
    CHECK(n >= 0);
    CHECK(abs(n - (2000 - MP3_SEEK_LEAD_FRAMES)) <= 20);  // 1/256 of the file is 16 frames
    CHECK(p.pcm_frame <= target);
    Mp3Seek_free(&info);
    free(f.data);
}

// VBRI: frame 0 is the VBRI frame, and the table gives groups of 100 frames
TEST(vbri_point_is_the_start_of_a_group) {
    File f; make_file(&f, 417);
    uint8_t* h = f.data + ID3_BYTES;
    static const uint8_t hdr[4] = {HDR_NO_PAD};
    memcpy(h, hdr, 4);
    uint8_t* v = h + 4 + 32;
    memcpy(v, "VBRI", 4);
    put_be32(v + 14, FRAMES);
    int groups = FRAMES / 100;
    put_be16(v + 18, (uint16_t)groups);  // entries
    put_be16(v + 20, 1);                 // scale
    put_be16(v + 22, 2);                 // bytes of an entry
    put_be16(v + 24, 100);               // frames of an entry
    for (int i = 0; i < groups; i++) {
        put_be16(v + 26 + 2 * i, (uint16_t)(f.frame_pos[(i + 1) * 100] - f.frame_pos[i * 100]));
    }

    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, ID3_BYTES));  // the decoder starts at VBRI
    CHECK_EQ_INT(info.kind, MP3_SEEK_VBRI);
    int64_t target = (int64_t)(1 + 1234) * SPF;  // audio frame 1234 is decoder frame 1235
    Mp3SeekPoint p;
    CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, target, &p));
    CHECK_EQ_INT((int)p.byte_pos, (int)f.frame_pos[1200]);
    CHECK_EQ_INT((int)p.pcm_frame, (1 + 1200) * SPF);
    Mp3Seek_free(&info);
    free(f.data);
}

int main(void) {
    RUN(info_header_gives_cbr);
    RUN(cbr_point_is_the_exact_frame);
    RUN(target_near_the_start_gives_no_point);
    RUN(stream_start_that_does_not_agree_gives_none);
    RUN(file_without_a_header_frame_gives_none);
    RUN(xing_without_toc_gives_none);
    RUN(xing_point_is_a_real_frame_near_the_toc_position);
    RUN(vbri_point_is_the_start_of_a_group);
    return test_summary();
}
