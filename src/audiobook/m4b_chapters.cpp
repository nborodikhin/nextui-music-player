#include "audiobook/m4b_chapters.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>

namespace audiobook {
namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(d));
}

constexpr uint32_t kBoxMoov = fourcc('m', 'o', 'o', 'v');
constexpr uint32_t kBoxUdta = fourcc('u', 'd', 't', 'a');
constexpr uint32_t kBoxChpl = fourcc('c', 'h', 'p', 'l');
constexpr uint32_t kBoxTrak = fourcc('t', 'r', 'a', 'k');
constexpr uint32_t kBoxTref = fourcc('t', 'r', 'e', 'f');
constexpr uint32_t kBoxChap = fourcc('c', 'h', 'a', 'p');
constexpr uint32_t kBoxTkhd = fourcc('t', 'k', 'h', 'd');
constexpr uint32_t kBoxMdia = fourcc('m', 'd', 'i', 'a');
constexpr uint32_t kBoxMdhd = fourcc('m', 'd', 'h', 'd');
constexpr uint32_t kBoxMinf = fourcc('m', 'i', 'n', 'f');
constexpr uint32_t kBoxStbl = fourcc('s', 't', 'b', 'l');
constexpr uint32_t kBoxStts = fourcc('s', 't', 't', 's');
constexpr uint32_t kBoxStsz = fourcc('s', 't', 's', 'z');
constexpr uint32_t kBoxStsc = fourcc('s', 't', 's', 'c');
constexpr uint32_t kBoxStco = fourcc('s', 't', 'c', 'o');
constexpr uint32_t kBoxCo64 = fourcc('c', 'o', '6', '4');
constexpr uint32_t kBoxMeta = fourcc('m', 'e', 't', 'a');
constexpr uint32_t kBoxIlst = fourcc('i', 'l', 's', 't');
constexpr uint32_t kBoxData = fourcc('d', 'a', 't', 'a');
constexpr uint32_t kBoxName   = fourcc('\xA9', 'n', 'a', 'm');
constexpr uint32_t kBoxArtist = fourcc('\xA9', 'A', 'R', 'T');
constexpr uint32_t kBoxAlbum  = fourcc('\xA9', 'a', 'l', 'b');

/** A chapter list longer than this comes from a corrupt or hostile file */
constexpr uint32_t kMaxChapters = 4096;

/** The longest title that a chapter keeps, in bytes */
constexpr size_t kMaxTitleBytes = 255;

/** The payload of a box: from `start` to `end`, in bytes from the start of the file. */
struct Box {
    int64_t start;
    int64_t end;
};

/** One box header: its type, its payload, and the offset of the box after it. */
struct BoxHeader {
    uint32_t type;
    Box payload;
    int64_t next;
};

struct FileCloser {
    void operator()(FILE* file) const { fclose(file); }
};

/** An MP4 file, read as a tree of boxes, with big-endian numbers. */
class Mp4File {
public:
    /** Opens `path`, and finds its moov box. Check isValid() after. */
    explicit Mp4File(const std::string& path) : file(fopen(path.c_str(), "rb")) {
        if (!file) return;
        if (fseek(file.get(), 0, SEEK_END) != 0) return;
        const long size = ftell(file.get());
        if (size <= 0) return;
        moov = findChild(Box{0, size}, kBoxMoov);
    }

    bool isValid() const { return moov.has_value(); }
    Box getMoov() const { return *moov; }

    bool seek(int64_t offset) { return fseek(file.get(), static_cast<long>(offset), SEEK_SET) == 0; }
    bool skip(int64_t count) { return fseek(file.get(), static_cast<long>(count), SEEK_CUR) == 0; }
    int64_t tell() { return ftell(file.get()); }

    bool read(void* buffer, size_t size) { return fread(buffer, 1, size, file.get()) == size; }

    bool readAt(int64_t offset, void* buffer, size_t size) { return seek(offset) && read(buffer, size); }

    std::optional<uint8_t> read8() {
        uint8_t value;
        if (!read(&value, 1)) return std::nullopt;
        return value;
    }

    std::optional<uint32_t> read32() {
        unsigned char bytes[4];
        if (!read(bytes, sizeof(bytes))) return std::nullopt;
        return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
               (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
    }

    std::optional<uint64_t> read64() {
        const std::optional<uint32_t> high = read32();
        const std::optional<uint32_t> low  = read32();
        if (!high || !low) return std::nullopt;
        return (static_cast<uint64_t>(*high) << 32) | *low;
    }

    /** Reads the header of the box at `position`, which must end inside `limit`. */
    std::optional<BoxHeader> readHeader(int64_t position, int64_t limit) {
        if (position + 8 > limit || !seek(position)) return std::nullopt;
        const std::optional<uint32_t> size32 = read32();
        const std::optional<uint32_t> type   = read32();
        if (!size32 || !type) return std::nullopt;

        int64_t header = 8;
        int64_t size   = *size32;
        if (*size32 == 1) {
            const std::optional<uint64_t> size64 = read64();
            if (!size64) return std::nullopt;
            header = 16;
            size   = static_cast<int64_t>(*size64);
        } else if (*size32 == 0) {
            // The box runs to the end of the box around it
            size = limit - position;
        }
        if (size < header || position + size > limit) return std::nullopt;
        return BoxHeader{*type, Box{position + header, position + size}, position + size};
    }

    /** Returns the payload of the first child of `parent` of `type`. */
    std::optional<Box> findChild(Box parent, uint32_t type) {
        int64_t position = parent.start;
        while (position < parent.end) {
            const std::optional<BoxHeader> header = readHeader(position, parent.end);
            if (!header) return std::nullopt;
            if (header->type == type) return header->payload;
            position = header->next;
        }
        return std::nullopt;
    }

    /** Follows a chain of nested types from `parent`, such as udta then chpl. */
    std::optional<Box> findPath(Box parent, std::span<const uint32_t> types) {
        std::optional<Box> box = parent;
        for (const uint32_t type : types) {
            box = findChild(*box, type);
            if (!box) return std::nullopt;
        }
        return box;
    }

    /** Calls `visit` with the payload of each child of `parent` of `type`, until it returns true. */
    template <class Visit>
    void forEachChild(Box parent, uint32_t type, Visit visit) {
        int64_t position = parent.start;
        while (position < parent.end) {
            const std::optional<BoxHeader> header = readHeader(position, parent.end);
            if (!header) return;
            if (header->type == type && visit(header->payload)) return;
            position = header->next;
        }
    }

private:
    std::unique_ptr<FILE, FileCloser> file;
    std::optional<Box> moov;
};

/** Returns the title of a chapter, or "Chapter N" for one with no title. */
std::string titleOrNumber(std::string title, size_t index) {
    if (!title.empty()) return title;
    return "Chapter " + std::to_string(index + 1);
}

// ---------------------------------------------------------------------------
// Nero `chpl`
// ---------------------------------------------------------------------------

/**
 * Layout: version(1) flags(3) [reserved(4) when version != 0] count(1), then for each entry the
 * start as a 64-bit count of 100 ns, and a Pascal string (1-byte length, UTF-8).
 */
std::vector<Chapter> parseChpl(Mp4File& file, Box chpl) {
    std::vector<Chapter> chapters;
    if (!file.seek(chpl.start)) return chapters;

    const std::optional<uint8_t> version = file.read8();
    if (!version || !file.skip(3)) return chapters;
    if (*version != 0 && !file.read32()) return chapters;

    const std::optional<uint8_t> count = file.read8();
    if (!count) return chapters;

    for (int index = 0; index < *count; index++) {
        if (file.tell() >= chpl.end) break;
        const std::optional<uint64_t> start100ns = file.read64();
        const std::optional<uint8_t> titleLength = file.read8();
        if (!start100ns || !titleLength) break;

        std::string title(*titleLength, '\0');
        if (*titleLength > 0 && !file.read(title.data(), title.size())) break;

        Chapter chapter;
        chapter.startMs = static_cast<int>(*start100ns / 10000ULL);
        chapter.title   = titleOrNumber(std::move(title), chapters.size());
        chapters.push_back(std::move(chapter));
    }
    return chapters;
}

// ---------------------------------------------------------------------------
// QuickTime chapter track
// ---------------------------------------------------------------------------

/** Reads the track_id of the tkhd box of a trak. */
std::optional<uint32_t> readTrackId(Mp4File& file, Box trak) {
    const std::optional<Box> tkhd = file.findChild(trak, kBoxTkhd);
    if (!tkhd || !file.seek(tkhd->start)) return std::nullopt;
    const std::optional<uint8_t> version = file.read8();
    // The flags, then the creation and the modification times
    if (!version || !file.skip(3) || !file.skip(*version == 1 ? 16 : 8)) return std::nullopt;
    return file.read32();
}

/** Reads the timescale of mdia/mdhd of a trak. */
std::optional<uint32_t> readTimescale(Mp4File& file, Box trak) {
    constexpr uint32_t kPath[] = {kBoxMdia, kBoxMdhd};
    const std::optional<Box> mdhd = file.findPath(trak, kPath);
    if (!mdhd || !file.seek(mdhd->start)) return std::nullopt;
    const std::optional<uint8_t> version = file.read8();
    if (!version || !file.skip(3) || !file.skip(*version == 1 ? 16 : 8)) return std::nullopt;
    return file.read32();
}

/** The start and the duration of each sample of a track, in the timescale of the track. */
struct SampleTimes {
    std::vector<int64_t> starts;
    std::vector<int64_t> durations;
};

std::optional<SampleTimes> readSampleTimes(Mp4File& file, Box stbl) {
    const std::optional<Box> stts = file.findChild(stbl, kBoxStts);
    if (!stts || !file.seek(stts->start + 4)) return std::nullopt;
    const std::optional<uint32_t> entryCount = file.read32();
    if (!entryCount) return std::nullopt;

    SampleTimes times;
    int64_t clock = 0;
    for (uint32_t entry = 0; entry < *entryCount && times.starts.size() < kMaxChapters; entry++) {
        const std::optional<uint32_t> run   = file.read32();
        const std::optional<uint32_t> delta = file.read32();
        if (!run || !delta) break;
        for (uint32_t sample = 0; sample < *run && times.starts.size() < kMaxChapters; sample++) {
            times.starts.push_back(clock);
            times.durations.push_back(*delta);
            clock += *delta;
        }
    }
    if (times.starts.empty()) return std::nullopt;
    return times;
}

/** Returns the size of each of `count` samples, from stsz. */
std::optional<std::vector<uint32_t>> readSampleSizes(Mp4File& file, Box stbl, size_t count) {
    const std::optional<Box> stsz = file.findChild(stbl, kBoxStsz);
    if (!stsz || !file.seek(stsz->start + 4)) return std::nullopt;
    const std::optional<uint32_t> uniformSize = file.read32();
    const std::optional<uint32_t> sizeCount   = file.read32();
    if (!uniformSize || !sizeCount) return std::nullopt;
    if (*uniformSize != 0) return std::vector<uint32_t>(count, *uniformSize);

    if (*sizeCount < count) return std::nullopt;
    std::vector<uint32_t> sizes;
    sizes.reserve(count);
    for (size_t sample = 0; sample < count; sample++) {
        const std::optional<uint32_t> size = file.read32();
        if (!size) return std::nullopt;
        sizes.push_back(*size);
    }
    return sizes;
}

/** Returns the file offset of each sample: stsc maps samples to chunks, and stco/co64 places them. */
std::optional<std::vector<int64_t>> readSampleOffsets(Mp4File& file, Box stbl,
                                                      const std::vector<uint32_t>& sizes) {
    struct ChunkRun {
        uint32_t firstChunk;
        uint32_t samplesPerChunk;
    };

    const std::optional<Box> stsc = file.findChild(stbl, kBoxStsc);
    if (!stsc || !file.seek(stsc->start + 4)) return std::nullopt;
    const std::optional<uint32_t> runCount = file.read32();
    if (!runCount || *runCount == 0 || *runCount > kMaxChapters) return std::nullopt;
    std::vector<ChunkRun> runs;
    for (uint32_t index = 0; index < *runCount; index++) {
        const std::optional<uint32_t> firstChunk      = file.read32();
        const std::optional<uint32_t> samplesPerChunk = file.read32();
        const std::optional<uint32_t> descriptionId   = file.read32();
        if (!firstChunk || !samplesPerChunk || !descriptionId) return std::nullopt;
        runs.push_back({*firstChunk, *samplesPerChunk});
    }

    bool is64 = false;
    std::optional<Box> chunkBox = file.findChild(stbl, kBoxStco);
    if (!chunkBox) {
        chunkBox = file.findChild(stbl, kBoxCo64);
        is64     = true;
    }
    if (!chunkBox || !file.seek(chunkBox->start + 4)) return std::nullopt;
    const std::optional<uint32_t> chunkCount = file.read32();
    if (!chunkCount || *chunkCount == 0 || *chunkCount > kMaxChapters) return std::nullopt;
    std::vector<int64_t> chunkOffsets;
    for (uint32_t chunk = 0; chunk < *chunkCount; chunk++) {
        if (is64) {
            const std::optional<uint64_t> offset = file.read64();
            if (!offset) return std::nullopt;
            chunkOffsets.push_back(static_cast<int64_t>(*offset));
        } else {
            const std::optional<uint32_t> offset = file.read32();
            if (!offset) return std::nullopt;
            chunkOffsets.push_back(*offset);
        }
    }

    // The samples of a chunk follow one another from the offset of the chunk
    std::vector<int64_t> offsets;
    for (uint32_t chunk = 0; chunk < chunkOffsets.size() && offsets.size() < sizes.size(); chunk++) {
        // The last run whose first chunk (counted from 1) is at or before this chunk
        uint32_t perChunk = runs[0].samplesPerChunk;
        for (const ChunkRun& run : runs) {
            if (run.firstChunk > chunk + 1) break;
            perChunk = run.samplesPerChunk;
        }
        int64_t offset = chunkOffsets[chunk];
        for (uint32_t sample = 0; sample < perChunk && offsets.size() < sizes.size(); sample++) {
            offsets.push_back(offset);
            offset += sizes[offsets.size() - 1];
        }
    }
    if (offsets.empty()) return std::nullopt;
    return offsets;
}

/** Reads the text of a chapter sample: a 16-bit length, then the UTF-8 title. */
std::string readSampleTitle(Mp4File& file, int64_t offset, uint32_t sampleSize) {
    if (sampleSize < 2) return "";
    unsigned char lengthBytes[2];
    if (!file.readAt(offset, lengthBytes, sizeof(lengthBytes))) return "";
    size_t length = (static_cast<size_t>(lengthBytes[0]) << 8) | lengthBytes[1];
    if (length > sampleSize - 2) length = sampleSize - 2;
    if (length > kMaxTitleBytes) length = kMaxTitleBytes;
    std::string title(length, '\0');
    if (length > 0 && !file.read(title.data(), length)) return "";
    return title;
}

/** Decodes the sample table of a chapter track into chapters. */
std::vector<Chapter> parseChapterTrack(Mp4File& file, Box trak) {
    std::vector<Chapter> chapters;
    const std::optional<uint32_t> timescale = readTimescale(file, trak);
    if (!timescale || *timescale == 0) return chapters;

    constexpr uint32_t kStblPath[] = {kBoxMdia, kBoxMinf, kBoxStbl};
    const std::optional<Box> stbl = file.findPath(trak, kStblPath);
    if (!stbl) return chapters;

    const std::optional<SampleTimes> times = readSampleTimes(file, *stbl);
    if (!times) return chapters;
    const std::optional<std::vector<uint32_t>> sizes = readSampleSizes(file, *stbl, times->starts.size());
    if (!sizes) return chapters;
    const std::optional<std::vector<int64_t>> offsets = readSampleOffsets(file, *stbl, *sizes);
    if (!offsets) return chapters;

    for (size_t sample = 0; sample < offsets->size(); sample++) {
        Chapter chapter;
        chapter.startMs    = static_cast<int>(times->starts[sample] * 1000 / *timescale);
        chapter.durationMs = static_cast<int>(times->durations[sample] * 1000 / *timescale);
        chapter.title      = titleOrNumber(readSampleTitle(file, (*offsets)[sample], (*sizes)[sample]), sample);
        chapters.push_back(std::move(chapter));
    }
    return chapters;
}

/** Finds the track that the tref/chap of another track names, and decodes it. */
std::vector<Chapter> parseQuickTimeChapters(Mp4File& file) {
    const Box moov = file.getMoov();

    std::optional<uint32_t> chapterTrackId;
    file.forEachChild(moov, kBoxTrak, [&](Box trak) {
        constexpr uint32_t kChapPath[] = {kBoxTref, kBoxChap};
        const std::optional<Box> chap = file.findPath(trak, kChapPath);
        if (!chap || chap->end - chap->start < 4 || !file.seek(chap->start)) return false;
        chapterTrackId = file.read32();
        return chapterTrackId.has_value() && *chapterTrackId != 0;
    });
    if (!chapterTrackId || *chapterTrackId == 0) return {};

    std::vector<Chapter> chapters;
    file.forEachChild(moov, kBoxTrak, [&](Box trak) {
        if (readTrackId(file, trak) != chapterTrackId) return false;
        chapters = parseChapterTrack(file, trak);
        return true;
    });
    return chapters;
}

/** Reads the text of the `data` child of one ilst tag: 8 bytes of type and locale, then the text. */
std::string readIlstText(Mp4File& file, Box tag) {
    const std::optional<Box> data = file.findChild(tag, kBoxData);
    if (!data) return "";
    const int64_t textStart = data->start + 8;
    int64_t length          = data->end - textStart;
    if (length <= 0) return "";
    if (length > static_cast<int64_t>(kMaxTitleBytes)) length = kMaxTitleBytes;
    std::string text(static_cast<size_t>(length), '\0');
    if (!file.readAt(textStart, text.data(), text.size())) return "";
    // A text can end in NULs
    text.resize(text.find('\0') == std::string::npos ? text.size() : text.find('\0'));
    return text;
}

}  // namespace

std::vector<Chapter> parseM4bChapters(const std::string& path, int totalDurationMs) {
    Mp4File file(path);
    if (!file.isValid()) return {};

    // The Nero chpl atom is by far the most common in .m4b files
    std::vector<Chapter> chapters;
    constexpr uint32_t kChplPath[] = {kBoxUdta, kBoxChpl};
    if (const std::optional<Box> chpl = file.findPath(file.getMoov(), kChplPath)) {
        chapters = parseChpl(file, *chpl);
    }
    if (chapters.empty()) chapters = parseQuickTimeChapters(file);

    // chpl carries no duration, thus each one runs to the start of the next chapter
    for (size_t index = 0; index < chapters.size(); index++) {
        Chapter& chapter = chapters[index];
        if (index + 1 < chapters.size()) {
            chapter.durationMs = chapters[index + 1].startMs - chapter.startMs;
        } else if (totalDurationMs > chapter.startMs) {
            chapter.durationMs = totalDurationMs - chapter.startMs;
        }
        if (chapter.durationMs < 0) chapter.durationMs = 0;
        chapter.path = path;
    }
    return chapters;
}

bool readM4bTags(const std::string& path, M4bTags& tags) {
    Mp4File file(path);
    if (!file.isValid()) return false;

    const std::optional<Box> udta = file.findChild(file.getMoov(), kBoxUdta);
    if (!udta) return false;
    const std::optional<Box> meta = file.findChild(*udta, kBoxMeta);
    if (!meta) return false;
    // meta is a full box, thus its children start after its version and flags
    const std::optional<Box> ilst = file.findChild(Box{meta->start + 4, meta->end}, kBoxIlst);
    if (!ilst) return false;

    if (const std::optional<Box> tag = file.findChild(*ilst, kBoxName)) tags.title = readIlstText(file, *tag);
    if (const std::optional<Box> tag = file.findChild(*ilst, kBoxArtist)) tags.artist = readIlstText(file, *tag);
    if (const std::optional<Box> tag = file.findChild(*ilst, kBoxAlbum)) tags.album = readIlstText(file, *tag);
    return !tags.title.empty() || !tags.artist.empty() || !tags.album.empty();
}

}  // namespace audiobook
