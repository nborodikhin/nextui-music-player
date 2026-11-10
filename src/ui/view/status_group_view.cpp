#include "ui/view/status_group_view.hpp"

#include <ctime>

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
#include <msettings.h>
}

using input::HardwareBar;
using input::InputState;

// Reads the cached values only: a query of the device takes too long for each frame
static StatusValues readPlatformValues() {
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    return StatusValues{
        .hdmi            = GetHDMI(),
        .connection      = static_cast<int>(PLAT_connectionStrength()),
        .bluetooth       = PLAT_btIsConnected(),
        .audioSink       = GetAudioSink(),
        .showClock       = CFG_getShowClock(),
        .clock24h        = CFG_getClock24H(),
        .minuteOfDay     = local.tm_hour * 60 + local.tm_min,
        .charging        = PWR_isCharging() != 0,
        .charge          = PWR_getBattery(),
        .showBatteryText = CFG_getShowBatteryPercent(),
    };
}

StatusGroupView::StatusGroupView(DrawGroup drawGroup, ReadValues readValues)
    : drawGroup(drawGroup ? drawGroup : GFX_blitHardwareGroup),
      readValues(readValues ? readValues : readPlatformValues) {}

void StatusGroupView::layout(UiLayoutContext& context) {
    InputState& state         = InputState::getInstance();
    const uint32_t now        = state.getTime();
    const HardwareBar bar     = state.getHardwareBar();
    const StatusValues values = readValues();
    if (values != pendingValues) {
        pendingValues = values;
        pendingSince  = now;
    }
    const bool valuesChanged = values != measuredValues || renderUncertain;
    const bool valuesSettled = valuesChanged && now - pendingSince >= kSettleMs;
    if (needsRender || bar != measuredBar || valuesSettled || state.isStatusChanged()) {
        SDL_Surface* scratch = clearScratch();
        int width            = 0;
        // The platform draws the status group only where the screen has the room for it
        if (scratch && scratch->w >= SCALE1(320)) {
            width = drawGroup(scratch, static_cast<int>(bar));
        }
        const int x = scratch ? scratch->w - SCALE1(PADDING) - width : 0;
        finishRender(SDL_Rect{x, SCALE1(PADDING), width, SCALE1(PILL_SIZE)});
        needsRender    = false;
        measuredBar    = bar;
        measuredValues = values;
        state.clearStatusChanged();
        // The platform reads the values again while it draws. A value that changed in the
        // meantime renders the group again when the values settle, also when they come back.
        const StatusValues drawnValues = readValues();
        renderUncertain                = drawnValues != values;
        if (renderUncertain) {
            pendingValues = drawnValues;
            pendingSince  = now;
        }
    }
    const SDL_Rect& rect = getScratchRect();
    context.setSize(UiSize{rect.w, rect.h});
}
