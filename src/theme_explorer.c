#ifndef HAVE_STRLCPY
#include "compat.h"
#endif/*
 * theme_explorer.c - Interactive Theme Explorer Implementation
 *
 * Provides a full-screen TUI panel for browsing and previewing
 * all available color schemes with live preview.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "theme_explorer.h"
#include "builtin_themes.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

#include <ncurses.h>
#include <ctype.h>

// Preview sample text elements
typedef struct {
    const char *label;
    const char *text;
    int color_index;  // Kitty color index (0-15, or special)
} PreviewElement;

// Color indices from Kitty theme format
#define COLOR_FG      -1  // foreground
#define COLOR_RED      1  // color1 - errors
#define COLOR_GREEN    2  // color2 - user/success
#define COLOR_YELLOW   3  // color3 - status/warnings
#define COLOR_BLUE     4  // color4 - headers
#define COLOR_MAGENTA  5  // color5
#define COLOR_CYAN     6  // color6 - assistant
#define COLOR_WHITE    7  // color7
#define COLOR_BRIGHT_BLUE 12  // color12 - tools

// Preview elements to show for each theme
static const PreviewElement preview_elements[] = {
    {"Foreground",  "Main text color for content",      COLOR_FG},
    {"User",        "[User] Hello, how can you help?",  COLOR_GREEN},
    {"Assistant",   "[Assistant] I can help with...",   COLOR_CYAN},
    {"Status",      "[Status] Processing request...",   COLOR_YELLOW},
    {"Error",       "[Error] Something went wrong!",    COLOR_RED},
    {"Tool",        "[Tool] Running bash command...",   COLOR_BRIGHT_BLUE},
    {"Diff Add",    "+ Added line in green",            COLOR_GREEN},
    {"Diff Remove", "- Removed line in red",            COLOR_RED},
};

#define PREVIEW_ELEMENT_COUNT (sizeof(preview_elements) / sizeof(preview_elements[0]))

// Parse a hex color from theme content
static int parse_color_from_content(const char *content, const char *key, int *r, int *g, int *b) {
    char search_key[32];
    snprintf(search_key, sizeof(search_key), "%s ", key);

    const char *line = strstr(content, search_key);
    if (!line) return -1;

    // Find the hex value
    const char *p = line + strlen(search_key);
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '#') return -1;
    p++;

    // Parse 6 hex digits
    if (strlen(p) < 6) return -1;

    char hex[3] = {0};
    hex[0] = p[0]; hex[1] = p[1];
    *r = (int)strtol(hex, NULL, 16);
    hex[0] = p[2]; hex[1] = p[3];
    *g = (int)strtol(hex, NULL, 16);
    hex[0] = p[4]; hex[1] = p[5];
    *b = (int)strtol(hex, NULL, 16);

    return 0;
}

// Convert RGB to ncurses 256-color index
static int rgb_to_256(int r, int g, int b) {
    // Check if grayscale
    int avg = (r + g + b) / 3;
    int r_diff = abs(r - avg);
    int g_diff = abs(g - avg);
    int b_diff = abs(b - avg);

    if (r_diff < 10 && g_diff < 10 && b_diff < 10) {
        int gray = (avg * 23) / 255;
        return 232 + gray;
    }

    // RGB cube
    int ri = (r * 5) / 255;
    int gi = (g * 5) / 255;
    int bi = (b * 5) / 255;
    return 16 + (36 * ri) + (6 * gi) + bi;
}

// Get color index for a preview element from theme content
static int get_theme_color_256(const char *content, int color_idx) {
    char key[16];
    int r = 255, g = 255, b = 255;

    if (color_idx == COLOR_FG) {
        if (parse_color_from_content(content, "foreground", &r, &g, &b) != 0) {
            return 7;  // Default white
        }
    } else {
        snprintf(key, sizeof(key), "color%d", color_idx);
        if (parse_color_from_content(content, key, &r, &g, &b) != 0) {
            // Fallback to basic ANSI colors
            return color_idx;
        }
    }

    return rgb_to_256(r, g, b);
}

// API Functions

int theme_explorer_get_theme_count(void) {
    return (int)built_in_themes_count;
}

const char* theme_explorer_get_theme_name(int index) {
    if (index < 0 || index >= (int)built_in_themes_count) {
        return NULL;
    }
    return built_in_themes[index].name;
}

int theme_explorer_init(ThemeExplorerState *state) {
    if (!state) return -1;

    memset(state, 0, sizeof(ThemeExplorerState));

    // Get terminal dimensions
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Create a full-screen window
    state->win = newwin(max_y, max_x, 0, 0);
    if (!state->win) {
        LOG_ERROR("[THEME_EXPLORER] Failed to create window");
        return -1;
    }

    state->win_height = max_y;
    state->win_width = max_x;
    state->selected_index = 0;
    state->scroll_offset = 0;
    state->preview_start_col = max_x / 3;  // Left 1/3 for list, right 2/3 for preview

    // Enable keypad for arrow keys
    keypad(state->win, TRUE);

    LOG_INFO("[THEME_EXPLORER] Initialized with %d themes", theme_explorer_get_theme_count());
    return 0;
}

void theme_explorer_cleanup(ThemeExplorerState *state) {
    if (!state) return;

    if (state->win) {
        delwin(state->win);
        state->win = NULL;
    }

    LOG_DEBUG("[THEME_EXPLORER] Cleaned up");
}

// Render the theme list on the left side
static void render_theme_list(ThemeExplorerState *state) {
    int list_width = state->preview_start_col - 1;
    int list_height = state->win_height - 4;  // Reserve space for header/footer
    int theme_count = theme_explorer_get_theme_count();

    // Draw header
    wattron(state->win, A_BOLD);
    mvwprintw(state->win, 1, 2, "Available Themes (%d)", theme_count);
    wattroff(state->win, A_BOLD);

    // Draw separator line
    mvwvline(state->win, 1, state->preview_start_col - 1, ACS_VLINE, state->win_height - 2);

    // Calculate visible range
    int visible_count = list_height;
    if (state->selected_index < state->scroll_offset) {
        state->scroll_offset = state->selected_index;
    } else if (state->selected_index >= state->scroll_offset + visible_count) {
        state->scroll_offset = state->selected_index - visible_count + 1;
    }

    // Draw theme names
    for (int i = 0; i < visible_count && (i + state->scroll_offset) < theme_count; i++) {
        int theme_idx = i + state->scroll_offset;
        const char *name = theme_explorer_get_theme_name(theme_idx);
        if (!name) continue;

        int row = 3 + i;
        int is_selected = (theme_idx == state->selected_index);

        if (is_selected) {
            wattron(state->win, A_REVERSE | A_BOLD);
        }

        // Clear the line first
        wmove(state->win, row, 2);
        for (int j = 0; j < list_width - 3; j++) {
            waddch(state->win, ' ');
        }

        // Print theme name with indicator
        mvwprintw(state->win, row, 2, "%s %s",
                  is_selected ? ">" : " ", name);

        if (is_selected) {
            wattroff(state->win, A_REVERSE | A_BOLD);
        }
    }

    // Draw scroll indicators if needed
    if (state->scroll_offset > 0) {
        mvwaddch(state->win, 2, list_width / 2, ACS_UARROW);
    }
    if (state->scroll_offset + visible_count < theme_count) {
        mvwaddch(state->win, state->win_height - 3, list_width / 2, ACS_DARROW);
    }
}

// Render the preview panel on the right side
static void render_preview(ThemeExplorerState *state) {
    int preview_col = state->preview_start_col + 1;
    int preview_width = state->win_width - preview_col - 2;
    (void)preview_width;

    const char *theme_name = theme_explorer_get_theme_name(state->selected_index);
    if (!theme_name) return;

    // Get theme content
    const char *content = NULL;
    for (size_t i = 0; i < built_in_themes_count; i++) {
        if (strcmp(built_in_themes[i].name, theme_name) == 0) {
            content = built_in_themes[i].content;
            break;
        }
    }
    if (!content) return;

    // Draw preview header
    wattron(state->win, A_BOLD);
    mvwprintw(state->win, 1, preview_col, "Preview: %s", theme_name);
    wattroff(state->win, A_BOLD);

    // Draw preview elements
    int row = 4;
    for (size_t i = 0; i < PREVIEW_ELEMENT_COUNT && row < state->win_height - 3; i++) {
        const PreviewElement *elem = &preview_elements[i];

        // Get the 256-color index for this element
        int color_idx = get_theme_color_256(content, elem->color_index);

        // Use extended color pairs (starting from 100 to avoid conflicts)
        int pair_id = 100 + (int)i;
        init_pair((short)pair_id, (short)color_idx, -1);

        // Draw label
        mvwprintw(state->win, row, preview_col, "%-12s ", elem->label);

        // Draw sample text with theme color
        wattron(state->win, COLOR_PAIR(pair_id));
        wprintw(state->win, "%s", elem->text);
        wattroff(state->win, COLOR_PAIR(pair_id));

        row += 2;
    }

    // Draw color palette preview
    row += 1;
    if (row < state->win_height - 5) {
        mvwprintw(state->win, row, preview_col, "Color Palette:");
        row++;

        // Show colors 0-7 (normal)
        mvwprintw(state->win, row, preview_col, "Normal:  ");
        for (int c = 0; c <= 7; c++) {
            int idx = get_theme_color_256(content, c);
            int pair = 110 + c;
            init_pair((short)pair, (short)idx, -1);
            wattron(state->win, COLOR_PAIR(pair));
            wprintw(state->win, "███");
            wattroff(state->win, COLOR_PAIR(pair));
            waddch(state->win, ' ');
        }
        row++;

        // Show colors 8-15 (bright)
        mvwprintw(state->win, row, preview_col, "Bright:  ");
        for (int c = 8; c <= 15; c++) {
            int idx = get_theme_color_256(content, c);
            int pair = 110 + c;
            init_pair((short)pair, (short)idx, -1);
            wattron(state->win, COLOR_PAIR(pair));
            wprintw(state->win, "███");
            wattroff(state->win, COLOR_PAIR(pair));
            waddch(state->win, ' ');
        }
    }
}

// Draw the full explorer UI
static void render_explorer(ThemeExplorerState *state) {
    werase(state->win);

    // Draw border
    box(state->win, 0, 0);

    // Draw title
    wattron(state->win, A_BOLD);
    mvwprintw(state->win, 0, 2, " Theme Explorer ");
    wattroff(state->win, A_BOLD);

    // Draw help text at bottom
    mvwprintw(state->win, state->win_height - 1, 2,
              " j/↓:Next  k/↑:Prev  Enter:Select  q/ESC:Cancel ");

    // Render components
    render_theme_list(state);
    render_preview(state);

    wrefresh(state->win);
}

ThemeExplorerResult theme_explorer_run(ThemeExplorerState *state) {
    if (!state || !state->win) {
        return THEME_EXPLORER_ERROR;
    }

    int theme_count = theme_explorer_get_theme_count();
    if (theme_count <= 0) {
        LOG_ERROR("[THEME_EXPLORER] No themes available");
        return THEME_EXPLORER_ERROR;
    }

    // Initial render
    render_explorer(state);

    // Event loop
    int running = 1;
    ThemeExplorerResult result = THEME_EXPLORER_CANCELLED;

    while (running) {
        int ch = wgetch(state->win);

        switch (ch) {
            case 'q':
            case 27:  // ESC
                running = 0;
                result = THEME_EXPLORER_CANCELLED;
                break;

            case '\n':
            case '\r':
            case KEY_ENTER:
                // Select current theme
                {
                    const char *name = theme_explorer_get_theme_name(state->selected_index);
                    if (name) {
                        strlcpy(state->selected_theme, name, sizeof(state->selected_theme));
                        result = THEME_EXPLORER_SELECTED;
                        running = 0;
                    }
                }
                break;

            case 'j':
            case KEY_DOWN:
                if (state->selected_index < theme_count - 1) {
                    state->selected_index++;
                    render_explorer(state);
                }
                break;

            case 'k':
            case KEY_UP:
                if (state->selected_index > 0) {
                    state->selected_index--;
                    render_explorer(state);
                }
                break;

            case 'g':
                // gg - go to top
                state->selected_index = 0;
                state->scroll_offset = 0;
                render_explorer(state);
                break;

            case 'G':
                // G - go to bottom
                state->selected_index = theme_count - 1;
                render_explorer(state);
                break;

            case KEY_PPAGE:  // Page Up
                state->selected_index -= (state->win_height - 6);
                if (state->selected_index < 0) state->selected_index = 0;
                render_explorer(state);
                break;

            case KEY_NPAGE:  // Page Down
                state->selected_index += (state->win_height - 6);
                if (state->selected_index >= theme_count) {
                    state->selected_index = theme_count - 1;
                }
                render_explorer(state);
                break;

            case KEY_HOME:
                state->selected_index = 0;
                state->scroll_offset = 0;
                render_explorer(state);
                break;

            case KEY_END:
                state->selected_index = theme_count - 1;
                render_explorer(state);
                break;

            case KEY_RESIZE:
                // Handle terminal resize
                {
                    int max_y, max_x;
                    getmaxyx(stdscr, max_y, max_x);
                    wresize(state->win, max_y, max_x);
                    mvwin(state->win, 0, 0);
                    state->win_height = max_y;
                    state->win_width = max_x;
                    state->preview_start_col = max_x / 3;
                    render_explorer(state);
                }
                break;

            default:
                break;
        }
    }

    return result;
}

const char* theme_explorer_get_selected(ThemeExplorerState *state) {
    if (!state || state->selected_theme[0] == '\0') {
        return NULL;
    }
    return state->selected_theme;
}
