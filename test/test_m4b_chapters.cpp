// The chapters and the tags of synthetic MP4 files. Each test writes its file under
// TEST_TEMP_DIR, or /tmp where that is not set, and removes it.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "audiobook/m4b_chapters.hpp"
#include "test.h"

using audiobook::Chapter;
using audiobook::M4bTags;

namespace {

using Bytes = std::vector<uint8_t>;

void put32(Bytes& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

void put64(Bytes& out, uint64_t value) {
    put32(out, static_cast<uint32_t>(value >> 32));
    put32(out, static_cast<uint32_t>(value));
}

void putText(Bytes& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

/** A box of `type` around `payload`. */
Bytes box(const char* type, const Bytes& payload) {
    Bytes out;
    put32(out, static_cast<uint32_t>(8 + payload.size()));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}

/** A Nero chpl box of version 0 with `starts` in ms and `titles`. */
Bytes chpl(const std::vector<int>& starts, const std::vector<std::string>& titles) {
    Bytes payload = {0, 0, 0, 0};  // version, flags
    payload.push_back(static_cast<uint8_t>(starts.size()));
    for (size_t index = 0; index < starts.size(); index++) {
        put64(payload, static_cast<uint64_t>(starts[index]) * 10000ULL);
        payload.push_back(static_cast<uint8_t>(titles[index].size()));
        putText(payload, titles[index]);
    }
    return box("chpl", payload);
}

/** An ilst tag whose data box holds `text`. */
Bytes tag(const char* type, const std::string& text) {
    Bytes data = {0, 0, 0, 1, 0, 0, 0, 0};  // type and locale
    putText(data, text);
    return box(type, box("data", data));
}

/** Writes `bytes` to a new file, and returns its path. */
std::string writeFile(const Bytes& bytes) {
    const char* dir = getenv("TEST_TEMP_DIR");
    std::string path = std::string(dir && dir[0] ? dir : "/tmp") + "/m4b_test_XXXXXX";
    const int fd = mkstemp(path.data());
    if (fd < 0) return "";
    const ssize_t written = write(fd, bytes.data(), bytes.size());
    close(fd);
    return written == static_cast<ssize_t>(bytes.size()) ? path : "";
}

Bytes ftyp() {
    Bytes payload;
    putText(payload, "M4B ");
    put32(payload, 0);
    return box("ftyp", payload);
}

}  // namespace

TEST(nero_chapters_run_to_the_next_start_and_the_total) {
    const Bytes file = concat({ftyp(), box("moov", box("udta", chpl({0, 60000, 150000}, {"Intro", "", "End"})))});
    const std::string path = writeFile(file);
    CHECK(!path.empty());

    const std::vector<Chapter> chapters = audiobook::parseM4bChapters(path, 200000);
    CHECK_EQ_SZ(chapters.size(), 3u);
    CHECK(chapters[0].title == "Intro");
    // A chapter with no title gets its number
    CHECK(chapters[1].title == "Chapter 2");
    CHECK_EQ_INT(chapters[1].startMs, 60000);
    CHECK_EQ_INT(chapters[0].durationMs, 60000);
    CHECK_EQ_INT(chapters[1].durationMs, 90000);
    // The total closes the last chapter
    CHECK_EQ_INT(chapters[2].durationMs, 50000);
    CHECK(chapters[2].path == path);
    unlink(path.c_str());
}

TEST(a_file_with_no_chapter_gives_no_chapter) {
    const std::string path = writeFile(concat({ftyp(), box("moov", box("udta", Bytes{}))}));
    CHECK(audiobook::parseM4bChapters(path, 1000).empty());
    unlink(path.c_str());
}

TEST(a_file_that_is_not_mp4_gives_no_chapter) {
    const std::string path = writeFile(Bytes{'n', 'o', 't', ' ', 'a', 'n', ' ', 'm', 'p', '4'});
    CHECK(audiobook::parseM4bChapters(path, 1000).empty());
    M4bTags tags;
    CHECK(!audiobook::readM4bTags(path, tags));
    unlink(path.c_str());
}

TEST(tags_come_from_the_ilst_of_the_meta) {
    const Bytes ilst = box("ilst", concat({tag("\xA9nam", "Part One"), tag("\xA9" "ART", "An Author"),
                                           tag("\xA9" "alb", "The Book")}));
    const Bytes meta = box("meta", concat({Bytes{0, 0, 0, 0}, ilst}));
    const std::string path = writeFile(concat({ftyp(), box("moov", box("udta", meta))}));

    M4bTags tags;
    CHECK(audiobook::readM4bTags(path, tags));
    CHECK(tags.title == "Part One");
    CHECK(tags.artist == "An Author");
    CHECK(tags.album == "The Book");
    unlink(path.c_str());
}

TEST(quicktime_chapter_track_gives_its_samples) {
    // The text samples of the chapter track, in an mdat at the start of the file
    Bytes samples;
    const std::vector<std::string> titles = {"One", "Two"};
    std::vector<uint32_t> sizes;
    for (const std::string& title : titles) {
        samples.push_back(0);
        samples.push_back(static_cast<uint8_t>(title.size()));
        putText(samples, title);
        sizes.push_back(static_cast<uint32_t>(2 + title.size()));
    }
    const Bytes head  = ftyp();
    const Bytes mdat  = box("mdat", samples);
    const uint32_t samplesOffset = static_cast<uint32_t>(head.size() + 8);

    // The audio track names track 2 as its chapters
    Bytes chap;
    put32(chap, 2);
    const Bytes audioTrak = box("trak", box("tref", box("chap", chap)));

    Bytes tkhd = {0, 0, 0, 0};
    put32(tkhd, 0);  // creation
    put32(tkhd, 0);  // modification
    put32(tkhd, 2);  // track id
    Bytes mdhd = {0, 0, 0, 0};
    put32(mdhd, 0);
    put32(mdhd, 0);
    put32(mdhd, 1000);  // timescale

    Bytes stts = {0, 0, 0, 0};
    put32(stts, 2);  // entries
    put32(stts, 1);
    put32(stts, 30000);
    put32(stts, 1);
    put32(stts, 45000);
    Bytes stsz = {0, 0, 0, 0};
    put32(stsz, 0);  // sizes differ
    put32(stsz, 2);
    for (const uint32_t size : sizes) put32(stsz, size);
    Bytes stsc = {0, 0, 0, 0};
    put32(stsc, 1);
    put32(stsc, 1);  // first chunk
    put32(stsc, 2);  // samples per chunk
    put32(stsc, 1);  // description
    Bytes stco = {0, 0, 0, 0};
    put32(stco, 1);
    put32(stco, samplesOffset);

    const Bytes stbl = box("stbl", concat({box("stts", stts), box("stsz", stsz), box("stsc", stsc), box("stco", stco)}));
    const Bytes textTrak = box("trak", concat({box("tkhd", tkhd), box("mdia", concat({box("mdhd", mdhd),
                                                                                  box("minf", stbl)}))}));
    const std::string path = writeFile(concat({head, mdat, box("moov", concat({audioTrak, textTrak}))}));

    const std::vector<Chapter> chapters = audiobook::parseM4bChapters(path, 75000);
    CHECK_EQ_SZ(chapters.size(), 2u);
    CHECK(chapters[0].title == "One");
    CHECK(chapters[1].title == "Two");
    CHECK_EQ_INT(chapters[1].startMs, 30000);
    CHECK_EQ_INT(chapters[1].durationMs, 45000);
    unlink(path.c_str());
}

int main(void) {
    RUN(nero_chapters_run_to_the_next_start_and_the_total);
    RUN(a_file_with_no_chapter_gives_no_chapter);
    RUN(a_file_that_is_not_mp4_gives_no_chapter);
    RUN(tags_come_from_the_ilst_of_the_meta);
    RUN(quicktime_chapter_track_gives_its_samples);
    return test_summary();
}
