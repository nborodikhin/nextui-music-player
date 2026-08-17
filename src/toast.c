#include <string.h>

#include "defines.h"
#include "api.h"
#include "toast.h"
#include "ui_fonts.h"

// Toast sits on the highest GPU layer, above scroll text and every other overlay.
#define LAYER_TOAST 5

static ToastState state;

static int target_w = 0;
static int target_h = 0;

// Paint the current message onto the toast layer.
static void draw(void) {
    if (target_w <= 0 || target_h <= 0 || state.current == TOAST_TOKEN_NONE) return;

    SDL_Surface* text = TTF_RenderUTF8_Blended(Fonts_getMedium(), state.message, COLOR_WHITE);
    if (!text) return;

    int border   = SCALE1(2);
    int toast_w  = text->w + SCALE1(PADDING * 3);
    int toast_h  = text->h + SCALE1(12);
    int toast_x  = (target_w - toast_w) / 2;
    int toast_y  = target_h - SCALE1(BUTTON_SIZE + BUTTON_MARGIN + PADDING * 3) - toast_h;

    // Total surface size including border
    int surface_w = toast_w + border * 2;
    int surface_h = toast_h + border * 2;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,
        surface_w, surface_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface) {
        // Disable blending so fills are opaque
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);

        // Light gray border (outer rect)
        SDL_FillRect(surface, NULL, SDL_MapRGBA(surface->format, 200, 200, 200, 255));

        // Dark grey background (inner rect)
        SDL_Rect bg_rect = {border, border, toast_w, toast_h};
        SDL_FillRect(surface, &bg_rect, SDL_MapRGBA(surface->format, 40, 40, 40, 255));

        // Text centered within the toast
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
        int text_x = border + (toast_w - text->w) / 2;
        int text_y = border + (toast_h - text->h) / 2;
        SDL_BlitSurface(text, NULL, surface, &(SDL_Rect){text_x, text_y});

        PLAT_clearLayers(LAYER_TOAST);
        PLAT_drawOnLayer(surface, toast_x - border, toast_y - border,
                         surface_w, surface_h, 1.0f, false, LAYER_TOAST);
        PLAT_GPU_Flip();

        SDL_FreeSurface(surface);
    }
    SDL_FreeSurface(text);
}

static void clear(void) {
    PLAT_clearLayers(LAYER_TOAST);
    PLAT_GPU_Flip();
}

static ToastToken show(const char* msg, uint32_t duration_ms, bool screen_bound) {
    ToastToken token = ToastState_show(&state, msg, duration_ms, screen_bound, SDL_GetTicks());
    if (token != TOAST_TOKEN_NONE) draw();
    return token;
}

ToastToken Toast_show(const char* msg, uint32_t duration_ms) {
    return show(msg, duration_ms, false);
}

ToastToken Toast_showScreenBound(const char* msg, uint32_t duration_ms) {
    return show(msg, duration_ms, true);
}

bool Toast_isShowing(ToastToken token) {
    return ToastState_isShowing(&state, token, SDL_GetTicks());
}

void Toast_dismiss(ToastToken token) {
    if (ToastState_dismiss(&state, token, SDL_GetTicks())) clear();
}

void Toast_tick(void) {
    if (ToastState_tick(&state, SDL_GetTicks())) clear();
}

void Toast_setSurface(SDL_Surface* screen) {
    if (!screen) return;
    target_w = screen->w;
    target_h = screen->h;
}

void Toast_screenChanged(void) {
    if (ToastState_screenChanged(&state)) clear();
}

void Toast_redraw(void) {
    if (Toast_isShowing(state.current)) draw();
}
