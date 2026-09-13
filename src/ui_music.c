#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_music.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_utils.h"
#include "ui_theme.h"

// Scroll text state for browser list (selected item)
static ScrollTextState browser_scroll = {0};

// Render the file browser
void render_browser(SDL_Surface* screen, int show_setting,
                    BrowserContext *browser) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    // The path of the directory from the music root, thus the user sees where the
    // rows are. The root itself is "/".
    const char* path = browser->current_path;
    size_t root_len = strlen(MUSIC_PATH);
    if (strncmp(path, MUSIC_PATH, root_len) == 0 && (path[root_len] == '/' || path[root_len] == '\0')) {
        path += root_len;
    }
    render_screen_header(screen, path[0] ? path : "/", show_setting);

    // Empty state at root: no playable music anywhere
    if (Browser_countAudioFiles(browser) == 0 && !Browser_hasParent(browser)) {
        if (!Browser_hasAudioRecursive(browser->current_path)) {
            render_empty_state(screen, "No music files found", "Add music to /Music on your SD card", NULL);
            return;
        }
    }

    // Use common list layout calculation
    ListLayout layout = calc_list_layout(screen);
    browser->items_per_page = layout.items_per_page;

    adjust_list_scroll(browser->selected, &browser->scroll_offset, browser->items_per_page);

    // Calculate icon size and spacing (icons are 24x24)
    int icon_size = Icons_isLoaded() ? SCALE1(24) : 0;
    int icon_spacing = Icons_isLoaded() ? SCALE1(6) : 0;
    int icon_offset = icon_size + icon_spacing;

    for (int i = 0; i < browser->items_per_page && browser->scroll_offset + i < browser->entry_count; i++) {
        int idx = browser->scroll_offset + i;
        FileEntry *entry = &browser->entries[idx];
        bool selected = (idx == browser->selected);

        int y = layout.list_y + i * layout.item_h;

        // Get display name (without folder brackets or prefixes when icons are used)
        char display[256];
        if (Icons_isLoaded()) {
            // With icons, use clean names
            if (entry->is_dir || entry->is_play_all) {
                strncpy(display, entry->name, sizeof(display) - 1);
                display[sizeof(display) - 1] = '\0';
            } else {
                Browser_getDisplayName(entry->name, display, sizeof(display));
            }
        } else {
            // Without icons, use text indicators
            if (entry->is_dir) {
                snprintf(display, sizeof(display), "[%s]", entry->name);
            } else if (entry->is_play_all) {
                snprintf(display, sizeof(display), "> %s", entry->name);
            } else {
                Browser_getDisplayName(entry->name, display, sizeof(display));
            }
        }

        // Render pill background and get text position (with icon offset)
        ListItemPos pos = render_list_item_pill(screen, &layout, display, truncated, y, selected, icon_offset);

        // Render icon if available
        if (Icons_isLoaded()) {
            SDL_Surface *icon = NULL;
            if (entry->is_dir) {
                icon = Icons_getFolder(THEME_ROLE_PRIMARY, selected);
            } else if (entry->is_play_all) {
                icon = Icons_getPlayAll(THEME_ROLE_PRIMARY, selected);
            } else {
                icon = Icons_getForFormat(entry->format, THEME_ROLE_PRIMARY, selected);
            }

            if (icon) {
                // Center icon vertically within the item
                int icon_y = y + (layout.item_h - icon_size) / 2;
                int icon_x = pos.text_x;

                // Scale and blit the icon
                SDL_Rect src_rect = {0, 0, icon->w, icon->h};
                SDL_Rect dst_rect = {icon_x, icon_y, icon_size, icon_size};
                SDL_BlitScaled(icon, &src_rect, screen, &dst_rect);
            }
        }

        // Adjust text position for icon
        int text_x = pos.text_x + icon_offset;
        int available_width = pos.pill_width - SCALE1(BUTTON_PADDING * 2) - icon_offset;

        // Use common text rendering with scrolling for selected items
        render_list_item_text(screen, &browser_scroll, display, Fonts_getMedium(),
                              text_x, pos.text_y, available_width, selected);
    }

    render_scroll_indicators(screen, &layout, browser->scroll_offset, browser->entry_count);

    // Button hints
    GFX_blitButtonGroup((char *[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char *[]){"B", "BACK", "A", "SELECT", NULL}, 1, screen, 1);
}

// Check if browser list has active scrolling (for refresh optimization)
bool browser_needs_scroll_refresh(void) {
    return ScrollText_isScrolling(&browser_scroll);
}

// Check if browser scroll needs a render to transition (delay phase)
bool browser_scroll_needs_render(void) {
    return ScrollText_needsRender(&browser_scroll);
}

// Animate browser scroll only (GPU mode, no screen redraw needed)
void browser_animate_scroll(void) {
    ScrollText_animateOnly(&browser_scroll);
}
