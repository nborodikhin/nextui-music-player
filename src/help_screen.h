#ifndef __HELP_SCREEN_H__
#define __HELP_SCREEN_H__

// Identifies which screen the controls-help dialog is being opened from.
//
// Every module passes one of these to ModuleCommon_handleGlobalInput(), which
// forwards it to render_controls_help(). One shared enum so two screens cannot
// quietly pick the same id and get each other's help text.
//
// Pure integer logic, no platform includes.
typedef enum {
    // Fallback bindings. Nothing sends this; it is the value a zero-initialized
    // HelpId lands on, and what render_controls_help() falls back to.
    HELP_DEFAULT,

    // No help for this screen. Modules pass it while a modal dialog is up, so
    // START does not stack a controls box on top of the dialog.
    HELP_NONE,

    HELP_MAIN_MENU,

    HELP_LIBRARY_MENU,
    HELP_BROWSER,
    HELP_PLAYER,

    HELP_AUDIOBOOK_LIBRARY,
    HELP_AUDIOBOOK_CHAPTERS,
    HELP_AUDIOBOOK_PLAYING,

    HELP_PLAYLIST_LIST,
    HELP_PLAYLIST_DETAIL,

    HELP_RADIO_LIST,
    HELP_RADIO_PLAYING,
    HELP_RADIO_MANAGE,
    HELP_RADIO_BROWSE,
    HELP_RADIO_HELP,

    HELP_PODCAST_MENU,
    HELP_PODCAST_MANAGE,
    HELP_PODCAST_TOP_SHOWS,
    HELP_PODCAST_SEARCH_RESULTS,
    HELP_PODCAST_EPISODES,
    HELP_PODCAST_DOWNLOADS,
    HELP_PODCAST_PLAYING,

    HELP_DOWNLOADER_MENU,
    HELP_DOWNLOADER_SEARCHING,
    HELP_DOWNLOADER_RESULTS,
    HELP_DOWNLOADER_QUEUE,
    HELP_DOWNLOADER_YTDLP,

    HELP_SETTINGS,
    HELP_ABOUT,
} HelpId;

#endif
