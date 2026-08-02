#ifndef __MODULE_VIDEO_H__
#define __MODULE_VIDEO_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "module_common.h"

// Initialize video module
void VideoModule_init(void);

// Run the video module (video file browser and player)
ModuleExitReason VideoModule_run(SDL_Surface* screen);

// Check if a file extension is a supported video format
bool VideoModule_isVideoFile(const char* filename);

// Set toast message for video module
void VideoModule_setToast(const char* message);

#endif // __MODULE_VIDEO_H__
