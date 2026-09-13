#include "defines.h"
#include "api.h"
#include "ui_layers.h"
#include "module_common.h"

void UiLayer_clear(UiLayer layer) {
    PLAT_clearLayers(layer);
    ModuleCommon_markLayerDrawn();
}

void UiLayer_blit(SDL_Surface* surface, int x, int y, UiLayer layer) {
    if (!surface) return;
    PLAT_drawOnLayer(surface, x, y, surface->w, surface->h, 1.0f, false, layer);
    ModuleCommon_markLayerDrawn();
}
