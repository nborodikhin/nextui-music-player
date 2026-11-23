#ifndef __AUDIOBOOK_M4B_CHAPTERS_HPP__
#define __AUDIOBOOK_M4B_CHAPTERS_HPP__

#include <string>
#include <vector>

namespace audiobook {

/** A chapter of a book: a span of one audio file. */
struct Chapter {
    std::string title;
    /** The file that holds the chapter */
    std::string path;
    /** The start of the chapter in its file. A book of one file per chapter starts each one at 0. */
    int startMs    = 0;
    int durationMs = 0;
};

/** The iTunes tags of an MP4 file. A tag that the file does not have is empty. */
struct M4bTags {
    std::string title;
    std::string artist;
    std::string album;
};

/**
 * Returns the chapters that an MP4 file (.m4b, .m4a) carries: the Nero `chpl` atom, else a
 * QuickTime chapter track that a `tref`/`chap` of another track names. minimp4 demuxes the audio
 * only, thus this reads the boxes itself. Each chapter gets `path` as its file. The duration of
 * the file closes the last chapter; pass 0 when it is not known.
 * Returns: no chapter when the file has no chapter information
 */
std::vector<Chapter> parseM4bChapters(const std::string& path, int totalDurationMs);

/**
 * Reads `©nam`, `©ART` and `©alb` from moov/udta/meta/ilst.
 * Returns: false when the file has none of them
 */
bool readM4bTags(const std::string& path, M4bTags& tags);

}  // namespace audiobook

#endif
