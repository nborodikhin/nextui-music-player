#include "test.h"
#include "audiobook_session.h"
#include "background.h"
#include "module_common.h"
#include "module_podcast.h"
#include "music_session.h"
#include "podcast.h"
#include "radio.h"

static int music_ticks = 0;
static int podcast_ticks = 0;
static int audiobook_ticks = 0;
static int audiobook_stops = 0;

void MusicSession_tick(void) { music_ticks++; }
void MusicSession_stop(void) {}
bool MusicSession_isActive(void) { return true; }
void PodcastModule_backgroundTick(void) { podcast_ticks++; }
void Podcast_stop(void) {}
void Podcast_flushProgress(void) {}
bool Podcast_isActive(void) { return true; }
void AudiobookSession_tick(void) { audiobook_ticks++; }
void AudiobookSession_stop(void) { audiobook_stops++; }
bool AudiobookSession_isActive(void) { return true; }
void Radio_stop(void) {}
bool Radio_isActive(void) { return true; }
void ModuleCommon_setAutosleepDisabled(bool disabled) { (void)disabled; }

TEST(frame_tick_ticks_the_music_one_time) {
    music_ticks = 0;
    Background_setActive(BG_MUSIC);
    Background_frameTick(false);
    CHECK_EQ_INT(music_ticks, 1);
}

TEST(frame_tick_does_nothing_while_a_quit_is_pending) {
    music_ticks = 0;
    podcast_ticks = 0;
    Background_setActive(BG_MUSIC);
    Background_frameTick(true);
    Background_setActive(BG_PODCAST);
    Background_frameTick(true);
    CHECK_EQ_INT(music_ticks, 0);
    CHECK_EQ_INT(podcast_ticks, 0);
}

TEST(frame_tick_ticks_the_source_that_plays_only) {
    music_ticks = 0;
    podcast_ticks = 0;
    Background_setActive(BG_PODCAST);
    Background_frameTick(false);
    CHECK_EQ_INT(music_ticks, 0);
    CHECK_EQ_INT(podcast_ticks, 1);
    Background_setActive(BG_NONE);
    Background_frameTick(false);
    CHECK_EQ_INT(podcast_ticks, 1);
}

TEST(frame_tick_ticks_the_audiobook) {
    audiobook_ticks = 0;
    music_ticks = 0;
    Background_setActive(BG_AUDIOBOOK);
    Background_frameTick(false);
    CHECK_EQ_INT(audiobook_ticks, 1);
    CHECK_EQ_INT(music_ticks, 0);
}

TEST(stop_all_stops_the_audiobook) {
    audiobook_stops = 0;
    Background_setActive(BG_AUDIOBOOK);
    CHECK(Background_isPlaying());
    Background_stopAll();
    CHECK_EQ_INT(audiobook_stops, 1);
    CHECK_EQ_INT(Background_getActive(), BG_NONE);
}

int main(void) {
    RUN(frame_tick_ticks_the_music_one_time);
    RUN(frame_tick_does_nothing_while_a_quit_is_pending);
    RUN(frame_tick_ticks_the_source_that_plays_only);
    RUN(frame_tick_ticks_the_audiobook);
    RUN(stop_all_stops_the_audiobook);
    return test_summary();
}
