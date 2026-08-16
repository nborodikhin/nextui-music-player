#ifndef __TOAST_H__
#define __TOAST_H__

#include <stdbool.h>
#include <stdint.h>

#include "api.h"

// Transient message shown at the bottom of the screen, on a GPU layer of its
// own above all other content. At most one toast exists at a time; showing a
// new one replaces whatever was up.
//
// The toast is not part of the screen surface, so it neither needs nor causes a
// module redraw: it is painted once when shown and cleared once when it ends.

// Default lifetime for toasts with nothing better to ask for.
#define TOAST_DURATION 3000

// Identifies one shown toast. Handed out by Toast_show*(), passed back to ask
// about or dismiss that specific toast.
typedef uint32_t ToastToken;

// Zero is never a token assigned to a toast
#define TOAST_TOKEN_NONE 0u

// ============================================
// Toast API methods
// ============================================

// Show `msg` for `duration_ms`, replacing any current toast.
// Returns a token that uniquely identifies this toast.
ToastToken Toast_show(const char* msg, uint32_t duration_ms);

// As Toast_show(), but the toast also ends when the screen changes, so it can
// never bleed into whatever the user opens next.
ToastToken Toast_showScreenBound(const char* msg, uint32_t duration_ms);

// True while `token` is the current toast and its duration has not elapsed.
bool Toast_isShowing(ToastToken token);

// End the toast `token` refers to; does nothing if it is not the current one.
void Toast_dismiss(ToastToken token);

// ============================================
// Toast system API methods
// ============================================

// Should be called on every tick, used for managing toast durations.
void Toast_tick(void);

// The surface toasts are positioned against.
void Toast_setSurface(SDL_Surface* screen);

// Should be called when screen changes, used for managing screen-bound toasts.
void Toast_screenChanged(void);

// Repaint the current toast, for when something else has dropped GPU layers.
void Toast_redraw(void);

#endif
