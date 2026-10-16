#ifndef __MP3_SEEK_H__
#define __MP3_SEEK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Seek in an MP3 file by an embedded table or a verified constant-bitrate index,
// in place of a decode from the start.
//
// The first frame of many MP3 files is a header frame:
// - "Info" (a constant bitrate): the byte position of a time is arithmetic
// - "Xing" with a TOC (a variable bitrate): 100 byte positions, one for each
//   percent of the duration
// - "VBRI" (a variable bitrate, Fraunhofer): a table of the sizes of equal
//   groups of frames
// Without a header frame, init scans the frame headers synchronously. If every
// frame has the same bitrate and sample rate and valid reservoir consumption,
// it keeps one offset per 256 frames.
//
// For each seek, Mp3Seek_pointFor() gives one point to seek from: the byte
// position of a real frame header near the target, and the index of the first
// PCM frame the decoder will produce there after reservoir warm-up.
// Indexed CBR walks at most 255 frame headers from an exact offset, then checks
// the reservoir warm-up frames; embedded tables read a few KB to find the header.
// The file holds no platform include: the reads come through a callback.

// Decoded frames between the point and the frame of the target. Indexed CBR
// reserves additional frames to fill the bit reservoir before these. The first
// frames after a jump have no bit reservoir and no MDCT overlap, thus their output
// differs from a decode that did not jump. The decoder decodes the lead frames
// in full and discards their samples, because they come before the target.
//
// A point must not ask dr_mp3 to discard more than one frame: dr_mp3 discards
// all but the last with no output buffer, and minimp3 then reads only their
// headers, thus their data does not fill the reservoir.
#define MP3_SEEK_LEAD_FRAMES 3
#define MP3_SEEK_INDEX_STRIDE 256

typedef enum {
    MP3_SEEK_NONE = 0,  // no usable table or index: decode from the start
    MP3_SEEK_CBR,       // "Info" header: the positions are arithmetic
    MP3_SEEK_XING,      // "Xing" header with a TOC
    MP3_SEEK_VBRI,      // "VBRI" header
    MP3_SEEK_CBR_INDEX, // no header: exact offsets from a frame-header scan
} Mp3SeekKind;

// Reads up to `len` bytes at `offset`. Returns the count of bytes read.
typedef int64_t (*Mp3SeekRead)(void* ctx, int64_t offset, uint8_t* buf, int64_t len);

typedef struct {
    Mp3SeekKind kind;
    int64_t header_pos;       // the byte position of the header frame
    int64_t first_frame;      // the byte position of frame 0, as the decoder counts frames
    int64_t frame_count;      // the frames of audio, from the header or scan
    int samples_per_frame;
    // The fields of the first frame, that each real frame header matches
    int version;
    int sample_rate;
    int bitrate_kbps;
    double frame_bytes;       // CBR: the mean size of a frame
    // Xing
    int64_t xing_bytes;       // the bytes from header_pos that the TOC divides
    uint8_t toc[100];
    // VBRI
    int vbri_entries;
    int vbri_frames_per_entry;
    int64_t* vbri_offsets;    // byte position of each group, vbri_entries + 1 values
    // Headerless CBR: frame 0, 256, 512, ...; ceil(frame_count / 256) entries
    int64_t* cbr_offsets;
} Mp3SeekInfo;

typedef struct {
    int64_t byte_pos;          // the first byte of a frame
    int64_t pcm_frame;         // first PCM frame the decoder produces from byte_pos
    int frames_to_discard;     // 1: prime the decoder through its first usable frame
} Mp3SeekPoint;

// Read the header frame, or build an index for headerless CBR. `stream_start` is
// the byte position the decoder counts as frame 0 (dr_mp3: streamStartOffset).
// A header that does not agree gives MP3_SEEK_NONE. Returns true for a kind other
// than NONE.
bool Mp3Seek_init(Mp3SeekInfo* info, Mp3SeekRead read, void* ctx, int64_t file_size,
                  int64_t stream_start);

void Mp3Seek_free(Mp3SeekInfo* info);

// A point to seek from for `target_frame`, a PCM frame as the decoder counts
// them. The point is at or before the target. Returns false where the target is
// too near the start for a point, or where no frame header is near: the caller
// then decodes from the start.
//
// CBR, indexed CBR and VBRI give the exact frame. Xing gives the frame by the TOC,
// thus the position can be wrong by the precision of the TOC.
bool Mp3Seek_pointFor(const Mp3SeekInfo* info, Mp3SeekRead read, void* ctx, int64_t file_size,
                      int64_t target_frame, Mp3SeekPoint* point);

#ifdef __cplusplus
}
#endif

#endif
