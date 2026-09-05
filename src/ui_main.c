#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_main.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include "selfupdate.h"
#include "module_common.h"
#include "module_menu.h"
#include "resume.h"
#include "background.h"

static const char* menu_label(MenuSelection selection) {
    switch (selection) {
        case MENU_RESUME:      return "Resume";
        case MENU_NOW_PLAYING: return "Now Playing";
        case MENU_LIBRARY:     return "Library";
        case MENU_RADIO:       return "Online Radio";
        case MENU_PODCAST:     return "Podcasts";
        case MENU_SETTINGS:    return "Settings";
        default:               return "";
    }
}

// Row map for the frame being rendered. render_simple_menu's label and text
// callbacks take an index and nothing else, so the map has to reach them from
// file scope.
static MenuRows current_rows;

// Scroll state for Resume track name
static ScrollTextState resume_scroll = {0};

// Get label for Now Playing based on background player type
static const char* get_now_playing_label(void) {
    switch (Background_getActive()) {
        case BG_MUSIC:  return "Music";
        case BG_RADIO:  return "Radio";
        case BG_PODCAST: return "Podcast";
        default: return "Audio";
    }
}

// Label callback for first item label and Settings update badge
static const char* main_menu_get_label(int index, const char* default_label,
                                        char* buffer, int buffer_size) {
    switch (MenuRows_selectionAt(&current_rows, index)) {
        case MENU_NOW_PLAYING:
            snprintf(buffer, buffer_size, "Now Playing: %s", get_now_playing_label());
            return buffer;
        case MENU_RESUME: {
            const char* label = Resume_getLabel();
            if (label) {
                snprintf(buffer, buffer_size, "%s", label);
                return buffer;
            }
            break;
        }
        case MENU_SETTINGS: {
            const SelfUpdateStatus status = SelfUpdate_getStatus();
            if (status.update_available) {
                snprintf(buffer, buffer_size, "Settings (Update available)");
                return buffer;
            }
            break;
        }
        default:
            break;
    }
    return NULL;  // Use default label
}

// Move the whole line of the Resume row and of the Now Playing row, the
// prefix with the title. The row that the cursor is not on draws by default.
static bool main_menu_render_text(SDL_Surface* screen, int index, bool selected,
                                   int text_x, int text_y, int max_text_width) {
    MenuSelection sel = MenuRows_selectionAt(&current_rows, index);
    if (sel != MENU_RESUME && sel != MENU_NOW_PLAYING) return false;
    if (!selected) return false;

    char buffer[MAX_PATH];
    const char* line = main_menu_get_label(index, NULL, buffer, sizeof(buffer));
    if (!line) return false;

    render_list_item_text(screen, &resume_scroll, line, Fonts_getLarge(),
                          text_x, text_y, max_text_width, true);
    return true;
}

// Render the main menu
void render_menu(SDL_Surface* screen, int show_setting, int menu_selected, int menu_scroll,
                 const MenuRows* rows) {
    current_rows = *rows;

    const char* items[MENU_ROWS_MAX];
    for (int i = 0; i < rows->count; i++) {
        items[i] = menu_label(MenuRows_selectionAt(rows, i));
    }

    SimpleMenuConfig config = {
        .title = "Music Player",
        .items = items,
        .item_count = rows->count,
        .btn_b_label = "EXIT",
        .get_label = main_menu_get_label,
        .render_badge = NULL,
        .get_icon = NULL,
        .render_text = main_menu_render_text
    };
    render_simple_menu(screen, show_setting, menu_selected, menu_scroll, &config);
}

// Controls help text for each page/state
typedef struct {
    const char* button;
    const char* action;
} ControlHelp;

// Blocks follow HelpId order.
// Fallback bindings (HELP_DEFAULT)
static const ControlHelp default_controls[] = {
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Main menu controls (A/B shown in footer)
static const ControlHelp main_menu_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"X", "Clear History/Playback"},
    {"B (double)", "Exit App"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Library menu controls (A/B shown in footer)
static const ControlHelp library_menu_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// File browser controls (A/B shown in footer)
static const ControlHelp browser_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Y", "Add to Playlist"},
    {"X", "Delete File"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Music player controls (A/B shown in footer)
static const ControlHelp player_controls[] = {
    {"X", "Toggle Shuffle"},
    {"Y", "Toggle Repeat"},
    {"Up/R1", "Next Track"},
    {"Down/L1", "Prev Track"},
    {"Left/Right", "Seek"},
    {"L2/L3", "Toggle Visualizer"},
    {"R2/R3", "Toggle Lyrics"},
    {"Select", "Screen Off"},
    {"Select + A", "Wake Screen"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Playlist list controls (A/B shown in footer)
static const ControlHelp keyboard_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"A", "Type Key"},
    {"B", "Delete / Exit"},
    {"X", "Shift"},
    {"X (hold)", "Caps Lock"},
    {"Y", "Language"},
    {"Select", "Confirm"},
    {NULL, NULL}
};

static const ControlHelp playlist_list_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Y", "New Playlist"},
    {"X", "Delete Playlist"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Playlist detail controls (A/B shown in footer)
static const ControlHelp playlist_detail_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"X", "Remove Track"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Radio list controls (A/B shown in footer)
static const ControlHelp radio_list_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Y", "Manage Stations"},
    {"X", "Delete Station"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Radio playing controls (B shown in footer)
static const ControlHelp radio_playing_controls[] = {
    {"A", "Play/Pause"},
    {"Up/R1", "Next Station"},
    {"Down/L1", "Prev Station"},
    {"Select", "Screen Off"},
    {"Select + A", "Wake Screen"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Radio manage stations controls - country list (A/B shown in footer)
static const ControlHelp radio_manage_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Y", "Manual Setup Help"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Radio browse stations controls - station list (A/B shown in footer)
static const ControlHelp radio_browse_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"A", "Add/Remove Station"},
    {"Y", "Manual Setup Help"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Radio manual help controls (B shown in footer)
static const ControlHelp radio_help_controls[] = {
    {"Up/Down", "Scroll"},
    {"B", "Back"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast menu controls (shows subscribed podcasts)
static const ControlHelp podcast_menu_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"X", "Unsubscribe"},
    {"Y", "Manage Podcasts"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast manage menu controls
static const ControlHelp podcast_manage_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast top shows controls
static const ControlHelp podcast_top_shows_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"A", "Subscribe/Unsubscribe"},
    {"X", "Refresh List"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast search results controls
static const ControlHelp podcast_search_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"A", "Subscribe/Unsubscribe"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast episodes list controls
static const ControlHelp podcast_episodes_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Y", "Refresh Episodes"},
    {"X", "Mark Played/Unplayed"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast downloads queue controls (X/B shown in footer)
static const ControlHelp podcast_downloads_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"X", "Cancel Download"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Podcast playing controls
static const ControlHelp podcast_playing_controls[] = {
    {"Left", "Rewind 10s"},
    {"Right", "Forward 30s"},
    {"Up/Down", "Playback Speed"},
    {"Select", "Screen Off"},
    {"Select + A", "Wake Screen"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// YouTube menu controls (A/B shown in footer)
static const ControlHelp youtube_menu_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Downloader search-in-progress controls (B shown in footer)
static const ControlHelp downloader_searching_controls[] = {
    {"B", "Cancel search"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// YouTube results controls (A/B shown in footer)
static const ControlHelp youtube_results_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"B", "Back"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// YouTube queue controls (A/B/X shown in footer)
static const ControlHelp youtube_queue_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Navigate"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// yt-dlp install/update controls (A/B shown in footer)
static const ControlHelp ytdlp_controls[] = {
    {"A", "Install/Dismiss"},
    {"B", "Cancel/Back"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Settings menu controls
static const ControlHelp settings_controls[] = {
    {"Up/Down", "Navigate"},
    {"Left/Right", "Change Value/Navigate"},
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// About page controls (A/B shown in footer)
static const ControlHelp about_controls[] = {
    {"Start (hold)", "Exit App"},
    {NULL, NULL}
};

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, HelpId help_id) {
    int hw = screen->w;
    int hh = screen->h;

    // Keep the frame sane if the switch below ever stops covering every HelpId.
    const ControlHelp* controls = default_controls;
    const char* page_title = "Controls";

    switch (help_id) {
        // HELP_NONE never gets here - handleGlobalInput() does not open the
        // dialog for it - but the switch has to cover every HelpId.
        case HELP_NONE:
        case HELP_DEFAULT:
            break;
        case HELP_MAIN_MENU:
            controls = main_menu_controls;
            page_title = "Main Menu";
            break;
        case HELP_LIBRARY_MENU:
            controls = library_menu_controls;
            page_title = "Library";
            break;
        case HELP_BROWSER:
            controls = browser_controls;
            page_title = "File Browser";
            break;
        case HELP_PLAYER:
            controls = player_controls;
            page_title = "Music Player";
            break;
        case HELP_PLAYLIST_LIST:
            controls = playlist_list_controls;
            page_title = "Playlists";
            break;
        case HELP_PLAYLIST_DETAIL:
            controls = playlist_detail_controls;
            page_title = "Playlist Tracks";
            break;
        case HELP_RADIO_LIST:
            controls = radio_list_controls;
            page_title = "Radio Stations";
            break;
        case HELP_RADIO_PLAYING:
            controls = radio_playing_controls;
            page_title = "Radio Player";
            break;
        case HELP_RADIO_MANAGE:
            controls = radio_manage_controls;
            page_title = "Manage Stations";
            break;
        case HELP_RADIO_BROWSE:
            controls = radio_browse_controls;
            page_title = "Browse Stations";
            break;
        case HELP_RADIO_HELP:
            controls = radio_help_controls;
            page_title = "Radio Help";
            break;
        case HELP_PODCAST_MENU:
            controls = podcast_menu_controls;
            page_title = "Podcasts";
            break;
        case HELP_PODCAST_MANAGE:
            controls = podcast_manage_controls;
            page_title = "Manage Podcasts";
            break;
        case HELP_PODCAST_TOP_SHOWS:
            controls = podcast_top_shows_controls;
            page_title = "Top Shows";
            break;
        case HELP_PODCAST_SEARCH_RESULTS:
            controls = podcast_search_controls;
            page_title = "Search Results";
            break;
        case HELP_PODCAST_EPISODES:
            controls = podcast_episodes_controls;
            page_title = "Episodes";
            break;
        case HELP_PODCAST_DOWNLOADS:
            controls = podcast_downloads_controls;
            page_title = "Downloads";
            break;
        case HELP_PODCAST_PLAYING:
            controls = podcast_playing_controls;
            page_title = "Podcast Player";
            break;
        case HELP_DOWNLOADER_MENU:
            controls = youtube_menu_controls;
            page_title = "Downloader";
            break;
        case HELP_DOWNLOADER_SEARCHING:
            controls = downloader_searching_controls;
            page_title = "Searching";
            break;
        case HELP_DOWNLOADER_RESULTS:
            controls = youtube_results_controls;
            page_title = "Search Results";
            break;
        case HELP_DOWNLOADER_QUEUE:
            controls = youtube_queue_controls;
            page_title = "Download Queue";
            break;
        case HELP_DOWNLOADER_YTDLP:
            controls = ytdlp_controls;
            page_title = "Youtube download helpers";
            break;
        case HELP_KEYBOARD:
            controls = keyboard_controls;
            page_title = "Keyboard";
            break;
        case HELP_SETTINGS:
            controls = settings_controls;
            page_title = "Settings";
            break;
        case HELP_ABOUT:
            controls = about_controls;
            page_title = "About";
            break;
        // No default arm on purpose: compiler with -Werror=switch
        // forces to have all enum cases to be explicitly handled.
    }

    // Count controls
    int control_count = 0;
    while (controls[control_count].button != NULL) {
        control_count++;
    }

    // Dialog box
    int line_height = SCALE1(18);
    int hint_gap = SCALE1(15);
    int box_h = SCALE1(60) + (control_count * line_height) + hint_gap;
    DialogBox db = render_dialog_box(screen, SCALE1(240), box_h);

    // Title text (left aligned)
    SDL_Surface* title_surf = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), page_title, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (title_surf) {
        SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){db.content_x, db.box_y + SCALE1(10)});
        SDL_FreeSurface(title_surf);
    }

    int y_offset = db.box_y + SCALE1(35);
    int right_col = db.box_x + SCALE1(90);

    for (int i = 0; i < control_count; i++) {
        // Button name
        SDL_Surface* btn_surf = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), controls[i].button, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (btn_surf) {
            SDL_BlitSurface(btn_surf, NULL, screen, &(SDL_Rect){db.content_x, y_offset});
            SDL_FreeSurface(btn_surf);
        }

        // Action description
        SDL_Surface* action_surf = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), controls[i].action, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (action_surf) {
            SDL_BlitSurface(action_surf, NULL, screen, &(SDL_Rect){right_col, y_offset});
            SDL_FreeSurface(action_surf);
        }

        y_offset += line_height;
    }

    // Button hint at bottom (left aligned, same gap as title from top)
    const char* hint = "Press any button to close";
    SDL_Surface* hint_surf = TTF_RenderUTF8_Blended(
        Fonts_getSmall(), hint, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (hint_surf) {
        int hint_y = db.box_y + db.box_h - SCALE1(10) - hint_surf->h;
        SDL_BlitSurface(hint_surf, NULL, screen, &(SDL_Rect){db.content_x, hint_y});
        SDL_FreeSurface(hint_surf);
    }
}

// Render confirmation dialog overlay (title + optional content + "A: Yes  B: No")
void render_confirmation_dialog(SDL_Surface* screen, const char* content, const char* title) {
    bool has_content = content && content[0];
    int box_h = has_content ? SCALE1(110) : SCALE1(90);
    DialogBox db = render_dialog_box(screen, SCALE1(280), box_h);
    int hw = screen->w;

    // Title text
    if (!title) title = "Delete File?";
    int title_y = has_content ? db.box_y + SCALE1(15) : db.box_y + SCALE1(20);
    SDL_Surface* title_surf = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), title, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (title_surf) {
        SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){(hw - title_surf->w) / 2, title_y});
        SDL_FreeSurface(title_surf);
    }

    // Content text (truncated if needed)
    if (has_content) {
        char truncated[64];
        GFX_truncateText(Fonts_getSmall(), content, truncated, db.box_w - SCALE1(20), 0);
        SDL_Surface* name_surf = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (name_surf) {
            SDL_BlitSurface(name_surf, NULL, screen, &(SDL_Rect){(hw - name_surf->w) / 2, db.box_y + SCALE1(45)});
            SDL_FreeSurface(name_surf);
        }
    }

    // Button hints
    int hint_y = has_content ? db.box_y + SCALE1(75) : db.box_y + SCALE1(55);
    const char* hint = "A: Yes   B: No";
    SDL_Surface* hint_surf = TTF_RenderUTF8_Blended(
        Fonts_getSmall(), hint, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (hint_surf) {
        SDL_BlitSurface(hint_surf, NULL, screen, &(SDL_Rect){(hw - hint_surf->w) / 2, hint_y});
        SDL_FreeSurface(hint_surf);
    }
}

// Check if Resume scroll needs continuous redraw (software scroll mode)
bool menu_needs_scroll_redraw(void) {
    // Needs redraw if scrolling is active OR about to start (delay -> active transition)
    return ScrollText_isScrolling(&resume_scroll) || ScrollText_needsRender(&resume_scroll);
}

// Render screen off hint message (shown before screen turns off)
void render_screen_off_hint(SDL_Surface* screen) {
    int hw = screen->w;
    int hh = screen->h;

    // Fill entire screen with black
    //noinspection HardcodedColor
    SDL_FillRect(screen, NULL, RGB_BLACK);

    // Render hint message centered
    const char* msg = "Press SELECT + A to wake screen";
    //noinspection HardcodedColor
    SDL_Surface* msg_surf = TTF_RenderUTF8_Blended(Fonts_getMedium(), msg, COLOR_WHITE);
    if (msg_surf) {
        SDL_BlitSurface(msg_surf, NULL, screen, &(SDL_Rect){(hw - msg_surf->w) / 2, (hh - msg_surf->h) / 2});
        SDL_FreeSurface(msg_surf);
    }
}
