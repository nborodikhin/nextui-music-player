#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_system.h"
#include "selfupdate.h"
#include "ui_system.h"
#include "wifi.h"

// Internal states
typedef enum {
    SYSTEM_STATE_ABOUT,
    SYSTEM_STATE_UPDATING
} SystemState;

ModuleExitReason SystemModule_run(SDL_Surface* screen) {
    SystemState state = SYSTEM_STATE_ABOUT;
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        ModuleCommon_startFrame();
        PAD_poll();

        // Handle global input first
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting,
            state == SYSTEM_STATE_ABOUT ? 35 : 34);  // STATE_ABOUT=35, STATE_APP_UPDATING=34
        if (global.should_quit) {
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            ModuleCommon_adaptiveSync();
            continue;
        }

        // State-specific handling
        if (state == SYSTEM_STATE_UPDATING) {
            // Disable autosleep during update
            ModuleCommon_setAutosleepDisabled(true);

            SelfUpdate_update();
            const SelfUpdateStatus* status = SelfUpdate_getStatus();
            SelfUpdateState update_state = status->state;

            if (update_state == SELFUPDATE_STATE_COMPLETED) {
                if (PAD_justPressed(BTN_A)) {
                    // Quit to apply update
                    ModuleCommon_setAutosleepDisabled(false);
                    return MODULE_EXIT_QUIT;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                if (update_state == SELFUPDATE_STATE_DOWNLOADING) {
                    SelfUpdate_cancelUpdate();
                }
                ModuleCommon_setAutosleepDisabled(false);
                state = SYSTEM_STATE_ABOUT;
                dirty = 1;
            }

            // Always redraw during update
            dirty = 1;
        }
        else {  // SYSTEM_STATE_ABOUT
            SelfUpdate_update();
            const SelfUpdateStatus* status = SelfUpdate_getStatus();

            // Keep refreshing while checking for updates
            if (status->state == SELFUPDATE_STATE_CHECKING) {
                dirty = 1;
            }

            if (PAD_justPressed(BTN_A)) {
                if (status->update_available) {
                    SelfUpdate_startUpdate();
                    state = SYSTEM_STATE_UPDATING;
                    dirty = 1;
                } else if (status->state != SELFUPDATE_STATE_CHECKING) {
                    if (Wifi_ensureConnected(screen, show_setting)) {
                        SelfUpdate_checkForUpdate();
                    }
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                return MODULE_EXIT_TO_MENU;
            }
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            if (state == SYSTEM_STATE_UPDATING) {
                render_app_updating(screen, show_setting);
            } else {
                render_about(screen, show_setting);
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        } else {
            ModuleCommon_adaptiveSync();
        }
    }
}
