#include "common_button_handler.hpp"

#include "background.h"
#include "module_common.h"

namespace input {

bool CommonButtonHandler::onTick(uint32_t) {
    // Background playback keeps the device awake. The playback clears it when it stops.
    if (Background_isPlaying()) ModuleCommon_setAutosleepDisabled(true);
    InputState& state = InputState::getInstance();
    int dirty         = 0;
    int showSetting   = static_cast<int>(state.getHardwareBar());
    ModuleCommon_PWR_updateWithWake(&dirty, &showSetting, &woke);
    state.setHardwareBar(static_cast<HardwareBar>(showSetting));
    return dirty != 0;
}

}  // namespace input
