#ifndef __AUDIOBOOK_SESSION_HPP__
#define __AUDIOBOOK_SESSION_HPP__

#include <cstdint>
#include <string>
#include <vector>

#include "audiobook/library.hpp"
#include "audiobook/m4b_chapters.hpp"

namespace audiobook {

/**
 * The playback of one book: its chapters, the step to the next chapter, the saved place and the
 * sleep timer. It has no screen. Each screen reads its state and commands it, and the background
 * tick runs it on each frame, whatever screen shows.
 *
 * The session holds the background slot BG_AUDIOBOOK from open() to its close or its end, as the
 * music session holds BG_MUSIC.
 */
class Session {
public:
    /** Why the last session closed by itself. */
    enum class EndReason : uint8_t {
        None,
        /** The last chapter ended */
        Finished,
        /** The sleep timer ran out */
        SleepTimer,
    };

    static Session& getInstance();

    /**
     * Stops what plays, and plays the book of `key` from its saved place. A seek to that place
     * can take some frames, see isSeeking().
     * Returns: false when the book has no chapter or its file does not load
     */
    bool open(const std::string& key);

    /** Saves the place, and stops. */
    void close();

    /** Returns true from open() to the close or the end of the session. */
    bool isOpen() const { return !key.empty(); }

    /** Returns true while the seek to the saved place runs, before the sound starts. */
    bool isSeeking() const { return pendingPlay; }
    bool isPaused() const;

    const std::string& getKey() const { return key; }

    /** Returns the book of the session, or nullptr. */
    const Book* getBook() const;
    const std::vector<Chapter>& getChapters() const { return chapters; }
    int getChapterIndex() const { return chapterIndex; }
    const Chapter* getChapter() const;

    int getChapterPositionMs() const;
    int getChapterDurationMs() const;
    /** Returns the position from the start of the whole book. */
    int getBookPositionMs() const;

    void togglePause();

    /** Moves by `deltaMs`, inside the current chapter. */
    void skip(int deltaMs);

    /** Plays the chapter at `index` from its start. Returns false when it does not load. */
    bool playChapter(int index);

    /** Moves the sleep timer to its next option, and returns the toast that names it. */
    std::string cycleSleepTimer();

    /** Returns the time that the sleep timer leaves, "end of chapter", or "" with no timer. */
    std::string getSleepLabel() const;

    /** Starts the sound after a seek, moves to the next chapter, ends the book, runs the timer and saves the place. */
    void tick();

    /** Writes the place to the card. */
    void saveNow();

    /** Returns the reason of the last end of a session by itself, and forgets it. */
    EndReason takeEndReason();

private:
    Session() = default;

    /** Loads the chapter at `index` and seeks `positionMs` into it. Returns false on a failure. */
    bool startChapter(int index, int positionMs);

    /** Stops the sound and leaves the session. Pass true in `save` to save the place of now first. */
    void stop(bool save);

    /** Stops as stop() does, and records `reason` for the screen. */
    void end(EndReason reason, bool save);

    void resetSleepTimer();

    std::string key;
    std::vector<Chapter> chapters;
    int chapterIndex = 0;
    /** The sound starts when the seek to the saved place ends */
    bool pendingPlay = false;
    /** The player played since the last load. Before that, STOPPED means "not started". */
    bool playbackSeen = false;
    uint32_t lastSaveTime = 0;
    int sleepOption       = 0;
    /** SDL ticks where the timer runs out, 0 with no timer */
    uint32_t sleepDeadline  = 0;
    bool sleepAtChapterEnd  = false;
    EndReason endReason     = EndReason::None;
};

}  // namespace audiobook

#endif
