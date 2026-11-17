#include "module_about.h"

#include "defines.h"
#include "api.h"
#include "help_screen.h"
#include "ui_utils.h"
#include "selfupdate.h"
#include "ui_system.h"
#include "wifi.h"

typedef enum {
    ABOUT_STATE_ABOUT,
    ABOUT_STATE_UPDATING,
} AboutState;

ModuleExitReason AboutModule_run(DisplayContext* display) {
    AboutState state = ABOUT_STATE_ABOUT;
    int dirty = 1;
    int show_setting = 0;

    // What the About screen last painted, so a change repaints it exactly once
    UpdateUiState shown_update_ui = UPDATE_UI_UNCHECKED;

    ScreenTitle_start(false);

    while (1) {
        ModuleCommon_frameBegin(true);
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_ABOUT);
        if (global.should_quit) {
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            ModuleCommon_frameEnd(screen);
            continue;
        }

        switch (state) {
            case ABOUT_STATE_ABOUT: {
                SelfUpdate_update();
                const SelfUpdateStatus status = SelfUpdate_getStatus();
                const UpdateUiState update_ui = SelfUpdate_uiState(&status);

                // The check finishes on a worker thread, so the frame that has
                // to repaint is the one where its state changed under us
                if (update_ui != shown_update_ui) {
                    shown_update_ui = update_ui;
                    dirty = 1;
                }

                if (PAD_justPressed(BTN_A)) {
                    if (update_ui == UPDATE_UI_RESTART) {
                        // The new binary is already on disk; exiting with the
                        // flag set lets launch.sh bring it straight back up
                        SelfUpdate_requestRestart();
                        return MODULE_EXIT_QUIT;
                    } else if (update_ui == UPDATE_UI_AVAILABLE) {
                        SelfUpdate_startUpdate();
                        state = ABOUT_STATE_UPDATING;
                        dirty = 1;
                    } else if (update_ui == UPDATE_UI_UNCHECKED || update_ui == UPDATE_UI_FAILED) {
                        if (Wifi_ensureConnected(screen, show_setting)) {
                            SelfUpdate_checkForUpdate();
                        }
                        dirty = 1;
                    }
                    // Checking and up-to-date offer no A, so nothing to do
                }
                else if (PAD_justPressed(BTN_B)) {
                    return MODULE_EXIT_TO_MENU;
                }
                break;
            }

            case ABOUT_STATE_UPDATING: {
                // Disable autosleep during update
                ModuleCommon_setAutosleepDisabled(true);

                SelfUpdate_update();
                const SelfUpdateStatus update_status = SelfUpdate_getStatus();
                SelfUpdateState update_state = update_status.state;

                if (update_state == SELFUPDATE_STATE_COMPLETED) {
                    if (PAD_justPressed(BTN_A)) {
                        // The new binary is already on disk; exiting with the
                        // flag set lets launch.sh bring it straight back up
                        SelfUpdate_requestRestart();
                        ModuleCommon_setAutosleepDisabled(false);
                        return MODULE_EXIT_QUIT;
                    }
                }
                else if (PAD_justPressed(BTN_B)) {
                    if (update_state == SELFUPDATE_STATE_DOWNLOADING) {
                        SelfUpdate_cancelUpdate();
                    }
                    ModuleCommon_setAutosleepDisabled(false);
                    state = ABOUT_STATE_ABOUT;
                    dirty = 1;
                }

                // Always redraw during update
                dirty = 1;
                break;
            }
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        if (dirty) {
            switch (state) {
                case ABOUT_STATE_ABOUT:    render_about(screen, show_setting); break;
                case ABOUT_STATE_UPDATING: render_app_updating(screen, show_setting); break;
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ScreenTitle_frameEnd(&dirty, true);
        ModuleCommon_frameEnd(screen);
    }
}
