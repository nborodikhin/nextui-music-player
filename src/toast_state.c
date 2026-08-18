#include <stdio.h>
#include <string.h>

#include "toast_state.h"

void ToastState_reset(ToastState* state) {
    state->current    = TOAST_TOKEN_NONE;
    state->message[0] = '\0';
    state->shown_at   = 0;
    state->duration   = 0;
}

static bool expired(const ToastState* state, uint32_t now) {
    if (state->duration >= TOAST_DURATION_FOREVER) return false;
    return now - state->shown_at >= state->duration;
}

ToastToken ToastState_show(ToastState* state, const char* msg, uint32_t duration_ms,
                           bool screen_bound, uint32_t now) {
    if (!msg || msg[0] == '\0' || duration_ms == 0) return TOAST_TOKEN_NONE;

    snprintf(state->message, sizeof(state->message), "%s", msg);
    state->shown_at     = now;
    state->duration     = duration_ms;
    state->screen_bound = screen_bound;

    state->current = ++state->next_token;
    if (state->current == TOAST_TOKEN_NONE) state->current = ++state->next_token;

    return state->current;
}

bool ToastState_isShowing(const ToastState* state, ToastToken token, uint32_t now) {
    return token != TOAST_TOKEN_NONE && token == state->current && !expired(state, now);
}

bool ToastState_dismiss(ToastState* state, ToastToken token, uint32_t now) {
    if (!ToastState_isShowing(state, token, now)) return false;
    ToastState_reset(state);
    return true;
}

bool ToastState_tick(ToastState* state, uint32_t now) {
    if (state->current == TOAST_TOKEN_NONE || !expired(state, now)) return false;
    ToastState_reset(state);
    return true;
}

bool ToastState_screenChanged(ToastState* state) {
    if (state->current == TOAST_TOKEN_NONE || !state->screen_bound) return false;
    ToastState_reset(state);
    return true;
}
