// Tests for src/toast_state.c - which toast is up and until when.
//
// Pins the token semantics (identity, never handing out TOAST_TOKEN_NONE), the
// expiry boundary, and the two lifetimes: a plain toast survives a screen
// change, a screen-bound one does not.

#include <string.h>

#include "test.h"
#include "toast_state.h"

#define SHOW(state, msg, ms, now)       ToastState_show(&(state), (msg), (ms), false, (now))
#define SHOW_BOUND(state, msg, ms, now) ToastState_show(&(state), (msg), (ms), true,  (now))

// A zero-initialized state is a valid "nothing is up" state - modules rely on
// this so a ToastToken field needs no explicit reset.
TEST(zero_state_shows_nothing) {
    ToastState s = {0};
    CHECK(!ToastState_isShowing(&s, TOAST_TOKEN_NONE, 0));
    CHECK(!ToastState_isShowing(&s, 1, 0));
    CHECK(!ToastState_tick(&s, 0));
    CHECK(!ToastState_screenChanged(&s));
}

TEST(show_returns_a_live_token) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Saved", 3000, 1000);
    CHECK(t != TOAST_TOKEN_NONE);
    CHECK(ToastState_isShowing(&s, t, 1000));
    CHECK_EQ_INT(strcmp(s.message, "Saved"), 0);
}

// Nothing to show and nothing to time: rejected, and the state is left alone.
TEST(empty_message_or_zero_duration_is_rejected) {
    ToastState s = {0};
    ToastToken live = SHOW(s, "Saved", 3000, 0);

    CHECK_EQ_INT(SHOW(s, "", 3000, 0), TOAST_TOKEN_NONE);
    CHECK_EQ_INT(SHOW(s, NULL, 3000, 0), TOAST_TOKEN_NONE);
    CHECK_EQ_INT(SHOW(s, "Saved", 0, 0), TOAST_TOKEN_NONE);

    // The rejected calls did not disturb the toast that was already up.
    CHECK(ToastState_isShowing(&s, live, 0));
}

TEST(tokens_are_unique_and_never_none) {
    ToastState s = {0};
    ToastToken a = SHOW(s, "one", 3000, 0);
    ToastToken b = SHOW(s, "two", 3000, 0);
    ToastToken c = SHOW(s, "three", 3000, 0);
    CHECK(a != b && b != c && a != c);
    CHECK(a != TOAST_TOKEN_NONE && b != TOAST_TOKEN_NONE && c != TOAST_TOKEN_NONE);
}

// The counter is uint32 and issues one token per toast, but wrapping must not
// hand out TOAST_TOKEN_NONE - a caller holding it would read as "not showing".
TEST(token_wraparound_skips_none) {
    ToastState s = {0};
    s.next_token = UINT32_MAX;
    ToastToken t = SHOW(s, "wrapped", 3000, 0);
    CHECK_EQ_INT(t != TOAST_TOKEN_NONE, 1);
    CHECK(ToastState_isShowing(&s, t, 0));
}

// TOAST_TOKEN_NONE never reads as showing, even when it happens to equal the
// state's own current field.
TEST(none_token_never_shows) {
    ToastState s = {0};
    CHECK(!ToastState_isShowing(&s, TOAST_TOKEN_NONE, 0));
    SHOW(s, "Saved", 3000, 0);
    CHECK(!ToastState_isShowing(&s, TOAST_TOKEN_NONE, 0));
}

// Showing a second toast replaces the first, so the first token goes stale
// immediately - this is what disarms the menu's exit prompt when something
// else toasts over it.
TEST(a_new_toast_makes_the_previous_token_stale) {
    ToastState s = {0};
    ToastToken first = SHOW(s, "first", 3000, 0);
    ToastToken second = SHOW(s, "second", 3000, 100);

    CHECK(!ToastState_isShowing(&s, first, 100));
    CHECK(ToastState_isShowing(&s, second, 100));
    CHECK_EQ_INT(strcmp(s.message, "second"), 0);
}

// Replacing restarts the clock rather than inheriting the old deadline.
TEST(replacing_restarts_the_duration) {
    ToastState s = {0};
    SHOW(s, "first", 3000, 0);
    ToastToken second = SHOW(s, "second", 3000, 2900);

    CHECK(ToastState_isShowing(&s, second, 3000));   // past the first deadline
    CHECK(!ToastState_tick(&s, 3000));
    CHECK(ToastState_isShowing(&s, second, 5899));
    CHECK(!ToastState_isShowing(&s, second, 5900));  // 2900 + 3000
}

// Visible up to the deadline, gone at it.
TEST(expiry_boundary_is_exclusive) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Saved", 3000, 1000);
    CHECK(ToastState_isShowing(&s, t, 1000));
    CHECK(ToastState_isShowing(&s, t, 3999));
    CHECK(!ToastState_isShowing(&s, t, 4000));
    CHECK(!ToastState_isShowing(&s, t, 4001));
}

// Tick reports the end exactly once, so the screen is cleared once.
TEST(tick_ends_the_toast_once) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Saved", 3000, 0);

    CHECK(!ToastState_tick(&s, 2999));
    CHECK(ToastState_isShowing(&s, t, 2999));

    CHECK(ToastState_tick(&s, 3000));
    CHECK(!ToastState_tick(&s, 3000));
    CHECK(!ToastState_tick(&s, 9999));
    CHECK(!ToastState_isShowing(&s, t, 3000));
    CHECK_EQ_INT(s.message[0], '\0');
}

TEST(forever_toast_does_not_expire) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Working...", TOAST_DURATION_FOREVER, 1000);

    CHECK(ToastState_isShowing(&s, t, UINT32_MAX));
    CHECK(!ToastState_tick(&s, UINT32_MAX));
    CHECK(ToastState_isShowing(&s, t, 999));
    CHECK(ToastState_dismiss(&s, t, 999));
}

TEST(screen_change_ends_a_bound_forever_toast) {
    ToastState s = {0};
    ToastToken t = SHOW_BOUND(s, "Working...", TOAST_DURATION_FOREVER, 0);

    CHECK(ToastState_isShowing(&s, t, UINT32_MAX));
    CHECK(ToastState_screenChanged(&s));
    CHECK(!ToastState_isShowing(&s, t, UINT32_MAX));
}

TEST(dismiss_ends_the_toast_once) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Saved", 3000, 0);

    CHECK(ToastState_dismiss(&s, t, 100));
    CHECK(!ToastState_dismiss(&s, t, 100));
    CHECK(!ToastState_isShowing(&s, t, 100));
}

// A screen may only take down its own toast, so a stale token cannot clear the
// message some other screen has since put up.
TEST(dismiss_with_a_stale_token_is_a_no_op) {
    ToastState s = {0};
    ToastToken first = SHOW(s, "first", 3000, 0);
    ToastToken second = SHOW(s, "second", 3000, 0);

    CHECK(!ToastState_dismiss(&s, first, 0));
    CHECK(!ToastState_dismiss(&s, TOAST_TOKEN_NONE, 0));
    CHECK(ToastState_isShowing(&s, second, 0));
}

// Dismissing an already-expired toast reports nothing to clear: tick got there
// first, and a second clear would be a redundant screen write.
TEST(dismiss_after_expiry_reports_nothing_to_clear) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Saved", 3000, 0);
    CHECK(!ToastState_dismiss(&s, t, 3000));
}

// The plain lifetime: a message set just before returning to the menu has to
// finish its time there.
TEST(a_plain_toast_survives_a_screen_change) {
    ToastState s = {0};
    ToastToken t = SHOW(s, "Playlist created", 3000, 0);

    CHECK(!ToastState_screenChanged(&s));
    CHECK(ToastState_isShowing(&s, t, 100));
}

// The bound lifetime: the menu's exit prompt must not follow the user into
// whatever they opened.
TEST(a_screen_bound_toast_ends_on_a_screen_change) {
    ToastState s = {0};
    ToastToken t = SHOW_BOUND(s, "Press B again to exit", 3000, 0);

    CHECK(ToastState_isShowing(&s, t, 100));
    CHECK(ToastState_screenChanged(&s));
    CHECK(!ToastState_isShowing(&s, t, 100));
    CHECK_EQ_INT(s.message[0], '\0');
}

TEST(screen_change_ends_the_bound_toast_once) {
    ToastState s = {0};
    SHOW_BOUND(s, "Press B again to exit", 3000, 0);
    CHECK(ToastState_screenChanged(&s));
    CHECK(!ToastState_screenChanged(&s));
}

// Boundness belongs to the toast that is up, not to the state: a plain toast
// shown over a bound one is not itself bound.
TEST(boundness_follows_the_current_toast) {
    ToastState s = {0};
    SHOW_BOUND(s, "bound", 3000, 0);
    ToastToken plain = SHOW(s, "plain", 3000, 0);

    CHECK(!ToastState_screenChanged(&s));
    CHECK(ToastState_isShowing(&s, plain, 0));

    ToastToken bound = SHOW_BOUND(s, "bound again", 3000, 0);
    CHECK(ToastState_screenChanged(&s));
    CHECK(!ToastState_isShowing(&s, bound, 0));
}

// A message longer than the buffer is truncated, not a write past the end.
TEST(long_message_is_truncated) {
    ToastState s = {0};
    char msg[TOAST_MESSAGE_MAX * 2];
    memset(msg, 'x', sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';

    ToastToken t = SHOW(s, msg, 3000, 0);
    CHECK(ToastState_isShowing(&s, t, 0));
    CHECK_EQ_SZ(strlen(s.message), TOAST_MESSAGE_MAX - 1);
}

// SDL_GetTicks() wraps after ~49 days of uptime; unsigned arithmetic has to
// carry the toast across the wrap rather than expiring it instantly.
TEST(clock_wraparound_does_not_expire_early) {
    ToastState s = {0};
    uint32_t before_wrap = UINT32_MAX - 1000;
    ToastToken t = SHOW(s, "Saved", 3000, before_wrap);

    CHECK(ToastState_isShowing(&s, t, before_wrap + 500));   // still before the wrap
    CHECK(ToastState_isShowing(&s, t, (uint32_t)(before_wrap + 2999)));  // past it
    CHECK(!ToastState_isShowing(&s, t, (uint32_t)(before_wrap + 3000)));
}

// Reset drops the toast but keeps issuing fresh tokens, so a token from before
// the reset can never come back to life.
TEST(reset_does_not_recycle_tokens) {
    ToastState s = {0};
    ToastToken before = SHOW(s, "before", 3000, 0);
    ToastState_reset(&s);

    CHECK(!ToastState_isShowing(&s, before, 0));
    ToastToken after = SHOW(s, "after", 3000, 0);
    CHECK(after != before);
    CHECK(!ToastState_isShowing(&s, before, 0));
}

int main(void) {
    printf("test_toast_state\n");
    RUN(zero_state_shows_nothing);
    RUN(show_returns_a_live_token);
    RUN(empty_message_or_zero_duration_is_rejected);
    RUN(tokens_are_unique_and_never_none);
    RUN(token_wraparound_skips_none);
    RUN(none_token_never_shows);
    RUN(a_new_toast_makes_the_previous_token_stale);
    RUN(replacing_restarts_the_duration);
    RUN(expiry_boundary_is_exclusive);
    RUN(tick_ends_the_toast_once);
    RUN(forever_toast_does_not_expire);
    RUN(screen_change_ends_a_bound_forever_toast);
    RUN(dismiss_ends_the_toast_once);
    RUN(dismiss_with_a_stale_token_is_a_no_op);
    RUN(dismiss_after_expiry_reports_nothing_to_clear);
    RUN(a_plain_toast_survives_a_screen_change);
    RUN(a_screen_bound_toast_ends_on_a_screen_change);
    RUN(screen_change_ends_the_bound_toast_once);
    RUN(boundness_follows_the_current_toast);
    RUN(long_message_is_truncated);
    RUN(clock_wraparound_does_not_expire_early);
    RUN(reset_does_not_recycle_tokens);
    return test_summary();
}
