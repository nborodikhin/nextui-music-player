#include "audiobook/library.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <strings.h>

#include "include/parson/parson.h"
#include "player.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

namespace audiobook {
namespace {

#define AUDIOBOOK_DATA_DIR SHARED_USERDATA_PATH "/music-player"

constexpr const char* kRootName     = "Audiobook";
constexpr const char* kDataDir      = AUDIOBOOK_DATA_DIR;
constexpr const char* kProgressFile = AUDIOBOOK_DATA_DIR "/audiobooks.json";
constexpr const char* kCacheFile    = AUDIOBOOK_DATA_DIR "/audiobook_library.json";

/** A folder with more files than this is not a book */
constexpr size_t kMaxChapters = 4096;

bool isAudioFile(const char* name) {
    return Player_detectFormat(name) != AUDIO_FORMAT_UNKNOWN;
}

std::string stripExtension(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool lessByName(const std::string& left, const std::string& right) {
    return strcasecmp(left.c_str(), right.c_str()) < 0;
}

/** The order of the list: the books in progress, the last played first, then the rest by title. */
bool lessForDisplay(const Book& left, const Book& right) {
    const bool leftActive  = left.isInProgress();
    const bool rightActive = right.isInProgress();
    if (leftActive != rightActive) return leftActive;
    if (leftActive && left.progress.lastPlayed != right.progress.lastPlayed) {
        return left.progress.lastPlayed > right.progress.lastPlayed;
    }
    return lessByName(left.title, right.title);
}

/** Returns the audio files of a folder, sorted by name. */
std::vector<std::string> listChapterFiles(const std::string& folder) {
    std::vector<std::string> names;
    DIR* dir = opendir(folder.c_str());
    if (!dir) return names;
    while (const dirent* entry = readdir(dir)) {
        if (names.size() >= kMaxChapters) break;
        if (entry->d_name[0] == '.' || !isAudioFile(entry->d_name)) continue;
        const std::string path = folder + "/" + entry->d_name;
        struct stat info;
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
        names.emplace_back(entry->d_name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end(), lessByName);
    return names;
}

const char* getString(const JSON_Object* object, const char* name) {
    const char* value = json_object_get_string(object, name);
    return value ? value : "";
}

JSON_Value* asJson(void* cache) {
    return static_cast<JSON_Value*>(cache);
}

}  // namespace

Library& Library::getInstance() {
    static Library library;
    return library;
}

std::string Library::getRootPath() {
    return std::string(SDCARD_PATH) + "/" + kRootName;
}

void Library::init() {
    if (initialized) return;
    mkdir(kDataDir, 0755);
    mkdir(getRootPath().c_str(), 0755);
    loadProgress();
    loadCache();
    initialized = true;
}

void Library::quit() {
    if (!initialized) return;
    flush();
    saveCache();
    books.clear();
    json_value_free(asJson(cache));
    cache       = nullptr;
    initialized = false;
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

void Library::loadProgress() {
    progress.clear();
    JSON_Value* root = json_parse_file(kProgressFile);
    if (!root) return;
    if (const JSON_Object* object = json_value_get_object(root)) {
        for (size_t index = 0; index < json_object_get_count(object); index++) {
            const char* key          = json_object_get_name(object, index);
            const JSON_Object* entry = json_value_get_object(json_object_get_value_at(object, index));
            if (!key || !entry) continue;
            Progress& saved  = progress[key];
            saved.chapter    = static_cast<int>(json_object_get_number(entry, "chapter"));
            saved.positionMs = static_cast<int>(json_object_get_number(entry, "position_ms"));
            saved.finished   = json_object_get_boolean(entry, "finished") == 1;
            saved.lastPlayed = static_cast<uint32_t>(json_object_get_number(entry, "last_played"));
        }
    }
    json_value_free(root);
}

void Library::flush() {
    if (!progressDirty) return;
    JSON_Value* root   = json_value_init_object();
    JSON_Object* object = json_value_get_object(root);
    for (const auto& [key, saved] : progress) {
        JSON_Value* entryValue = json_value_init_object();
        JSON_Object* entry     = json_value_get_object(entryValue);
        json_object_set_number(entry, "chapter", saved.chapter);
        json_object_set_number(entry, "position_ms", saved.positionMs);
        json_object_set_boolean(entry, "finished", saved.finished);
        json_object_set_number(entry, "last_played", saved.lastPlayed);
        // A key holds no dot that parson reads as a path, thus set_value and not dotset
        json_object_set_value(object, key.c_str(), entryValue);
    }
    if (json_serialize_to_file_pretty(root, kProgressFile) == JSONSuccess) progressDirty = false;
    json_value_free(root);
}

Progress* Library::findProgress(const std::string& key) {
    const auto found = progress.find(key);
    return found == progress.end() ? nullptr : &found->second;
}

void Library::saveProgress(const std::string& key, int chapter, int positionMs) {
    Progress& saved  = progress[key];
    saved.chapter    = std::max(chapter, 0);
    saved.positionMs = std::max(positionMs, 0);
    saved.lastPlayed = static_cast<uint32_t>(time(nullptr));
    progressDirty    = true;
    for (Book& book : books) {
        if (book.key == key) book.progress = saved;
    }
}

void Library::setFinished(const std::string& key, bool finished) {
    Progress& saved = progress[key];
    saved.finished  = finished;
    progressDirty   = true;
    for (Book& book : books) {
        if (book.key == key) book.progress.finished = finished;
    }
}

// ---------------------------------------------------------------------------
// Scan cache
// ---------------------------------------------------------------------------

void Library::loadCache() {
    JSON_Value* loaded = json_parse_file(kCacheFile);
    if (!loaded || json_value_get_type(loaded) != JSONObject) {
        json_value_free(loaded);
        loaded = json_value_init_object();
    }
    cache      = loaded;
    cacheDirty = false;
}

void Library::saveCache() {
    if (!cacheDirty || !cache) return;
    if (json_serialize_to_file_pretty(asJson(cache), kCacheFile) == JSONSuccess) cacheDirty = false;
}

void Library::storeInCache(const Book& book, uint32_t mtime, const std::vector<Chapter>& chapters) {
    JSON_Object* root = json_value_get_object(asJson(cache));
    if (!root) return;

    JSON_Value* entryValue = json_value_init_object();
    JSON_Object* entry     = json_value_get_object(entryValue);
    json_object_set_number(entry, "mtime", mtime);
    json_object_set_string(entry, "title", book.title.c_str());
    json_object_set_string(entry, "author", book.author.c_str());
    json_object_set_boolean(entry, "single_file", book.singleFile);
    json_object_set_number(entry, "duration_ms", book.totalDurationMs);

    JSON_Value* arrayValue = json_value_init_array();
    JSON_Array* array      = json_value_get_array(arrayValue);
    const size_t prefix    = book.path.size() + 1;  // "<book path>/"
    for (const Chapter& chapter : chapters) {
        JSON_Value* itemValue = json_value_init_object();
        JSON_Object* item     = json_value_get_object(itemValue);
        json_object_set_string(item, "title", chapter.title.c_str());
        // The name of the file only, thus the card can move to another device
        const std::string file = book.singleFile || chapter.path.size() <= prefix ? "" : chapter.path.substr(prefix);
        json_object_set_string(item, "file", file.c_str());
        json_object_set_number(item, "start_ms", chapter.startMs);
        json_object_set_number(item, "duration_ms", chapter.durationMs);
        json_array_append_value(array, itemValue);
    }
    json_object_set_value(entry, "chapters", arrayValue);
    json_object_set_value(root, book.key.c_str(), entryValue);
    cacheDirty = true;
}

// ---------------------------------------------------------------------------
// Books
// ---------------------------------------------------------------------------

std::vector<Chapter> Library::buildChapters(Book& book) {
    if (book.singleFile) {
        const int totalMs = Player_probeDuration(book.path.c_str());
        book.totalDurationMs = totalMs;
        if (Player_detectFormat(book.path.c_str()) == AUDIO_FORMAT_M4A) {
            M4bTags tags;
            if (readM4bTags(book.path, tags)) {
                if (!tags.album.empty()) book.title = tags.album;
                if (!tags.artist.empty()) book.author = tags.artist;
            }
            std::vector<Chapter> chapters = parseM4bChapters(book.path, totalMs);
            if (!chapters.empty()) return chapters;
        }
        // A file with no chapter of its own is one chapter
        return {Chapter{book.title, book.path, 0, totalMs}};
    }

    std::vector<Chapter> chapters;
    int totalMs = 0;
    for (const std::string& name : listChapterFiles(book.path)) {
        Chapter chapter;
        chapter.path       = book.path + "/" + name;
        chapter.title      = stripExtension(name);
        chapter.durationMs = Player_probeDuration(chapter.path.c_str());
        totalMs += chapter.durationMs;
        chapters.push_back(std::move(chapter));
    }
    book.totalDurationMs = totalMs;
    return chapters;
}

bool Library::resolveMetadata(Book& book, uint32_t mtime) {
    const JSON_Object* root  = json_value_get_object(asJson(cache));
    const JSON_Object* entry = root ? json_value_get_object(json_object_get_value(root, book.key.c_str())) : nullptr;
    if (entry && static_cast<uint32_t>(json_object_get_number(entry, "mtime")) == mtime) {
        if (getString(entry, "title")[0]) book.title = getString(entry, "title");
        book.author          = getString(entry, "author");
        book.totalDurationMs = static_cast<int>(json_object_get_number(entry, "duration_ms"));
        const JSON_Array* chapters = json_object_get_array(entry, "chapters");
        book.chapterCount = chapters ? static_cast<int>(json_array_get_count(chapters)) : 0;
        return book.chapterCount > 0;
    }

    const std::vector<Chapter> chapters = buildChapters(book);
    if (chapters.empty()) return false;
    book.chapterCount = static_cast<int>(chapters.size());
    storeInCache(book, mtime, chapters);
    return true;
}

void Library::scan() {
    init();
    books.clear();

    const std::string root = getRootPath();
    DIR* dir = opendir(root.c_str());
    if (!dir) return;
    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = root + "/" + entry->d_name;
        struct stat info;
        if (stat(path.c_str(), &info) != 0) continue;
        const bool isFolder = S_ISDIR(info.st_mode);
        if (!isFolder && !isAudioFile(entry->d_name)) continue;

        Book book;
        book.singleFile = !isFolder;
        book.path       = path;
        book.key        = entry->d_name;
        book.title      = isFolder ? std::string(entry->d_name) : stripExtension(entry->d_name);
        // A book with no playable chapter is not a book
        if (!resolveMetadata(book, static_cast<uint32_t>(info.st_mtime))) continue;

        if (const Progress* saved = findProgress(book.key)) {
            book.progress         = *saved;
            book.progress.chapter = std::clamp(saved->chapter, 0, book.chapterCount - 1);
        }
        books.push_back(std::move(book));
    }
    closedir(dir);

    std::stable_sort(books.begin(), books.end(), lessForDisplay);
    saveCache();
}

const Book* Library::findBook(const std::string& key) const {
    for (const Book& book : books) {
        if (book.key == key) return &book;
    }
    return nullptr;
}

const Book* Library::getResumeBook() const {
    const Book* best = nullptr;
    for (const Book& book : books) {
        if (!book.isInProgress()) continue;
        if (!best || book.progress.lastPlayed > best->progress.lastPlayed) best = &book;
    }
    return best;
}

std::vector<Chapter> Library::loadChapters(const Book& book) {
    init();
    const JSON_Object* root  = json_value_get_object(asJson(cache));
    const JSON_Object* entry = root ? json_value_get_object(json_object_get_value(root, book.key.c_str())) : nullptr;
    const JSON_Array* array  = entry ? json_object_get_array(entry, "chapters") : nullptr;

    std::vector<Chapter> chapters;
    if (array) {
        for (size_t index = 0; index < json_array_get_count(array) && index < kMaxChapters; index++) {
            const JSON_Object* item = json_array_get_object(array, index);
            if (!item) continue;
            Chapter chapter;
            chapter.title      = getString(item, "title");
            const char* file   = getString(item, "file");
            chapter.path       = book.singleFile || !file[0] ? book.path : book.path + "/" + file;
            chapter.startMs    = static_cast<int>(json_object_get_number(item, "start_ms"));
            chapter.durationMs = static_cast<int>(json_object_get_number(item, "duration_ms"));
            chapters.push_back(std::move(chapter));
        }
    }
    if (!chapters.empty()) return chapters;

    // No cache entry, as on the first run: read the files
    Book scratch = book;
    chapters     = buildChapters(scratch);
    if (!chapters.empty()) {
        struct stat info;
        const uint32_t mtime = stat(book.path.c_str(), &info) == 0 ? static_cast<uint32_t>(info.st_mtime) : 0;
        storeInCache(scratch, mtime, chapters);
        saveCache();
    }
    return chapters;
}

}  // namespace audiobook
