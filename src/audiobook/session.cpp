#include "audiobook/session.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include <SDL2/SDL.h>

#include "audiobook_session.h"
#include "background.h"
#include "module_common.h"
#include "player.h"
#include "ui_album_art.h"

namespace audiobook {
namespace {

/** How often the place of a book that plays goes to the card */
constexpr uint32_t kSaveIntervalMs = 30000;

/** The options of the sleep timer in their order: off, minutes, and -1 for the end of the chapter */
constexpr int kSleepMinutes[] = {0, 15, 30, 45, 60, -1};
constexpr int kSleepOptionCount = static_cast<int>(std::size(kSleepMinutes));

}  // namespace

Session& Session::getInstance() {
    static Session session;
    return session;
}

const Book* Session::getBook() const {
    return isOpen() ? Library::getInstance().findBook(key) : nullptr;
}

const Chapter* Session::getChapter() const {
    if (chapterIndex < 0 || chapterIndex >= static_cast<int>(chapters.size())) return nullptr;
    return &chapters[static_cast<size_t>(chapterIndex)];
}

bool Session::isPaused() const {
    return isOpen() && !pendingPlay && Player_getState() == PLAYER_STATE_PAUSED;
}

int Session::getChapterPositionMs() const {
    const Chapter* chapter = getChapter();
    // A book of one file plays the whole file, thus the player counts from the start of the book
    const int position = Player_getPosition() - (chapter ? chapter->startMs : 0);
    return std::max(position, 0);
}

int Session::getChapterDurationMs() const {
    const Chapter* chapter = getChapter();
    if (chapter && chapter->durationMs > 0) return chapter->durationMs;
    return Player_getDuration();
}

int Session::getBookPositionMs() const {
    int offset = 0;
    for (int index = 0; index < chapterIndex && index < static_cast<int>(chapters.size()); index++) {
        offset += chapters[static_cast<size_t>(index)].durationMs;
    }
    return offset + getChapterPositionMs();
}

bool Session::open(const std::string& bookKey) {
    Library& library = Library::getInstance();
    const Book* book = library.findBook(bookKey);
    if (!book) return false;

    // One source plays at a time. A book that plays saves its place on the way out.
    Background_stopAll();

    std::vector<Chapter> loaded = library.loadChapters(*book);
    if (loaded.empty()) return false;

    key      = bookKey;
    chapters = std::move(loaded);
    resetSleepTimer();
    endReason = EndReason::None;

    const int chapter = std::clamp(book->progress.chapter, 0, static_cast<int>(chapters.size()) - 1);
    // A finished book plays again from its start
    const int position = book->progress.finished ? 0 : book->progress.positionMs;
    if (!startChapter(book->progress.finished ? 0 : chapter, position)) {
        key.clear();
        chapters.clear();
        return false;
    }
    if (book->progress.finished) library.setFinished(key, false);

    Background_setActive(BG_AUDIOBOOK);
    ModuleCommon_setAutosleepDisabled(true);
    return true;
}

bool Session::startChapter(int index, int positionMs) {
    if (index < 0 || index >= static_cast<int>(chapters.size())) return false;
    const Chapter& chapter = chapters[static_cast<size_t>(index)];

    // A book of one file stays loaded from chapter to chapter
    const char* loadedFile = Player_getCurrentFile();
    const bool loaded      = loadedFile && chapter.path == loadedFile && Player_getState() != PLAYER_STATE_STOPPED;
    if (!loaded) {
        // The cache of the background keys on the address of the art, which the load frees
        cleanup_album_art_background();
        if (Player_load(chapter.path.c_str()) != 0) return false;
    }

    chapterIndex = index;
    playbackSeen = false;
    lastSaveTime = SDL_GetTicks();

    const int seekMs = chapter.startMs + positionMs;
    if (seekMs > 0 || loaded) {
        Player_seek(seekMs);
        pendingPlay = true;
    } else {
        pendingPlay = false;
        Player_play();
    }
    return true;
}

void Session::saveNow() {
    if (!isOpen() || pendingPlay) return;
    Library& library = Library::getInstance();
    library.saveProgress(key, chapterIndex, getChapterPositionMs());
    library.flush();
}

void Session::close() {
    stop(true);
}

void Session::stop(bool save) {
    if (!isOpen()) return;
    if (save) saveNow();
    Player_stop();
    cleanup_album_art_background();
    key.clear();
    chapters.clear();
    pendingPlay  = false;
    playbackSeen = false;
    resetSleepTimer();
    if (Background_getActive() == BG_AUDIOBOOK) Background_setActive(BG_NONE);
    ModuleCommon_setAutosleepDisabled(false);
}

void Session::end(EndReason reason, bool save) {
    stop(save);
    endReason = reason;
}

Session::EndReason Session::takeEndReason() {
    const EndReason reason = endReason;
    endReason              = EndReason::None;
    return reason;
}

void Session::togglePause() {
    if (!isOpen() || pendingPlay) return;
    if (Player_getState() == PLAYER_STATE_PAUSED) {
        Player_play();
    } else {
        Player_pause();
        saveNow();
    }
}

void Session::skip(int deltaMs) {
    if (!isOpen() || pendingPlay) return;
    const Chapter* chapter = getChapter();
    const int start        = chapter ? chapter->startMs : 0;
    const int end          = start + getChapterDurationMs();
    // A skip back stops at the start of the chapter, and a skip forward at its end
    int target = Player_getPosition() + deltaMs;
    target     = std::max(target, start);
    if (end > start) target = std::min(target, end);
    Player_seek(target);
}

bool Session::playChapter(int index) {
    if (!isOpen()) return false;
    if (!startChapter(index, 0)) return false;
    Library::getInstance().saveProgress(key, chapterIndex, 0);
    Library::getInstance().flush();
    return true;
}

void Session::resetSleepTimer() {
    sleepOption       = 0;
    sleepDeadline     = 0;
    sleepAtChapterEnd = false;
}

std::string Session::cycleSleepTimer() {
    sleepOption       = (sleepOption + 1) % kSleepOptionCount;
    sleepDeadline     = 0;
    sleepAtChapterEnd = false;
    const int minutes = kSleepMinutes[sleepOption];
    if (minutes > 0) {
        sleepDeadline = SDL_GetTicks() + static_cast<uint32_t>(minutes) * 60000u;
        return "Sleep timer: " + std::to_string(minutes) + " min";
    }
    if (minutes < 0) {
        sleepAtChapterEnd = true;
        return "Sleep timer: end of chapter";
    }
    return "Sleep timer off";
}

std::string Session::getSleepLabel() const {
    if (sleepAtChapterEnd) return "end of chapter";
    if (sleepDeadline == 0) return "";
    const uint32_t now       = SDL_GetTicks();
    const uint32_t remaining = sleepDeadline > now ? (sleepDeadline - now) / 1000 : 0;
    char label[16];
    snprintf(label, sizeof(label), "%u:%02u", remaining / 60, remaining % 60);
    return label;
}

void Session::tick() {
    if (!isOpen()) return;
    Player_update();

    // The seek to the saved place runs over some frames. The sound starts after it.
    if (pendingPlay) {
        if (Player_resume()) return;
        pendingPlay = false;
        Player_play();
        lastSaveTime = SDL_GetTicks();
    }

    // Until the player played, STOPPED means that the sound did not start yet
    const PlayerState state = Player_getState();
    if (state != PLAYER_STATE_STOPPED) {
        playbackSeen = true;
    } else if (!playbackSeen) {
        return;
    }

    Library& library     = Library::getInstance();
    const int count      = static_cast<int>(chapters.size());
    bool chapterChanged  = false;

    // A book of one file plays through: follow the chapter of the position
    const Book* book = getBook();
    if (book && book->singleFile) {
        const int position = Player_getPosition();
        while (chapterIndex + 1 < count && position >= chapters[static_cast<size_t>(chapterIndex + 1)].startMs) {
            chapterIndex++;
            chapterChanged = true;
        }
    }

    if (state == PLAYER_STATE_STOPPED) {
        // A book of one file per chapter ends its file at each chapter
        const bool hasNext = chapterIndex + 1 < count && !(book && book->singleFile);
        if (hasNext && sleepAtChapterEnd) {
            // The next listen starts at the next chapter
            library.saveProgress(key, chapterIndex + 1, 0);
            library.flush();
            end(EndReason::SleepTimer, false);
            return;
        }
        if (hasNext && startChapter(chapterIndex + 1, 0)) {
            library.saveProgress(key, chapterIndex, 0);
            library.flush();
            return;
        }
        // The end of the book. The place stays on its last chapter, thus its end can play again.
        library.saveProgress(key, std::max(count - 1, 0), 0);
        library.setFinished(key, true);
        library.flush();
        end(EndReason::Finished, false);
        return;
    }

    if (chapterChanged) {
        saveNow();
        if (sleepAtChapterEnd) {
            end(EndReason::SleepTimer, true);
            return;
        }
    }

    const uint32_t now = SDL_GetTicks();
    if (sleepDeadline != 0 && now >= sleepDeadline) {
        end(EndReason::SleepTimer, true);
        return;
    }

    if (state == PLAYER_STATE_PLAYING && now - lastSaveTime >= kSaveIntervalMs) {
        saveNow();
        lastSaveTime = now;
    }
}

}  // namespace audiobook

// ---------------------------------------------------------------------------
// C side
// ---------------------------------------------------------------------------

bool AudiobookSession_isActive(void) {
    return audiobook::Session::getInstance().isOpen();
}

void AudiobookSession_tick(void) {
    audiobook::Session::getInstance().tick();
}

void AudiobookSession_stop(void) {
    audiobook::Session::getInstance().close();
}

void AudiobookSession_quit(void) {
    audiobook::Session::getInstance().close();
    audiobook::Library::getInstance().quit();
}
