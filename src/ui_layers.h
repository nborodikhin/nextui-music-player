#ifndef __UI_LAYERS_H__
#define __UI_LAYERS_H__

#include <SDL2/SDL.h>

// The GPU layers of the app. The platform composes layers 1 and 2 under the
// regular surface, and layers 3 to 5 over it. Each layer has one purpose on
// every screen, thus a clear of a layer takes each element of that purpose and
// nothing else. A screen does not give a layer a second name of its own.
typedef enum {
    UI_LAYER_BACKGROUND = 1,  // the page background of the platform; the app does not draw it
    UI_LAYER_SELECTION  = 2,  // kept for the selection pill; clear until that work lands
    UI_LAYER_STATUS     = 3,  // slow status: time, a progress fill, the radio state and bitrate
    UI_LAYER_ANIMATION  = 4,  // fast animation: the spectrum, each marquee, the radio buffer fill
    UI_LAYER_TOAST      = 5,  // a toast
} UiLayer;

// Clear `layer` and record the change for the presenter of the frame.
void UiLayer_clear(UiLayer layer);

// Draw `surface` on `layer` at `x`, `y` and record the change for the presenter
// of the frame. The surface keeps its size.
void UiLayer_blit(SDL_Surface* surface, int x, int y, UiLayer layer);

#endif
