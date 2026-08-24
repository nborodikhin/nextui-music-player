#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "m4b_chapters.h"

#define FOURCC(a, b, c, d) \
    (((uint32_t)(unsigned char)(a) << 24) | ((uint32_t)(unsigned char)(b) << 16) | \
     ((uint32_t)(unsigned char)(c) << 8)  |  (uint32_t)(unsigned char)(d))

#define BOX_MOOV FOURCC('m','o','o','v')
#define BOX_UDTA FOURCC('u','d','t','a')
#define BOX_CHPL FOURCC('c','h','p','l')
#define BOX_TRAK FOURCC('t','r','a','k')
#define BOX_TREF FOURCC('t','r','e','f')
#define BOX_CHAP FOURCC('c','h','a','p')
#define BOX_TKHD FOURCC('t','k','h','d')
#define BOX_MDIA FOURCC('m','d','i','a')
#define BOX_MDHD FOURCC('m','d','h','d')
#define BOX_MINF FOURCC('m','i','n','f')
#define BOX_STBL FOURCC('s','t','b','l')
#define BOX_STTS FOURCC('s','t','t','s')
#define BOX_STSZ FOURCC('s','t','s','z')
#define BOX_STSC FOURCC('s','t','s','c')
#define BOX_STCO FOURCC('s','t','c','o')
#define BOX_CO64 FOURCC('c','o','6','4')
#define BOX_META FOURCC('m','e','t','a')
#define BOX_ILST FOURCC('i','l','s','t')
#define BOX_DATA FOURCC('d','a','t','a')
#define BOX_NAM  FOURCC(0xA9,'n','a','m')
#define BOX_ART  FOURCC(0xA9,'A','R','T')
#define BOX_ALB  FOURCC(0xA9,'a','l','b')

// A chapter list longer than this is certainly a corrupt or hostile file
#define MAX_CHAPTERS 4096

// ---------------------------------------------------------------------------
// Big-endian primitive reads
// ---------------------------------------------------------------------------

static bool read_at(FILE* f, int64_t offset, void* buf, size_t size) {
    if (fseek(f, (long)offset, SEEK_SET) != 0) return false;
    return fread(buf, 1, size, f) == size;
}

static bool rd8(FILE* f, uint8_t* out) {
    return fread(out, 1, 1, f) == 1;
}

static bool rd32(FILE* f, uint32_t* out) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return true;
}

static bool rd64(FILE* f, uint64_t* out) {
    uint32_t hi, lo;
    if (!rd32(f, &hi) || !rd32(f, &lo)) return false;
    *out = ((uint64_t)hi << 32) | lo;
    return true;
}

// ---------------------------------------------------------------------------
// Box walking
// ---------------------------------------------------------------------------

// Read the box header at `pos`. Fills the payload range [start, end) of the box
// and `next`, the offset of the following sibling.
static bool read_box_header(FILE* f, int64_t pos, int64_t limit, uint32_t* type,
                            int64_t* payload_start, int64_t* payload_end, int64_t* next) {
    if (pos + 8 > limit) return false;
    if (fseek(f, (long)pos, SEEK_SET) != 0) return false;

    uint32_t size32;
    if (!rd32(f, &size32) || !rd32(f, type)) return false;

    int64_t header = 8;
    int64_t size = size32;
    if (size32 == 1) {
        uint64_t size64;
        if (!rd64(f, &size64)) return false;
        header = 16;
        size = (int64_t)size64;
    } else if (size32 == 0) {
        size = limit - pos;  // Extends to the end of the enclosing box
    }

    if (size < header || pos + size > limit) return false;

    *payload_start = pos + header;
    *payload_end = pos + size;
    *next = pos + size;
    return true;
}

// Find the first child box of `type` within [start, end)
static bool find_box(FILE* f, int64_t start, int64_t end, uint32_t type,
                     int64_t* payload_start, int64_t* payload_end) {
    int64_t pos = start;
    while (pos < end) {
        uint32_t t;
        int64_t ps, pe, next;
        if (!read_box_header(f, pos, end, &t, &ps, &pe, &next)) return false;
        if (t == type) {
            *payload_start = ps;
            *payload_end = pe;
            return true;
        }
        pos = next;
    }
    return false;
}

// Follow a chain of nested box types, e.g. moov -> udta -> chpl
static bool find_box_path(FILE* f, int64_t start, int64_t end,
                          const uint32_t* types, int type_count,
                          int64_t* payload_start, int64_t* payload_end) {
    int64_t s = start, e = end;
    for (int i = 0; i < type_count; i++) {
        if (!find_box(f, s, e, types[i], &s, &e)) return false;
    }
    *payload_start = s;
    *payload_end = e;
    return true;
}

static bool open_moov(const char* path, FILE** out_file,
                      int64_t* moov_start, int64_t* moov_end, int64_t* file_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    int64_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }

    if (!find_box(f, 0, size, BOX_MOOV, moov_start, moov_end)) {
        fclose(f);
        return false;
    }

    *out_file = f;
    *file_size = size;
    return true;
}

// ---------------------------------------------------------------------------
// Nero `chpl` chapters
// ---------------------------------------------------------------------------

// Layout: version(1) flags(3) [reserved(4) if version != 0] count(1),
// then per entry: start time as a 64-bit count of 100 ns units, followed by a
// Pascal string (1-byte length + UTF-8 bytes).
static int parse_chpl(FILE* f, int64_t start, int64_t end, AudiobookChapter** out) {
    if (fseek(f, (long)start, SEEK_SET) != 0) return 0;

    uint8_t version;
    uint32_t ignored;
    if (!rd8(f, &version)) return 0;
    if (fseek(f, 3, SEEK_CUR) != 0) return 0;  // flags
    if (version != 0 && !rd32(f, &ignored)) return 0;

    uint8_t count;
    if (!rd8(f, &count) || count == 0) return 0;

    AudiobookChapter* chapters = calloc(count, sizeof(AudiobookChapter));
    if (!chapters) return 0;

    int parsed = 0;
    for (int i = 0; i < count; i++) {
        if (ftell(f) >= end) break;

        uint64_t start_100ns;
        uint8_t title_len;
        if (!rd64(f, &start_100ns) || !rd8(f, &title_len)) break;

        char title[AUDIOBOOK_MAX_TITLE];
        int to_read = title_len < (int)sizeof(title) - 1 ? title_len : (int)sizeof(title) - 1;
        if (to_read > 0 && fread(title, 1, to_read, f) != (size_t)to_read) break;
        title[to_read] = '\0';
        if (title_len > to_read) fseek(f, title_len - to_read, SEEK_CUR);

        chapters[parsed].start_ms = (int)(start_100ns / 10000ULL);
        if (title[0]) {
            snprintf(chapters[parsed].title, sizeof(chapters[parsed].title), "%s", title);
        } else {
            snprintf(chapters[parsed].title, sizeof(chapters[parsed].title), "Chapter %d", parsed + 1);
        }
        parsed++;
    }

    if (parsed == 0) {
        free(chapters);
        return 0;
    }

    *out = chapters;
    return parsed;
}

// ---------------------------------------------------------------------------
// QuickTime chapter track (`tref`/`chap`)
// ---------------------------------------------------------------------------

// Read the track_id out of a tkhd box
static bool read_track_id(FILE* f, int64_t trak_start, int64_t trak_end, uint32_t* track_id) {
    int64_t s, e;
    if (!find_box(f, trak_start, trak_end, BOX_TKHD, &s, &e)) return false;
    if (fseek(f, (long)s, SEEK_SET) != 0) return false;

    uint8_t version;
    if (!rd8(f, &version)) return false;
    if (fseek(f, 3, SEEK_CUR) != 0) return false;                    // flags
    if (fseek(f, version == 1 ? 16 : 8, SEEK_CUR) != 0) return false;  // creation + modification
    return rd32(f, track_id);
}

// Read the media timescale out of mdia/mdhd
static bool read_timescale(FILE* f, int64_t trak_start, int64_t trak_end, uint32_t* timescale) {
    const uint32_t path[] = {BOX_MDIA, BOX_MDHD};
    int64_t s, e;
    if (!find_box_path(f, trak_start, trak_end, path, 2, &s, &e)) return false;
    if (fseek(f, (long)s, SEEK_SET) != 0) return false;

    uint8_t version;
    if (!rd8(f, &version)) return false;
    if (fseek(f, 3, SEEK_CUR) != 0) return false;
    if (fseek(f, version == 1 ? 16 : 8, SEEK_CUR) != 0) return false;
    return rd32(f, timescale);
}

// Decode a chapter track's sample table into chapter titles and start times.
// Text samples are `uint16 length` followed by the UTF-8 title.
static int parse_chapter_track(FILE* f, int64_t trak_start, int64_t trak_end,
                               AudiobookChapter** out) {
    uint32_t timescale = 0;
    if (!read_timescale(f, trak_start, trak_end, &timescale) || timescale == 0) return 0;

    const uint32_t stbl_path[] = {BOX_MDIA, BOX_MINF, BOX_STBL};
    int64_t stbl_start, stbl_end;
    if (!find_box_path(f, trak_start, trak_end, stbl_path, 3, &stbl_start, &stbl_end)) return 0;

    int64_t s, e;
    uint32_t entry_count;

    // --- stts: sample durations, accumulated into start times ---
    if (!find_box(f, stbl_start, stbl_end, BOX_STTS, &s, &e)) return 0;
    if (fseek(f, (long)s + 4, SEEK_SET) != 0) return 0;
    if (!rd32(f, &entry_count)) return 0;

    int sample_count = 0;
    int64_t* start_times = NULL;   // in timescale units
    int64_t* durations = NULL;
    int capacity = 0;
    int64_t clock = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t run, delta;
        if (!rd32(f, &run) || !rd32(f, &delta)) break;
        if (run > MAX_CHAPTERS) run = MAX_CHAPTERS;
        for (uint32_t k = 0; k < run && sample_count < MAX_CHAPTERS; k++) {
            if (sample_count == capacity) {
                int new_cap = capacity ? capacity * 2 : 32;
                int64_t* ns = realloc(start_times, new_cap * sizeof(int64_t));
                int64_t* nd = realloc(durations, new_cap * sizeof(int64_t));
                if (!ns || !nd) {
                    free(ns ? ns : start_times);
                    free(nd ? nd : durations);
                    return 0;
                }
                start_times = ns;
                durations = nd;
                capacity = new_cap;
            }
            start_times[sample_count] = clock;
            durations[sample_count] = delta;
            clock += delta;
            sample_count++;
        }
    }
    if (sample_count == 0) { free(start_times); free(durations); return 0; }

    // --- stsz: sample sizes ---
    uint32_t uniform_size = 0, stsz_count = 0;
    uint32_t* sizes = NULL;
    if (!find_box(f, stbl_start, stbl_end, BOX_STSZ, &s, &e)) goto fail;
    if (fseek(f, (long)s + 4, SEEK_SET) != 0) goto fail;
    if (!rd32(f, &uniform_size) || !rd32(f, &stsz_count)) goto fail;
    if (uniform_size == 0) {
        if (stsz_count < (uint32_t)sample_count) goto fail;
        sizes = malloc((size_t)sample_count * sizeof(uint32_t));
        if (!sizes) goto fail;
        for (int i = 0; i < sample_count; i++) {
            if (!rd32(f, &sizes[i])) goto fail;
        }
    }

    // --- stsc + stco/co64: map samples to file offsets ---
    typedef struct { uint32_t first_chunk, samples_per_chunk; } StscEntry;
    StscEntry* stsc = NULL;
    uint32_t stsc_count = 0;
    if (!find_box(f, stbl_start, stbl_end, BOX_STSC, &s, &e)) goto fail;
    if (fseek(f, (long)s + 4, SEEK_SET) != 0) goto fail;
    if (!rd32(f, &stsc_count) || stsc_count == 0) goto fail;
    stsc = malloc(stsc_count * sizeof(StscEntry));
    if (!stsc) goto fail;
    for (uint32_t i = 0; i < stsc_count; i++) {
        uint32_t ignored;
        if (!rd32(f, &stsc[i].first_chunk) || !rd32(f, &stsc[i].samples_per_chunk) ||
            !rd32(f, &ignored)) {
            free(stsc);
            goto fail;
        }
    }

    bool is_co64 = false;
    if (!find_box(f, stbl_start, stbl_end, BOX_STCO, &s, &e)) {
        if (!find_box(f, stbl_start, stbl_end, BOX_CO64, &s, &e)) { free(stsc); goto fail; }
        is_co64 = true;
    }
    if (fseek(f, (long)s + 4, SEEK_SET) != 0) { free(stsc); goto fail; }
    uint32_t chunk_count = 0;
    if (!rd32(f, &chunk_count) || chunk_count == 0) { free(stsc); goto fail; }
    int64_t* chunk_offsets = malloc(chunk_count * sizeof(int64_t));
    if (!chunk_offsets) { free(stsc); goto fail; }
    for (uint32_t i = 0; i < chunk_count; i++) {
        if (is_co64) {
            uint64_t v;
            if (!rd64(f, &v)) { free(stsc); free(chunk_offsets); goto fail; }
            chunk_offsets[i] = (int64_t)v;
        } else {
            uint32_t v;
            if (!rd32(f, &v)) { free(stsc); free(chunk_offsets); goto fail; }
            chunk_offsets[i] = v;
        }
    }

    // Walk chunks in order, laying samples out consecutively inside each chunk
    int64_t* sample_offsets = malloc((size_t)sample_count * sizeof(int64_t));
    if (!sample_offsets) { free(stsc); free(chunk_offsets); goto fail; }

    int sample_idx = 0;
    for (uint32_t chunk = 0; chunk < chunk_count && sample_idx < sample_count; chunk++) {
        // Samples per chunk comes from the last stsc entry whose first_chunk <= chunk+1
        uint32_t per_chunk = stsc[0].samples_per_chunk;
        for (uint32_t i = 0; i < stsc_count; i++) {
            if (stsc[i].first_chunk <= chunk + 1) per_chunk = stsc[i].samples_per_chunk;
            else break;
        }
        int64_t offset = chunk_offsets[chunk];
        for (uint32_t k = 0; k < per_chunk && sample_idx < sample_count; k++) {
            sample_offsets[sample_idx] = offset;
            offset += sizes ? sizes[sample_idx] : uniform_size;
            sample_idx++;
        }
    }
    free(stsc);
    free(chunk_offsets);
    if (sample_idx < sample_count) sample_count = sample_idx;
    if (sample_count == 0) { free(sample_offsets); goto fail; }

    // --- Read the title text of each sample ---
    AudiobookChapter* chapters = calloc(sample_count, sizeof(AudiobookChapter));
    if (!chapters) { free(sample_offsets); goto fail; }

    for (int i = 0; i < sample_count; i++) {
        chapters[i].start_ms = (int)((start_times[i] * 1000) / timescale);
        chapters[i].duration_ms = (int)((durations[i] * 1000) / timescale);

        uint32_t sample_size = sizes ? sizes[i] : uniform_size;
        char title[AUDIOBOOK_MAX_TITLE] = "";
        if (sample_size >= 2) {
            unsigned char len_bytes[2];
            if (read_at(f, sample_offsets[i], len_bytes, 2)) {
                int text_len = ((int)len_bytes[0] << 8) | len_bytes[1];
                if (text_len > (int)sample_size - 2) text_len = (int)sample_size - 2;
                if (text_len > (int)sizeof(title) - 1) text_len = (int)sizeof(title) - 1;
                if (text_len > 0 && fread(title, 1, text_len, f) == (size_t)text_len) {
                    title[text_len] = '\0';
                } else {
                    title[0] = '\0';
                }
            }
        }
        if (title[0]) {
            snprintf(chapters[i].title, sizeof(chapters[i].title), "%s", title);
        } else {
            snprintf(chapters[i].title, sizeof(chapters[i].title), "Chapter %d", i + 1);
        }
    }

    free(sample_offsets);
    free(sizes);
    free(start_times);
    free(durations);
    *out = chapters;
    return sample_count;

fail:
    free(sizes);
    free(start_times);
    free(durations);
    return 0;
}

// Locate the trak referenced by another trak's tref/chap and decode it
static int parse_quicktime_chapters(FILE* f, int64_t moov_start, int64_t moov_end,
                                    AudiobookChapter** out) {
    // Find a chapter track id referenced from any track
    uint32_t chapter_track_id = 0;
    int64_t pos = moov_start;
    while (pos < moov_end && chapter_track_id == 0) {
        uint32_t type;
        int64_t ps, pe, next;
        if (!read_box_header(f, pos, moov_end, &type, &ps, &pe, &next)) break;
        if (type == BOX_TRAK) {
            const uint32_t chap_path[] = {BOX_TREF, BOX_CHAP};
            int64_t cs, ce;
            if (find_box_path(f, ps, pe, chap_path, 2, &cs, &ce) && ce - cs >= 4) {
                if (fseek(f, (long)cs, SEEK_SET) == 0) {
                    rd32(f, &chapter_track_id);
                }
            }
        }
        pos = next;
    }
    if (chapter_track_id == 0) return 0;

    // Then find the trak that carries it
    pos = moov_start;
    while (pos < moov_end) {
        uint32_t type;
        int64_t ps, pe, next;
        if (!read_box_header(f, pos, moov_end, &type, &ps, &pe, &next)) break;
        if (type == BOX_TRAK) {
            uint32_t id = 0;
            if (read_track_id(f, ps, pe, &id) && id == chapter_track_id) {
                return parse_chapter_track(f, ps, pe, out);
            }
        }
        pos = next;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int M4B_parseChapters(const char* path, int total_duration_ms, AudiobookChapter** out) {
    if (!path || !out) return 0;
    *out = NULL;

    FILE* f = NULL;
    int64_t moov_start, moov_end, file_size;
    if (!open_moov(path, &f, &moov_start, &moov_end, &file_size)) return 0;

    AudiobookChapter* chapters = NULL;
    int count = 0;

    // Nero `chpl` is by far the most common shape in .m4b files
    const uint32_t chpl_path[] = {BOX_UDTA, BOX_CHPL};
    int64_t cs, ce;
    if (find_box_path(f, moov_start, moov_end, chpl_path, 2, &cs, &ce)) {
        count = parse_chpl(f, cs, ce, &chapters);
    }
    if (count == 0) {
        count = parse_quicktime_chapters(f, moov_start, moov_end, &chapters);
    }

    fclose(f);
    if (count == 0) return 0;

    // Fill in durations from the gap to the next chapter; `chpl` carries none.
    for (int i = 0; i < count; i++) {
        if (i + 1 < count) {
            chapters[i].duration_ms = chapters[i + 1].start_ms - chapters[i].start_ms;
        } else if (total_duration_ms > chapters[i].start_ms) {
            chapters[i].duration_ms = total_duration_ms - chapters[i].start_ms;
        }
        if (chapters[i].duration_ms < 0) chapters[i].duration_ms = 0;
        snprintf(chapters[i].path, sizeof(chapters[i].path), "%s", path);
    }

    *out = chapters;
    return count;
}

// Copy one ilst tag's `data` payload (8 bytes of type/locale, then the text)
static bool read_ilst_text(FILE* f, int64_t start, int64_t end, char* out, int out_size) {
    if (!out || out_size <= 0) return false;

    int64_t ds, de;
    if (!find_box(f, start, end, BOX_DATA, &ds, &de)) return false;
    int64_t text_start = ds + 8;
    int64_t len = de - text_start;
    if (len <= 0) return false;
    if (len > out_size - 1) len = out_size - 1;

    if (!read_at(f, text_start, out, (size_t)len)) return false;
    out[len] = '\0';
    return out[0] != '\0';
}

bool M4B_readTags(const char* path, char* title, int title_size,
                  char* artist, int artist_size, char* album, int album_size) {
    if (!path) return false;

    FILE* f = NULL;
    int64_t moov_start, moov_end, file_size;
    if (!open_moov(path, &f, &moov_start, &moov_end, &file_size)) return false;

    // moov/udta/meta/ilst — `meta` is a full box, so its children start 4 bytes in
    int64_t udta_s, udta_e, meta_s, meta_e;
    bool found = false;
    if (find_box(f, moov_start, moov_end, BOX_UDTA, &udta_s, &udta_e) &&
        find_box(f, udta_s, udta_e, BOX_META, &meta_s, &meta_e)) {
        int64_t ilst_s, ilst_e;
        if (find_box(f, meta_s + 4, meta_e, BOX_ILST, &ilst_s, &ilst_e)) {
            int64_t s, e;
            if (title && find_box(f, ilst_s, ilst_e, BOX_NAM, &s, &e))
                found |= read_ilst_text(f, s, e, title, title_size);
            if (artist && find_box(f, ilst_s, ilst_e, BOX_ART, &s, &e))
                found |= read_ilst_text(f, s, e, artist, artist_size);
            if (album && find_box(f, ilst_s, ilst_e, BOX_ALB, &s, &e))
                found |= read_ilst_text(f, s, e, album, album_size);
        }
    }

    fclose(f);
    return found;
}
