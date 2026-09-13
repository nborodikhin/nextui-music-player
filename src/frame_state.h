#ifndef __FRAME_STATE_H__
#define __FRAME_STATE_H__

#include <stdbool.h>

// What one frame changed since the last presentation. The record is pure logic:
// the presenter in module_common.c reads it and calls the platform.

typedef enum {
    FRAME_CHANGED_NOTHING,  // synchronize, and present nothing
    FRAME_CHANGED_LAYERS,   // present the GPU layers over the surface that the display holds
    FRAME_CHANGED_SURFACE,  // upload the regular surface and present it with the GPU layers
} FrameChange;

typedef struct {
    bool surface;
    bool layers;
} FrameState;

void FrameState_markSurfaceDrawn(FrameState* state);
void FrameState_markLayerDrawn(FrameState* state);

// Returns what changed since the last take, and clears the record. A frame
// that does not present keeps its record for the frame that does.
FrameChange FrameState_take(FrameState* state);

#endif
