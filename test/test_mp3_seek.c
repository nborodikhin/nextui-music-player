// Tests for src/mp3_seek.c - the seek point from the table in an MP3 file.
//
// The files are synthetic: an ID3v2 tag, a header frame (Info, Xing or VBRI),
// or no header frame, then frames with real headers and empty data. The tests pin that a point is on
// a real frame header, that its index is the index of that frame, that it is not
// past the target, and that invalid files or tables that do not agree with the
// decoder give no point.

#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "mp3_seek.h"
#define DR_MP3_IMPLEMENTATION
#include "audio/dr_mp3.h"

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
    int read_calls;
} File;

static int64_t file_read(void* ctx, int64_t offset, uint8_t* buf, int64_t len) {
    File* f = (File*)ctx;
    f->read_calls++;
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

static void put_bits(uint8_t* p, int bit, int count, unsigned value) {
    for (int i = 0; i < count; i++) {
        unsigned mask = 1u << (7 - (bit + i) % 8);
        p[(bit + i) / 8] &= (uint8_t)~mask;
        if (value & (1u << (count - 1 - i))) p[(bit + i) / 8] |= (uint8_t)mask;
    }
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

TEST(file_without_a_header_frame_builds_cbr_index) {
    File f; make_file(&f, 0);
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    CHECK_EQ_INT(info.kind, MP3_SEEK_CBR_INDEX);
    CHECK_EQ_INT((int)info.frame_count, FRAMES);
    // Buffered reads, rather than one callback per frame.
    CHECK(f.read_calls < 100);
    for (int n = 0; n < FRAMES; n += MP3_SEEK_INDEX_STRIDE) {
        CHECK(info.cbr_offsets[n / MP3_SEEK_INDEX_STRIDE] == f.frame_pos[n]);
    }
    Mp3Seek_free(&info);
    CHECK(info.cbr_offsets == NULL);
    free(f.data);
}

// A constant bitrate does not require the encoder to use the usual padding
// pattern. Exact offsets must work even when arithmetic would drift.
TEST(indexed_cbr_points_are_exact_with_unusual_padding) {
    File f; make_file(&f, 0);
    int64_t pos = f.first_frame;
    for (int n = 0; n < FRAMES; n++) {
        static const uint8_t hdr[] = {HDR_NO_PAD};
        f.frame_pos[n] = pos;
        memset(f.data + pos, 0, 417);
        memcpy(f.data + pos, hdr, 4);
        pos += 417;
    }
    f.frame_pos[FRAMES] = f.size = pos;
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    for (int n = MP3_SEEK_LEAD_FRAMES + 3; n < FRAMES; n++) {
        Mp3SeekPoint p;
        CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, (int64_t)n * SPF + 500, &p));
        // Two extra frames are reserved for the bit reservoir; these synthetic
        // frames use no reservoir, so the very first one can be decoded.
        int start = n - MP3_SEEK_LEAD_FRAMES - 2;
        CHECK(p.byte_pos == f.frame_pos[start]);
        CHECK(p.pcm_frame == (int64_t)start * SPF);
        CHECK_EQ_INT(p.frames_to_discard, 1);
    }
    Mp3SeekPoint p;
    CHECK(!Mp3Seek_pointFor(&info, file_read, &f, f.size, 3 * SPF, &p));
    CHECK(!Mp3Seek_pointFor(&info, file_read, &f, f.size, (int64_t)FRAMES * SPF, &p));
    Mp3Seek_free(&info);
    free(f.data);
}

TEST(indexed_cbr_accounts_for_frames_skipped_by_reservoir) {
    File f; make_file(&f, 0);
    for (int n = 1; n < FRAMES; n++) {
        // MPEG-1 main_data_begin = 511: two frames must fill the reservoir
        // before the decoder produces its first PCM frame after a jump.
        uint8_t* side = f.data + f.frame_pos[n] + 4;
        int needed = n == 1 ? 396 : 511;
        put_bits(side, 0, 9, (unsigned)needed);
    }
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    int targets[] = {6, 258, 1000, FRAMES - 1};
    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        int n = targets[i];
        Mp3SeekPoint p;
        CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, (int64_t)n * SPF, &p));
        CHECK(p.byte_pos == f.frame_pos[n - MP3_SEEK_LEAD_FRAMES - 2]);
        CHECK(p.pcm_frame == (int64_t)(n - MP3_SEEK_LEAD_FRAMES) * SPF);
        CHECK_EQ_INT(p.frames_to_discard, 1);
    }
    Mp3Seek_free(&info);
    free(f.data);
}

TEST(indexed_low_bitrate_mpeg2_and_mpeg25_warm_up) {
    for (int version = 0; version <= 2; version += 2) {
        File f; make_file(&f, 0);
        int frame_bytes = version == 2 ? 26 : 52; // 8 kbps, mono, 22050 / 11025 Hz
        int reservoir_frames = version == 2 ? 85 : 9; // conservative stereo + CRC bound
        int skipped = (255 + (frame_bytes - 13) - 1) / (frame_bytes - 13);
        memset(f.data + ID3_BYTES, 0, (size_t)(f.size - ID3_BYTES));
        for (int n = 0; n < FRAMES; n++) {
            int64_t pos = ID3_BYTES + (int64_t)n * frame_bytes;
            f.frame_pos[n] = pos;
            uint8_t hdr[] = {0xFF, version == 2 ? 0xF3 : 0xE3, 0x10, 0xC4};
            memcpy(f.data + pos, hdr, 4);
            int needed = n * (frame_bytes - 13);
            if (needed > 255) needed = 255;
            f.data[pos + 4] = (uint8_t)needed;
        }
        f.size = ID3_BYTES + (int64_t)FRAMES * frame_bytes;
        Mp3SeekInfo info;
        CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
        CHECK_EQ_INT(info.samples_per_frame, 576);
        Mp3SeekPoint p;
        int start = 1000 - MP3_SEEK_LEAD_FRAMES - reservoir_frames;
        CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, 1000 * 576, &p));
        CHECK(p.byte_pos == f.frame_pos[start]);
        CHECK(p.pcm_frame == (int64_t)(start + skipped) * 576);
        CHECK_EQ_INT(p.frames_to_discard, 1);
        Mp3Seek_free(&info);
        free(f.data);
    }
}

TEST(headerless_missing_initial_reservoir_gives_none) {
    File f; make_file(&f, 0);
    f.data[f.first_frame + 4] = 1;
    Mp3SeekInfo info;
    CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    CHECK(info.cbr_offsets == NULL);
    free(f.data);
}

TEST(duration_count_can_hide_a_reservoir_underflow) {
    File f; make_file(&f, 0);
    // Mono MPEG-1: the first granule starts at bit 18, the second at bit 77.
    // Frame 0 consumes all its main data. Frame 1 then needs one missing byte.
    uint8_t* side = f.data + f.frame_pos[0] + 4;
    int payload = (int)(f.frame_pos[1] - f.frame_pos[0]) - 4 - SIDE_INFO;
    put_bits(side, 77, 12, (unsigned)payload * 8);
    put_bits(f.data + f.frame_pos[1] + 4, 0, 9, 1);

    drmp3 decoder;
    CHECK(drmp3_init_memory(&decoder, f.data, (size_t)f.size, NULL));
    CHECK(drmp3_get_pcm_frame_count(&decoder) == (drmp3_uint64)FRAMES * SPF);
    int16_t pcm[SPF];
    drmp3_uint64 decoded = 0, got;
    while ((got = drmp3_read_pcm_frames_s16(&decoder, SPF, pcm)) != 0) decoded += got;
    CHECK(decoded == (drmp3_uint64)(FRAMES - 1) * SPF);
    drmp3_uninit(&decoder);

    Mp3SeekInfo info;
    bool indexed = Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame);
    Mp3Seek_free(&info);
    free(f.data);
    CHECK(!indexed);
}

TEST(reservoir_consumption_counts_all_granules_and_rounds_up) {
    for (int channels = 1; channels <= 2; channels++) {
        for (int crc = 0; crc <= 1; crc++) {
            File f; make_file(&f, 0);
            for (int n = 0; n < FRAMES; n++) {
                uint8_t* hdr = f.data + f.frame_pos[n];
                hdr[3] = channels == 1 ? 0xC4 : 0x04;
                if (crc) hdr[1] &= (uint8_t)~1;
            }
            int side_size = channels == 1 ? 17 : 32;
            uint8_t* side = f.data + f.frame_pos[900] + 4 + 2 * crc;
            int payload = (int)(f.frame_pos[901] - f.frame_pos[900]) - 4 - 2 * crc - side_size;
            int granules = channels * 2;
            int bits = payload * 8 - 1; // Only one bit remains, so zero complete bytes remain.
            for (int gr = 0; gr < granules; gr++) {
                int count = bits / granules + (gr < bits % granules);
                put_bits(side, (channels == 1 ? 18 : 20) + 59 * gr, 12, (unsigned)count);
            }
            Mp3SeekInfo info;
            CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
            Mp3Seek_free(&info);
            put_bits(f.data + f.frame_pos[901] + 4 + 2 * crc, 0, 9, 1);
            CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
            CHECK(info.cbr_offsets == NULL);
            free(f.data);
        }
    }
}

TEST(headerless_long_file_grows_index) {
    File f; make_file(&f, 0);
    int frames = MP3_SEEK_INDEX_STRIDE * 300 + 1;
    f.size = ID3_BYTES + (int64_t)frames * 417;
    f.data = realloc(f.data, (size_t)f.size);
    CHECK(f.data != NULL);
    memset(f.data + ID3_BYTES, 0, (size_t)(f.size - ID3_BYTES));
    for (int n = 0; n < frames; n++) {
        static const uint8_t hdr[] = {HDR_NO_PAD};
        memcpy(f.data + ID3_BYTES + (int64_t)n * 417, hdr, 4);
    }
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    CHECK_EQ_INT((int)info.frame_count, frames);
    CHECK(info.cbr_offsets[300] == ID3_BYTES + (int64_t)300 * MP3_SEEK_INDEX_STRIDE * 417);
    Mp3SeekPoint p;
    CHECK(Mp3Seek_pointFor(&info, file_read, &f, f.size, (int64_t)(frames - 1) * SPF, &p));
    CHECK(p.pcm_frame == (int64_t)(frames - 6) * SPF);
    Mp3Seek_free(&info);
    free(f.data);
}

TEST(headerless_cbr_without_id3_builds_index) {
    File f; make_file(&f, 0);
    memmove(f.data, f.data + ID3_BYTES, (size_t)(f.size - ID3_BYTES));
    f.size -= ID3_BYTES;
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, 0));
    CHECK(info.first_frame == 0);
    CHECK(info.cbr_offsets[0] == 0);
    Mp3Seek_free(&info);
    free(f.data);
}

TEST(headerless_cbr_with_id3v1_builds_index) {
    File f; make_file(&f, 0);
    memset(f.data + f.size, 0, 128);
    memcpy(f.data + f.size, "TAG", 3);
    f.size += 128;
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    CHECK_EQ_INT((int)info.frame_count, FRAMES);
    Mp3Seek_free(&info);
    free(f.data);
}

TEST(headerless_stream_start_mismatch_gives_none) {
    File f; make_file(&f, 0);
    Mp3SeekInfo info;
    CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame + 417));
    CHECK_EQ_INT(info.kind, MP3_SEEK_NONE);
    CHECK(info.cbr_offsets == NULL);
    free(f.data);
}

TEST(headerless_late_bitrate_or_rate_change_gives_none) {
    for (int changed = 0; changed < 2; changed++) {
        File f; make_file(&f, 0);
        // Check the entire stream, not just a sample near its start.
        uint8_t* hdr = f.data + f.frame_pos[FRAMES - 10];
        hdr[2] = changed == 0 ? 0xA0 : 0x94; // 160 kbps, or 48 kHz
        Mp3SeekInfo info;
        CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
        CHECK_EQ_INT(info.kind, MP3_SEEK_NONE);
        CHECK(info.cbr_offsets == NULL);
        free(f.data);
    }
}

TEST(headerless_truncated_or_broken_frame_gives_none) {
    for (int broken = 0; broken < 2; broken++) {
        File f; make_file(&f, 0);
        if (broken) f.data[f.frame_pos[1000]] = 0;
        else f.size--;
        Mp3SeekInfo info;
        CHECK(!Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
        CHECK_EQ_INT(info.kind, MP3_SEEK_NONE);
        CHECK(info.cbr_offsets == NULL);
        free(f.data);
    }
}

TEST(indexed_cbr_changed_file_gives_no_point) {
    File f; make_file(&f, 0);
    Mp3SeekInfo info;
    CHECK(Mp3Seek_init(&info, file_read, &f, f.size, f.first_frame));
    f.data[f.frame_pos[1000]] = 0;
    Mp3SeekPoint p;
    CHECK(!Mp3Seek_pointFor(&info, file_read, &f, f.size, 1010 * SPF, &p));
    Mp3Seek_free(&info);
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
    RUN(file_without_a_header_frame_builds_cbr_index);
    RUN(indexed_cbr_points_are_exact_with_unusual_padding);
    RUN(indexed_cbr_accounts_for_frames_skipped_by_reservoir);
    RUN(indexed_low_bitrate_mpeg2_and_mpeg25_warm_up);
    RUN(headerless_missing_initial_reservoir_gives_none);
    RUN(duration_count_can_hide_a_reservoir_underflow);
    RUN(reservoir_consumption_counts_all_granules_and_rounds_up);
    RUN(headerless_long_file_grows_index);
    RUN(headerless_cbr_without_id3_builds_index);
    RUN(headerless_cbr_with_id3v1_builds_index);
    RUN(headerless_stream_start_mismatch_gives_none);
    RUN(headerless_late_bitrate_or_rate_change_gives_none);
    RUN(headerless_truncated_or_broken_frame_gives_none);
    RUN(indexed_cbr_changed_file_gives_no_point);
    RUN(xing_without_toc_gives_none);
    RUN(xing_point_is_a_real_frame_near_the_toc_position);
    RUN(vbri_point_is_the_start_of_a_group);
    return test_summary();
}
