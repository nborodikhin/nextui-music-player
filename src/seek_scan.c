#include "seek_scan.h"

#include <string.h>

static SeekScan shared_scan;

static int clamp_position(int position_ms, int duration_ms) {
    if (position_ms < 0) return 0;
    if (duration_ms > 0 && position_ms > duration_ms) return duration_ms;
    return position_ms;
}

// One repeat of a held button: move the target by the step, then grow the step
static void scan_repeat(SeekScan* scan, int duration_ms) {
    // The step is at most a part of the time that is left in the direction of the scan
    int left_ms = scan->direction > 0 ? duration_ms - scan->target_ms : scan->target_ms;
    int max_step = (int)((int64_t)left_ms * SEEK_SCAN_MAX_STEP_PERCENT / 100);
    if (max_step < SEEK_SCAN_MAX_STEP_FLOOR_MS) max_step = SEEK_SCAN_MAX_STEP_FLOOR_MS;
    if (scan->step_ms > max_step) scan->step_ms = max_step;

    scan->target_ms = clamp_position(scan->target_ms + scan->step_ms * scan->direction, duration_ms);
    scan->step_ms += scan->step_ms >> SEEK_SCAN_ACCEL_SHIFT;
}

bool SeekScan_update(SeekScan* scan, uint32_t now, bool back_down, bool fwd_down,
                     int position_ms, int duration_ms, int back_step_ms, int fwd_step_ms,
                     int* seek_to_ms) {
    // Exactly one button of the two is down, else there is no press
    int direction = 0;
    if (back_down && !fwd_down) direction = -1;
    if (fwd_down && !back_down) direction = +1;

    // A release, or a change of direction, ends the press. A scan seeks to its target.
    if (scan->direction != 0 && direction != scan->direction) {
        bool seek = scan->scanning;
        int target = scan->target_ms;
        SeekScan_cancel(scan);
        if (seek) {
            *seek_to_ms = target;
            return true;
        }
    }

    if (direction == 0) return false;

    // A new press seeks at once by the step of the screen
    if (scan->direction == 0) {
        int step = direction > 0 ? fwd_step_ms : back_step_ms;
        scan->direction = direction;
        scan->scanning = false;
        scan->target_ms = clamp_position(position_ms + step * direction, duration_ms);
        scan->step_ms = SEEK_SCAN_MIN_STEP_MS;
        scan->interval_ms = SEEK_SCAN_REPEAT_START_MS;
        scan->armed = false;
        *seek_to_ms = scan->target_ms;
        return true;
    }

    // The repeat delay starts on the frame after the press, thus the time of the
    // seek of the press does not count
    if (!scan->armed) {
        scan->armed = true;
        scan->next_repeat_at = now + SEEK_SCAN_REPEAT_DELAY_MS;
        return false;
    }

    // A scan needs the length of the track, for the limit of the step
    if (duration_ms <= 0) return false;

    // A held button: a repeat that is due moves the target. As in RockBox, the
    // first repeat only starts the scan. A frame does one repeat at most, and a
    // late frame drops the repeats that it missed, as a slow frame would in RockBox.
    if ((int32_t)(now - scan->next_repeat_at) >= 0) {
        if (scan->scanning) {
            scan_repeat(scan, duration_ms);
        } else {
            scan->scanning = true;
        }
        scan->next_repeat_at += (uint32_t)scan->interval_ms;
        if ((int32_t)(now - scan->next_repeat_at) >= 0) {
            scan->next_repeat_at = now + (uint32_t)scan->interval_ms;
        }
        if (scan->interval_ms > SEEK_SCAN_REPEAT_FINISH_MS) {
            scan->interval_ms -= SEEK_SCAN_REPEAT_DECREMENT_MS;
        }
    }
    return false;
}

int SeekScan_displayPosition(const SeekScan* scan, int position_ms) {
    return scan->scanning ? scan->target_ms : position_ms;
}

bool SeekScan_isScanning(const SeekScan* scan) {
    return scan->scanning;
}

void SeekScan_cancel(SeekScan* scan) {
    memset(scan, 0, sizeof(*scan));
}

SeekScan* SeekScan_shared(void) {
    return &shared_scan;
}
