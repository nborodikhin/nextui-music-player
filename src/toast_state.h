#ifndef __TOAST_STATE_H__
#define __TOAST_STATE_H__

#include <stdbool.h>
#include <stdint.h>

// Which toast is up and until when: the half of the toast system that has no
// screen to draw on. The clock is passed in, so this holds no hidden state and
// can be exercised on the host. See toast.h for the API modules actually call.

#define TOAST_MESSAGE_MAX 128

// Identifies one shown toast. Handed out by ToastState_show(), passed back to
// ask about or dismiss that specific toast.
typedef uint32_t ToastToken;

// Zero is never a token assigned to a toast
#define TOAST_TOKEN_NONE 0u

// Zero-initialized (or reset) means no toast is up: `current` is
// TOAST_TOKEN_NONE and `message` is empty.
typedef struct {
    ToastToken current;       // TOAST_TOKEN_NONE when nothing is up
    ToastToken next_token;    // 0 issues 1 first; never hands out TOAST_TOKEN_NONE
    char       message[TOAST_MESSAGE_MAX];
    uint32_t   shown_at;
    uint32_t   duration;
    bool       screen_bound;  // also ends on ToastState_screenChanged()
} ToastState;

// Drop any current toast without disturbing token issuing, so a token handed
// out before the reset can never read as showing after it.
void ToastState_reset(ToastState* state);

// Replace the current toast with `msg` for `duration_ms`, and return its token.
// Returns TOAST_TOKEN_NONE without touching the state for an empty message or a
// zero duration; the caller paints iff a real token comes back.
ToastToken ToastState_show(ToastState* state, const char* msg, uint32_t duration_ms,
                           bool screen_bound, uint32_t now);

// True while `token` is the current toast and its duration has not elapsed.
bool ToastState_isShowing(const ToastState* state, ToastToken token, uint32_t now);

// The three ways a toast ends. Each returns true when it did end, meaning the
// caller has a toast to clear off the screen.
bool ToastState_dismiss(ToastState* state, ToastToken token, uint32_t now);
bool ToastState_tick(ToastState* state, uint32_t now);
bool ToastState_screenChanged(ToastState* state);

#endif
