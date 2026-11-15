#ifndef __UI_PAGE_ACTIVITY_CLOCK_HPP__
#define __UI_PAGE_ACTIVITY_CLOCK_HPP__

#include <cstdint>

/**
 * Measures the active time of one page: the time between the frames in which the page is the
 * top. The first frame after a gap adds no time, thus the time of the gap does not count.
 */
class ActivityClock {
public:
    /** Call at the start of each frame in which the page is the top. */
    void onFrameStart(uint32_t frameTime) {
        if (clockIsActive) activeTime += frameTime - lastFrameTime;
        lastFrameTime = frameTime;
        clockIsActive = true;
    }

    /** The time from the last frame to the next frame does not count as active time. */
    void onActivityGap() { clockIsActive = false; }

    uint32_t getActiveTime() const { return activeTime; }

private:
    uint32_t activeTime    = 0;
    uint32_t lastFrameTime = 0;
    /** The page was active since the last frame */
    bool clockIsActive = false;
};

#endif
