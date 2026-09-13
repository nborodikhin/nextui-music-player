#include "frame_state.h"

void FrameState_markSurfaceDrawn(FrameState* state) {
    state->surface = true;
}

void FrameState_markLayerDrawn(FrameState* state) {
    state->layers = true;
}

FrameChange FrameState_take(FrameState* state) {
    FrameChange change = FRAME_CHANGED_NOTHING;
    if (state->surface)     change = FRAME_CHANGED_SURFACE;
    else if (state->layers) change = FRAME_CHANGED_LAYERS;
    state->surface = false;
    state->layers  = false;
    return change;
}
