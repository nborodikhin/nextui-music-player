#ifndef __BACKGROUND_H__
#define __BACKGROUND_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Background player types
typedef enum {
    BG_NONE = 0,
    BG_MUSIC,
    BG_RADIO,
    BG_PODCAST
} BackgroundPlayerType;

// Set the active background player
void Background_setActive(BackgroundPlayerType type);

// Get the active background player type
BackgroundPlayerType Background_getActive(void);

// Stop whatever is playing in the background
void Background_stopAll(void);

// Check if any background player is active
bool Background_isPlaying(void);

// Starts the next track when a track ends, and saves the position at an interval, for the source
// that plays. Draws nothing and reads no input.
void Background_tick(void);

// Ticks the background playback one time for a frame. Pass true in `quit_requested` while a quit
// is pending, thus no track starts and no position is saved.
void Background_frameTick(bool quit_requested);

#ifdef __cplusplus
}
#endif

#endif
