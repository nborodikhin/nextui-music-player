// The record of what a frame changed, and the presentation that it selects.

#include "test.h"
#include "frame_state.h"

TEST(test_nothing_changed_synchronizes) {
    FrameState s = {0};
    CHECK_EQ_INT(FrameState_take(&s), FRAME_CHANGED_NOTHING);
}

TEST(test_a_layer_change_presents_the_layers) {
    FrameState s = {0};
    FrameState_markLayerDrawn(&s);
    CHECK_EQ_INT(FrameState_take(&s), FRAME_CHANGED_LAYERS);
}

TEST(test_a_surface_change_presents_the_surface) {
    FrameState s = {0};
    FrameState_markSurfaceDrawn(&s);
    CHECK_EQ_INT(FrameState_take(&s), FRAME_CHANGED_SURFACE);
}

TEST(test_a_surface_change_includes_the_layers) {
    FrameState s = {0};
    FrameState_markLayerDrawn(&s);
    FrameState_markSurfaceDrawn(&s);
    CHECK_EQ_INT(FrameState_take(&s), FRAME_CHANGED_SURFACE);
}

TEST(test_a_take_clears_the_record) {
    FrameState s = {0};
    FrameState_markSurfaceDrawn(&s);
    FrameState_markLayerDrawn(&s);
    FrameState_take(&s);
    CHECK_EQ_INT(FrameState_take(&s), FRAME_CHANGED_NOTHING);
}

TEST(test_a_frame_that_does_not_present_keeps_the_record) {
    // A path that leaves the loop early takes nothing, thus the change waits
    // for the frame that presents.
    FrameState s = {0};
    FrameState_markLayerDrawn(&s);
    CHECK(s.layers);
    CHECK_EQ_INT(FrameState_take(&s), FRAME_CHANGED_LAYERS);
}

int main(void) {
    RUN(test_nothing_changed_synchronizes);
    RUN(test_a_layer_change_presents_the_layers);
    RUN(test_a_surface_change_presents_the_surface);
    RUN(test_a_surface_change_includes_the_layers);
    RUN(test_a_take_clears_the_record);
    RUN(test_a_frame_that_does_not_present_keeps_the_record);
    return test_summary();
}
