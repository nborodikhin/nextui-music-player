#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_radio.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_theme.h"
#include "radio_curated.h"

// One state is sufficient for the three lists of this module. Only one list
// draws at a time, and only the row that the cursor is on moves its title.
static ScrollTextState radio_list_scroll = {0};

// Render the radio station list
void render_radio_list(SDL_Surface* screen, int show_setting,
                       int radio_selected, int* radio_scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    render_screen_header(screen, "Online Radio", show_setting);

    // Station list
    RadioStation* stations;
    int station_count = Radio_getStations(&stations);

    // Empty state - no stations saved
    if (station_count == 0) {
        render_empty_state(screen, "No stations saved", "Press Y to manage stations", "MANAGE");
        return;
    }

    // Use common list layout calculation
    ListLayout layout = calc_list_layout(screen);
    adjust_list_scroll(radio_selected, radio_scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *radio_scroll + i < station_count; i++) {
        int idx = *radio_scroll + i;
        RadioStation* station = &stations[idx];
        bool selected = (idx == radio_selected);

        int y = layout.list_y + i * layout.item_h;

        // Four steps, in this order: the band of a selected row, the label at
        // the right edge, the pill of the title, then the title. The pill thus
        // covers the label if it ever reaches it.
        int genre_width = 0;
        if (station->genre[0]) {
            TTF_SizeUTF8(Fonts_getTiny(), station->genre, &genre_width, NULL);
        }

        if (selected && genre_width > 0) {
            SDL_Rect band = {SCALE1(PADDING), y, layout.max_width, layout.item_h};
            draw_list_item_band(screen, &band);
        }

        if (genre_width > 0) {
            SDL_Color genre_color = Theme_getColor(THEME_ROLE_PRIMARY, false);
            SDL_Surface* genre_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), station->genre, genre_color);
            if (genre_text) {
                SDL_BlitSurface(genre_text, NULL, screen, &(SDL_Rect){hw - genre_text->w - SCALE1(PADDING * 2), y + (layout.item_h - genre_text->h) / 2});
                SDL_FreeSurface(genre_text);
            }
        }

        ListItemPos pos = render_list_item_pill_right(screen, &layout, station->name,
                                                      truncated, y, selected, 0,
                                                      genre_width);

        // The row that the cursor is on moves the whole name. Another row shows
        // the name that the pill cut, thus its end lands on a character.
        render_list_item_text(screen, &radio_list_scroll,
                              selected ? station->name : truncated, Fonts_getMedium(),
                              pos.text_x, pos.text_y,
                              pos.pill_width - SCALE1(BUTTON_PADDING * 2), selected);

    }

    render_scroll_indicators(screen, &layout, *radio_scroll, station_count);

    // Show note for users using default stations (no custom stations yet)
    if (!Radio_hasUserStations()) {
        int note_y = hh - SCALE1(BUTTON_SIZE + BUTTON_MARGIN + PADDING + 55);

        const char* note1 = "These are default stations";
        SDL_Surface* note1_surf = TTF_RenderUTF8_Blended(
            Fonts_getTiny(), note1, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (note1_surf) {
            SDL_BlitSurface(note1_surf, NULL, screen, &(SDL_Rect){(hw - note1_surf->w) / 2, note_y});
            SDL_FreeSurface(note1_surf);
        }

        const char* note2 = "Press Y to manage stations";
        SDL_Surface* note2_surf = TTF_RenderUTF8_Blended(
            Fonts_getTiny(), note2, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (note2_surf) {
            SDL_BlitSurface(note2_surf, NULL, screen, &(SDL_Rect){(hw - note2_surf->w) / 2, note_y + SCALE1(14)});
            SDL_FreeSurface(note2_surf);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "PLAY", NULL}, 1, screen, 1);
}

// Helper to get current station by index
// Render add stations - country selection screen
void render_radio_add(SDL_Surface* screen, int show_setting,
                      int add_country_selected, int* add_country_scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    render_screen_header(screen, "Manage Stations", show_setting);

    // Country list
    int country_count = Radio_getCuratedCountryCount();
    const CuratedCountry* countries = Radio_getCuratedCountries();

    // Use common list layout calculation
    ListLayout layout = calc_list_layout(screen);
    adjust_list_scroll(add_country_selected, add_country_scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *add_country_scroll + i < country_count; i++) {
        int idx = *add_country_scroll + i;
        const CuratedCountry* country = &countries[idx];
        bool selected = (idx == add_country_selected);

        int y = layout.list_y + i * layout.item_h;
        // Four steps, in this order: the band of a selected row, the label at
        // the right edge, the pill of the title, then the title.
        int curated_station_count = Radio_getCuratedStationCount(country->code);
        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%d stations", curated_station_count);
        int count_width = 0;
        TTF_SizeUTF8(Fonts_getTiny(), count_str, &count_width, NULL);

        if (selected) {
            SDL_Rect band = {SCALE1(PADDING), y, layout.max_width, layout.item_h};
            draw_list_item_band(screen, &band);
        }

        SDL_Color count_color = Theme_getColor(THEME_ROLE_PRIMARY, false);
        SDL_Surface* count_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), count_str, count_color);
        if (count_text) {
            SDL_BlitSurface(count_text, NULL, screen, &(SDL_Rect){hw - count_text->w - SCALE1(PADDING * 2), y + (layout.item_h - count_text->h) / 2});
            SDL_FreeSurface(count_text);
        }

        ListItemPos pos = render_list_item_pill_right(screen, &layout, country->name,
                                                      truncated, y, selected, 0,
                                                      count_width);

        // Country name. The row that the cursor is on moves the whole name.
        render_list_item_text(screen, &radio_list_scroll,
                              selected ? country->name : truncated, Fonts_getMedium(),
                              pos.text_x, pos.text_y,
                              pos.pill_width - SCALE1(BUTTON_PADDING * 2), selected);

    }

    render_scroll_indicators(screen, &layout, *add_country_scroll, country_count);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SELECT", NULL}, 1, screen, 1);
}

// Render add stations - station selection screen
void render_radio_add_stations(SDL_Surface* screen, int show_setting,
                               const char* country_code,
                               int add_station_selected, int* add_station_scroll,
                               const int* sorted_indices, int sorted_count) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    // Get country name for title
    const char* country_name = "Stations";
    const CuratedCountry* countries = Radio_getCuratedCountries();
    int country_count = Radio_getCuratedCountryCount();
    for (int i = 0; i < country_count; i++) {
        if (strcmp(countries[i].code, country_code) == 0) {
            country_name = countries[i].name;
            break;
        }
    }

    render_screen_header(screen, country_name, show_setting);

    // Get stations for selected country
    int station_count = 0;
    const CuratedStation* stations = Radio_getCuratedStations(country_code, &station_count);

    // Use common list layout calculation
    ListLayout layout = calc_list_layout(screen);
    adjust_list_scroll(add_station_selected, add_station_scroll, layout.items_per_page);

    // Determine if the currently selected station is already added
    bool selected_exists = false;
    if (sorted_count > 0 && add_station_selected < sorted_count) {
        int sel_actual = sorted_indices[add_station_selected];
        if (sel_actual < station_count) {
            selected_exists = Radio_stationExists(stations[sel_actual].url);
        }
    }

    for (int i = 0; i < layout.items_per_page && *add_station_scroll + i < sorted_count; i++) {
        int idx = *add_station_scroll + i;
        int actual_idx = sorted_indices[idx];
        const CuratedStation* station = &stations[actual_idx];
        bool selected = (idx == add_station_selected);
        bool added = Radio_stationExists(station->url);

        int y = layout.list_y + i * layout.item_h;

        // Calculate prefix width for added indicator
        const char* prefix = added ? "[+] " : "";
        int prefix_width = 0;
        if (added) {
            int pw, ph;
            TTF_SizeUTF8(Fonts_getSmall(), "[+]", &pw, &ph);
            prefix_width = pw + SCALE1(6);
        }

        // Four steps, in this order: the band of a selected row, the label at
        // the right edge, the pill of the title, then the title. The width of
        // the label is what the title gives up, thus the two never overlap.
        int genre_width = 0;
        if (station->genre[0]) {
            TTF_SizeUTF8(Fonts_getTiny(), station->genre, &genre_width, NULL);
        }

        int pill_max_width = layout.max_width;
        if (genre_width > 0) {
            pill_max_width -= genre_width + SCALE1(PADDING * 2);
        }
        int name_max_width = pill_max_width - prefix_width;
        int text_width = GFX_truncateText(Fonts_getMedium(), station->name, truncated, name_max_width, SCALE1(BUTTON_PADDING * 2));
        // GFX_truncateText() gives the width with its padding already in it.
        int pill_width = MIN(pill_max_width, prefix_width + text_width);

        if (selected && genre_width > 0) {
            SDL_Rect band = {SCALE1(PADDING), y, layout.max_width, layout.item_h};
            draw_list_item_band(screen, &band);
        }

        if (genre_width > 0) {
            SDL_Color genre_color = Theme_getColor(THEME_ROLE_PRIMARY, false);
            SDL_Surface* genre_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), station->genre, genre_color);
            if (genre_text) {
                SDL_BlitSurface(genre_text, NULL, screen, &(SDL_Rect){hw - genre_text->w - SCALE1(PADDING * 2), y + (layout.item_h - genre_text->h) / 2});
                SDL_FreeSurface(genre_text);
            }
        }

        // Background pill
        SDL_Rect pill_rect = {SCALE1(PADDING), y, pill_width, layout.item_h};
        draw_list_item_bg(screen, &pill_rect, selected);

        int text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
        int text_y = y + (layout.item_h - TTF_FontHeight(Fonts_getMedium())) / 2;

        // Added indicator prefix
        if (added) {
            SDL_Color prefix_color = Theme_getColor(
                THEME_ROLE_PRIMARY, selected);
            SDL_Surface* prefix_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), "[+]", prefix_color);
            if (prefix_text) {
                SDL_BlitSurface(prefix_text, NULL, screen, &(SDL_Rect){text_x, y + (layout.item_h - prefix_text->h) / 2});
                SDL_FreeSurface(prefix_text);
            }
        }

        // The row that the cursor is on moves the whole name. Another row shows
        // the name that GFX_truncateText() cut, thus its end lands on a
        // character and not in the middle of a glyph.
        render_list_item_text(screen, &radio_list_scroll,
                              selected ? station->name : truncated, Fonts_getMedium(),
                              text_x + prefix_width, text_y,
                              pill_width - prefix_width - SCALE1(BUTTON_PADDING * 2),
                              selected);

    }

    render_scroll_indicators(screen, &layout, *add_station_scroll, sorted_count);

    // Button hints - dynamic based on whether selected station is already added
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (selected_exists) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "REMOVE", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "ADD", NULL}, 1, screen, 1);
    }
}

// Render help/instructions screen
void render_radio_help(SDL_Surface* screen, int show_setting, int* help_scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "How to Add Stations", show_setting);

    // Content padding (aligned with title pill)
    int left_padding = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    int right_padding = SCALE1(PADDING);
    int bottom_padding = SCALE1(PADDING);
    int max_content_width = hw - left_padding - right_padding;

    // Instructions text
    int content_start_y = SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN);
    int line_h = SCALE1(18);
    int button_area_h = SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN);
    int visible_height = hh - content_start_y - button_area_h - bottom_padding;

    const char* lines[] = {
        "To add custom radio stations:",
        "",
        "1. Create or edit the file:",
        "   /.userdata/shared/music-player/radio/stations.txt",
        "",
        "2. Add one station per line:",
        "   Name|URL|Genre|Slogan",
        "",
        "Example:",
        "   My Radio|http://example.com/stream|Music|Slogan",
        "",
        "Notes:",
        "- MP3, AAC, and M3U8 formats supported",
        "- Maximum 32 stations",
        "- Slogan is optional (shown when no song info)",
        "",
        "Find more stations at: fmstream.org"
    };

    int num_lines = sizeof(lines) / sizeof(lines[0]);

    // Calculate total content height
    int total_content_h = 0;
    for (int i = 0; i < num_lines; i++) {
        if (lines[i][0] == '\0') {
            total_content_h += line_h / 2;
        } else {
            total_content_h += line_h;
        }
    }

    // Calculate max scroll
    int max_scroll = total_content_h - visible_height;
    if (max_scroll < 0) max_scroll = 0;
    if (*help_scroll > max_scroll) *help_scroll = max_scroll;
    if (*help_scroll < 0) *help_scroll = 0;

    // Render lines with scroll offset
    int text_y = content_start_y - *help_scroll;
    for (int i = 0; i < num_lines; i++) {
        int current_line_h = (lines[i][0] == '\0') ? line_h / 2 : line_h;

        // Skip lines that are above visible area
        if (text_y + current_line_h < content_start_y) {
            text_y += current_line_h;
            continue;
        }

        // Stop if we're below visible area
        if (text_y >= hh - button_area_h) {
            break;
        }

        if (lines[i][0] == '\0') {
            text_y += line_h / 2;
            continue;
        }

        SDL_Color color = Theme_getColor(THEME_ROLE_PRIMARY, false);
        TTF_Font* use_font = Fonts_getSmall();

        // Highlight special lines
        if (strstr(lines[i], "Example:") || strstr(lines[i], "Notes:")) {
            color = Theme_getColor(THEME_ROLE_SECONDARY, false);
        } else if (lines[i][0] == '-') {
            color = Theme_getColor(THEME_ROLE_SECONDARY, false);
            use_font = Fonts_getTiny();
        }

        SDL_Surface* line_text = TTF_RenderUTF8_Blended(use_font, lines[i], color);
        if (line_text) {
            SDL_BlitSurface(line_text, NULL, screen, &(SDL_Rect){left_padding, text_y});
            SDL_FreeSurface(line_text);
        }
        text_y += line_h;
    }

    // Scroll indicators
    if (max_scroll > 0) {
        int ox = (hw - SCALE1(24)) / 2;
        if (*help_scroll > 0) {
            draw_scroll_indicator(screen, ASSET_SCROLL_UP, ox, content_start_y - SCALE1(12));
        }
        if (*help_scroll < max_scroll) {
            draw_scroll_indicator(screen, ASSET_SCROLL_DOWN, ox,
                                  hh - button_area_h - bottom_padding - SCALE1(4));
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
}

// True while a title of a list of this module moves.
bool radio_list_needs_scroll_refresh(void) {
    return ScrollText_isScrolling(&radio_list_scroll);
}

// True while the scroll waits to start and needs one more frame.
bool radio_list_scroll_needs_render(void) {
    return ScrollText_needsRender(&radio_list_scroll);
}

// Move the title without a redraw of the screen.
void radio_list_animate_scroll(void) {
    ScrollText_animateOnly(&radio_list_scroll);
}

// Forget the title that moves. Call this on the way out of a list.
void radio_list_clear_scroll(void) {
    ScrollText_forget(&radio_list_scroll);
}
