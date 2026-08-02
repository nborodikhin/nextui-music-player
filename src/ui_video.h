#ifndef __UI_VIDEO_H__
#define __UI_VIDEO_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "browser.h"

// Render the video file browser screen
void render_video_browser(SDL_Surface* screen, int show_setting, BrowserContext* browser);

// Render video on-screen display (OSD) overlay
void render_video_osd(SDL_Surface* screen, const char* title, int current_seconds, int total_seconds,
                      bool is_paused, bool is_locked, int show_setting, int seek_offset,
                      bool show_hud);

// Render lock screen overlay for video player
void render_video_lockscreen(SDL_Surface* screen);

#endif // __UI_VIDEO_H__
