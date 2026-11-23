#ifndef __AUDIOBOOK_LIBRARY_HPP__
#define __AUDIOBOOK_LIBRARY_HPP__

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "audiobook/m4b_chapters.hpp"

namespace audiobook {

/** The saved place of a book. */
struct Progress {
    int chapter     = 0;
    /** The position in the chapter */
    int positionMs  = 0;
    bool finished   = false;
    /** Unix time of the last save, 0 for a book that never started */
    uint32_t lastPlayed = 0;
};

/**
 * A book: a folder of /Audiobook, whose audio files sorted by name are the chapters, or one audio
 * file at the top of /Audiobook, whose chapters come from its MP4 container, or which is one
 * chapter.
 */
struct Book {
    std::string title;
    std::string author;
    /** The folder, or the file of a book of one file */
    std::string path;
    /** The path from /Audiobook, which names the progress of the book */
    std::string key;
    bool singleFile     = false;
    int chapterCount    = 0;
    int totalDurationMs = 0;
    Progress progress;

    bool isInProgress() const { return progress.lastPlayed > 0 && !progress.finished; }
};

/**
 * The books of /Audiobook and their progress. The metadata of each book stays in a cache on the
 * card, which a change of the time of the book renews, thus a scan reads the files of a new book
 * only. The progress stays in memory until flush().
 *
 * The files keep the names and the format of the first version of the feature, thus an update
 * keeps the progress of each book.
 */
class Library {
public:
    static Library& getInstance();

    /** Returns the folder of the books. */
    static std::string getRootPath();

    /** Reads the progress and the cache. Does nothing after the first call. */
    void init();

    /** Writes the progress and the cache, and forgets the books. */
    void quit();

    /**
     * Reads /Audiobook again. The books in progress come first, the last one played at the top,
     * then the others by title.
     */
    void scan();

    const std::vector<Book>& getBooks() const { return books; }

    /** Returns the book of `key`, or nullptr. A scan makes the pointer invalid. */
    const Book* findBook(const std::string& key) const;

    /** Returns the book in progress that played last, or nullptr. */
    const Book* getResumeBook() const;

    /** Returns the chapters of `book`, from the cache where it is current. */
    std::vector<Chapter> loadChapters(const Book& book);

    /** Records the place of the book of `key`, at the time of now. */
    void saveProgress(const std::string& key, int chapter, int positionMs);

    /** Marks the book of `key` finished or not. Keeps its place, thus its end can play again. */
    void setFinished(const std::string& key, bool finished);

    /** Writes the progress to the card where it changed. */
    void flush();

private:
    Library() = default;

    /** Reads the metadata of `book` from the cache, or from its files. Returns false for no chapter. */
    bool resolveMetadata(Book& book, uint32_t mtime);

    /** Reads the chapters of `book` from its files, and sets its title, author and duration. */
    std::vector<Chapter> buildChapters(Book& book);

    void storeInCache(const Book& book, uint32_t mtime, const std::vector<Chapter>& chapters);
    void loadProgress();
    void loadCache();
    void saveCache();
    Progress* findProgress(const std::string& key);

    bool initialized = false;
    std::vector<Book> books;
    std::map<std::string, Progress> progress;
    bool progressDirty = false;
    /** The scan cache, as a parson JSON_Value. Opaque here, thus the header needs no C include. */
    void* cache       = nullptr;
    bool cacheDirty   = false;
};

}  // namespace audiobook

#endif
