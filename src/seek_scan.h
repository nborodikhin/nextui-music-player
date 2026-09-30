#ifndef __SEEK_SCAN_H__
#define __SEEK_SCAN_H__

#include <stdbool.h>
#include <stdint.h>

// Progressive seek on LEFT and RIGHT, as RockBox does it (apps/gui/wps.c,
// ffwd_rew(), and the repeat of firmware/drivers/button.c).
//
// A press seeks at once by the step of the screen. A press that continues past
// the repeat delay starts a scan: the scan moves a target position on each
// repeat, and the step grows while the button stays down. The player does not
// seek during the scan; the screen shows the target. The release seeks once, to
// the target.
//
// The file holds no platform include: the clock and the buttons come in, and
// the seek comes out.

// RockBox: the first repeat after 300 ms, then a repeat each 160 ms that comes
// 10 ms sooner each time, down to 50 ms
#define SEEK_SCAN_REPEAT_DELAY_MS     300
#define SEEK_SCAN_REPEAT_START_MS     160
#define SEEK_SCAN_REPEAT_FINISH_MS     50
#define SEEK_SCAN_REPEAT_DECREMENT_MS  10

// RockBox defaults: a step of 1 s ("scan min step"), that grows by 1/32 on each
// repeat ("seek acceleration" normal), and that is at most 3% of the time that
// is left in the direction of the scan, but never less than 500 ms
#define SEEK_SCAN_MIN_STEP_MS        1000
#define SEEK_SCAN_ACCEL_SHIFT           5
#define SEEK_SCAN_MAX_STEP_PERCENT      3
#define SEEK_SCAN_MAX_STEP_FLOOR_MS   500

typedef struct {
    int direction;       // -1 back, +1 forward, 0 no button down
    bool scanning;       // the repeat delay is over and the target moves
    int target_ms;       // the position that the release seeks to
    int step_ms;         // the next move of the target
    int interval_ms;     // the time to the repeat after the next one
    bool armed;          // next_repeat_at is set: the frame after the press came
    uint32_t next_repeat_at;
} SeekScan;

// Feed one frame. `back_down` and `fwd_down` tell which buttons are down now.
// `back_step_ms` and `fwd_step_ms` are the steps of a short press. Returns true
// where the player must seek now, and puts the position into *seek_to_ms.
bool SeekScan_update(SeekScan* scan, uint32_t now, bool back_down, bool fwd_down,
                     int position_ms, int duration_ms, int back_step_ms, int fwd_step_ms,
                     int* seek_to_ms);

// The position that the screen shows: the target during a scan, else `position_ms`
int SeekScan_displayPosition(const SeekScan* scan, int position_ms);

// True during a scan. The screen draws each frame while the target moves.
bool SeekScan_isScanning(const SeekScan* scan);

// Stop a scan with no seek, for example when the track changes
void SeekScan_cancel(SeekScan* scan);

// The one scan of the app. A playing screen feeds it, and the screen draws from it.
SeekScan* SeekScan_shared(void);

#endif
