#include "mp3_seek.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The fields of an MPEG audio frame header that the seek needs
typedef struct {
    int version;           // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
    int sample_rate;
    int bitrate_kbps;
    int samples_per_frame;
    int frame_bytes;
    int side_info_bytes;
    bool crc;
} FrameHeader;

static const int bitrates_v1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const int bitrates_v2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
static const int sample_rates[4][3] = {
    {11025, 12000, 8000},   // MPEG-2.5
    {0, 0, 0},              // reserved
    {22050, 24000, 16000},  // MPEG-2
    {44100, 48000, 32000},  // MPEG-1
};

// Parse a Layer III frame header. Returns false for bytes that are not one.
static bool parse_header(const uint8_t* h, FrameHeader* out) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;

    int version = (h[1] >> 3) & 3;
    int layer = (h[1] >> 1) & 3;
    int bitrate_index = h[2] >> 4;
    int rate_index = (h[2] >> 2) & 3;
    if (version == 1 || layer != 1) return false;   // reserved version, or not Layer III
    if (bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) return false;

    bool mono = ((h[3] >> 6) & 3) == 3;
    out->version = version;
    out->sample_rate = sample_rates[version][rate_index];
    out->bitrate_kbps = version == 3 ? bitrates_v1[bitrate_index] : bitrates_v2[bitrate_index];
    out->samples_per_frame = version == 3 ? 1152 : 576;
    out->frame_bytes = (out->samples_per_frame / 8) * out->bitrate_kbps * 1000 / out->sample_rate +
                       ((h[2] >> 1) & 1);
    out->side_info_bytes = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    out->crc = (h[1] & 1) == 0;
    return true;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

// A frame header of the stream at `buf[i]`, followed by two more headers of the
// stream: three in a row are not a chance match inside the audio data
static bool is_stream_frame(const Mp3SeekInfo* info, const uint8_t* buf, int64_t len, int64_t i) {
    for (int n = 0; n < 3; n++) {
        FrameHeader h;
        if (i + 4 > len || !parse_header(&buf[i], &h)) return false;
        if (h.version != info->version || h.sample_rate != info->sample_rate) return false;
        if (info->kind == MP3_SEEK_CBR && h.bitrate_kbps != info->bitrate_kbps) return false;
        i += h.frame_bytes;
    }
    return true;
}

// The byte position of the last frame that starts at or before `estimate`, or -1
#define ALIGN_BACK_BYTES    4096
#define ALIGN_AHEAD_BYTES   (3 * 2881)  // three frames of the largest size
static int64_t align_to_frame(const Mp3SeekInfo* info, Mp3SeekRead read, void* ctx,
                              int64_t file_size, int64_t estimate) {
    if (estimate < info->first_frame) estimate = info->first_frame;
    if (estimate >= file_size) return -1;

    int64_t start = estimate - ALIGN_BACK_BYTES;
    if (start < info->first_frame) start = info->first_frame;
    int64_t want = (estimate - start) + ALIGN_AHEAD_BYTES;

    uint8_t* buf = (uint8_t*)malloc((size_t)want);
    if (!buf) return -1;
    int64_t got = read(ctx, start, buf, want);

    int64_t found = -1;
    for (int64_t i = estimate - start; i >= 0; i--) {
        if (is_stream_frame(info, buf, got, i)) {
            found = start + i;
            break;
        }
    }
    free(buf);
    return found;
}

bool Mp3Seek_init(Mp3SeekInfo* info, Mp3SeekRead read, void* ctx, int64_t file_size,
                  int64_t stream_start) {
    memset(info, 0, sizeof(*info));

    // The header frame is the first frame after the ID3v2 tags
    int64_t pos = 0;
    uint8_t id3[10];
    while (read(ctx, pos, id3, 10) == 10 && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3') {
        int64_t size = ((int64_t)(id3[6] & 0x7F) << 21) | ((id3[7] & 0x7F) << 14) |
                       ((id3[8] & 0x7F) << 7) | (id3[9] & 0x7F);
        pos += 10 + size + ((id3[5] & 0x10) ? 10 : 0);
    }

    uint8_t frame[2881];
    int64_t got = read(ctx, pos, frame, sizeof(frame));
    FrameHeader h;
    if (got < 4 || !parse_header(frame, &h) || h.frame_bytes > got) return false;

    info->header_pos = pos;
    info->version = h.version;
    info->sample_rate = h.sample_rate;
    info->bitrate_kbps = h.bitrate_kbps;
    info->samples_per_frame = h.samples_per_frame;

    const uint8_t* tag = frame + 4 + (h.crc ? 2 : 0) + h.side_info_bytes;
    const uint8_t* vbri = frame + 4 + 32;
    const uint8_t* end = frame + h.frame_bytes;

    if (tag + 8 <= end && (memcmp(tag, "Xing", 4) == 0 || memcmp(tag, "Info", 4) == 0)) {
        uint32_t flags = be32(tag + 4);
        const uint8_t* p = tag + 8;
        if (!(flags & 1) || p + 4 > end) return false;   // no frame count
        info->frame_count = be32(p);
        p += 4;
        info->xing_bytes = file_size - pos;
        if (flags & 2) {
            if (p + 4 > end) return false;
            info->xing_bytes = be32(p);
            p += 4;
        }

        // The decoder skips the header frame: frame 0 is the next one
        info->first_frame = pos + h.frame_bytes;
        if (memcmp(tag, "Info", 4) == 0) {
            info->kind = MP3_SEEK_CBR;
            info->frame_bytes = (double)(h.samples_per_frame / 8) * h.bitrate_kbps * 1000 / h.sample_rate;
        } else {
            if (!(flags & 4) || p + 100 > end) return false;   // no TOC
            memcpy(info->toc, p, 100);
            info->kind = MP3_SEEK_XING;
        }
    } else if (vbri + 26 <= end && memcmp(vbri, "VBRI", 4) == 0) {
        int entries = be16(vbri + 18);
        int scale = be16(vbri + 20);
        int entry_bytes = be16(vbri + 22);
        int frames_per_entry = be16(vbri + 24);
        const uint8_t* table = vbri + 26;
        if (entries <= 0 || entry_bytes < 1 || entry_bytes > 4 || frames_per_entry <= 0 ||
            table + (int64_t)entries * entry_bytes > end) {
            return false;
        }

        info->vbri_offsets = (int64_t*)malloc(sizeof(int64_t) * (size_t)(entries + 1));
        if (!info->vbri_offsets) return false;

        // The decoder counts the VBRI frame as frame 0; the table starts after it
        int64_t offset = pos + h.frame_bytes;
        for (int i = 0; i < entries; i++) {
            info->vbri_offsets[i] = offset;
            uint32_t size = 0;
            for (int b = 0; b < entry_bytes; b++) size = (size << 8) | table[i * entry_bytes + b];
            offset += (int64_t)size * scale;
        }
        info->vbri_offsets[entries] = offset;
        info->vbri_entries = entries;
        info->vbri_frames_per_entry = frames_per_entry;
        info->frame_count = be32(vbri + 14);
        info->first_frame = pos;
        info->kind = MP3_SEEK_VBRI;
    } else {
        return false;
    }

    // The decoder must count frames from the same byte, else each index is wrong
    if (info->first_frame != stream_start || info->frame_count <= 0) {
        Mp3Seek_free(info);
        return false;
    }
    return true;
}

void Mp3Seek_free(Mp3SeekInfo* info) {
    free(info->vbri_offsets);
    memset(info, 0, sizeof(*info));
}

bool Mp3Seek_pointFor(const Mp3SeekInfo* info, Mp3SeekRead read, void* ctx, int64_t file_size,
                      int64_t target_frame, Mp3SeekPoint* point) {
    if (info->kind == MP3_SEEK_NONE || target_frame < 0) return false;

    // The point is MP3_SEEK_LEAD_FRAMES frames before the frame of the target
    int64_t target_mp3_frame = target_frame / info->samples_per_frame;
    int64_t want_frame = target_mp3_frame - MP3_SEEK_LEAD_FRAMES;
    if (want_frame < 1) return false;

    int64_t frame = 0;
    int64_t pos = -1;

    if (info->kind == MP3_SEEK_CBR) {
        int64_t estimate = info->first_frame + (int64_t)(want_frame * info->frame_bytes);
        pos = align_to_frame(info, read, ctx, file_size, estimate);
        if (pos < 0) return false;
        // The header found is the frame asked for, or the one before it
        frame = llround((double)(pos - info->first_frame) / info->frame_bytes);
    } else if (info->kind == MP3_SEEK_XING) {
        // The TOC gives the byte position of each percent of the duration
        double percent = 100.0 * (double)want_frame / (double)info->frame_count;
        if (percent >= 100.0) percent = 99.999;
        int i = (int)percent;
        double a = info->toc[i];
        double b = i < 99 ? info->toc[i + 1] : 256.0;
        double fraction = (a + (b - a) * (percent - i)) / 256.0;
        int64_t estimate = info->header_pos + (int64_t)(fraction * (double)info->xing_bytes);
        pos = align_to_frame(info, read, ctx, file_size, estimate);
        if (pos < 0) return false;
        frame = want_frame;
    } else if (info->kind == MP3_SEEK_VBRI) {
        // Frame 0 is the VBRI frame; group i starts at frame 1 + i * frames_per_entry
        int64_t i = (want_frame - 1) / info->vbri_frames_per_entry;
        if (i >= info->vbri_entries) i = info->vbri_entries - 1;
        pos = align_to_frame(info, read, ctx, file_size, info->vbri_offsets[i]);
        if (pos != info->vbri_offsets[i]) return false;   // the table does not agree with the file
        frame = 1 + i * info->vbri_frames_per_entry;
    }

    if (frame < 1) return false;

    point->byte_pos = pos;
    point->frames_to_discard = 0;
    point->pcm_frame = frame * info->samples_per_frame;
    // The point must not pass the target, else the decoder counts back past it
    if (point->pcm_frame > target_frame) return false;
    return true;
}
