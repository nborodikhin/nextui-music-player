#ifndef __UI_VIEW_STATUS_GROUP_VIEW_HPP__
#define __UI_VIEW_STATUS_GROUP_VIEW_HPP__

#include "input/input_state.hpp"
#include "ui/view/platform_group_view.hpp"

/** The values that the status group shows. Equal values give equal pixels. */
struct StatusValues {
    int hdmi             = 0;
    int connection       = 0;
    bool bluetooth       = false;
    int audioSink        = 0;
    bool showClock       = false;
    bool clock24h        = false;
    int minuteOfDay      = 0;
    bool charging        = false;
    int charge           = 0;
    bool showBatteryText = false;

    bool operator==(const StatusValues&) const = default;
};

/**
 * The status group: the clock, the battery level and the wifi signal, or the hardware bar in
 * their place. It renders again when a value that it shows changes and then stays the same for
 * kSettleMs. The platform can give a wrong value for a short time while it updates the value.
 */
class StatusGroupView : public PlatformGroupView {
public:
    /** Draws the group on dst for the hardware bar showSetting, and returns its width. */
    using DrawGroup = int (*)(SDL_Surface* dst, int showSetting);

    /** Returns the values that the group shows now. It is called on each frame. */
    using ReadValues = StatusValues (*)();

    /** Pass null in drawGroup and readValues for the group and the values of the platform. */
    explicit StatusGroupView(DrawGroup drawGroup = nullptr, ReadValues readValues = nullptr);

    static constexpr uint32_t kSettleMs = 1000;

    /** Renders the group again on the next layout. */
    void refresh() { needsRender = true; }

    void layout(UiLayoutContext& context) override;

private:
    DrawGroup drawGroup;
    ReadValues readValues;
    input::HardwareBar measuredBar = input::HardwareBar::None;
    StatusValues measuredValues;
    StatusValues pendingValues;
    uint32_t pendingSince = 0;
    // The values changed during the last render, thus its pixels can be wrong
    bool renderUncertain = false;
};

#endif
