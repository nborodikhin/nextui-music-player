#ifndef __TOAST_H__
#define __TOAST_H__

#include <stdbool.h>

#include "api.h"

#include "display_helper.h"
#include "toast_types.h"

// Toast is a transient message shown at the bottom of the screen, rendered as
// a GPU layer of its own above all other content. At most one toast exists
// at a time; showing a new one replaces whatever was up.
//
// The toast is not part of the screen surface, so it neither needs nor causes a
// module redraw: it is painted once when shown and cleared once when it ends.

// ============================================
// Toast API
// ============================================

// Show `msg` for `duration_ms`, replacing any current toast.
// Returns a token that uniquely identifies this toast.
ToastToken Toast_show(const char* msg, uint32_t duration_ms);

// Same as Toast_show(), but the toast also ends when the screen changes.
ToastToken Toast_showScreenBound(const char* msg, uint32_t duration_ms);

// True while `token` is the current toast and its duration has not elapsed.
bool Toast_isShowing(ToastToken token);

// End the toast `token` refers to; does nothing if it is not the current one.
void Toast_dismiss(ToastToken token);

// ============================================
// Toast system API
// ============================================

void Toast_init(DisplayContext* display);
void Toast_quit(void);

// Should be called on every tick, used for managing toast durations.
void Toast_tick(void);

// Should be called when screen changes, used for managing screen-bound toasts.
void Toast_screenChanged(void);

#endif
