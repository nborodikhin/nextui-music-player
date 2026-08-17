#ifndef __TOAST_STATE_H__
#define __TOAST_STATE_H__

#include <stdbool.h>
#include <stdint.h>

// System-agnostic part of toast state management (state only, no graphics).
// See toast.h for the public API.

#define TOAST_MESSAGE_MAX 128

typedef uint32_t ToastToken;
#define TOAST_DURATION_FOREVER UINT32_MAX
#define TOAST_TOKEN_NONE ((ToastToken)0)

typedef struct {
    ToastToken current;       // TOAST_TOKEN_NONE when nothing is up
    ToastToken next_token;    // 0 issues 1 first; never hands out TOAST_TOKEN_NONE
    char       message[TOAST_MESSAGE_MAX];
    uint32_t   shown_at;
    uint32_t   duration;
    bool       screen_bound;  // also ends on ToastState_screenChanged()
} ToastState;

void ToastState_reset(ToastState* state);

ToastToken ToastState_show(
    ToastState* state,
    const char* msg,
    uint32_t duration_ms,
    bool screen_bound,
    uint32_t now
);
bool ToastState_isShowing(const ToastState* state, ToastToken token, uint32_t now);
bool ToastState_dismiss(ToastState* state, ToastToken token, uint32_t now);

bool ToastState_tick(ToastState* state, uint32_t now);
bool ToastState_screenChanged(ToastState* state);

#endif
