/*
 * theme_explorer.h - Interactive Theme Explorer
 *
 * Provides a TUI panel for previewing all available color schemes
 * in real-time, allowing users to see how each theme affects the
 * appearance before selecting one.
 */

#ifndef THEME_EXPLORER_H
#define THEME_EXPLORER_H

#include <ncurses.h>

// Theme explorer result codes
typedef enum {
    THEME_EXPLORER_CANCELLED = 0,  // User cancelled (ESC/q)
    THEME_EXPLORER_SELECTED = 1,   // User selected a theme (Enter)
    THEME_EXPLORER_ERROR = -1      // Error occurred
} ThemeExplorerResult;

// Theme explorer state
typedef struct {
    WINDOW *win;              // Main window for theme explorer
    int selected_index;       // Currently selected theme index
    int scroll_offset;        // Scroll offset for theme list
    int win_height;           // Window height
    int win_width;            // Window width
    int preview_start_col;    // Column where preview starts
    char selected_theme[64];  // Name of selected theme (if any)
} ThemeExplorerState;

/**
 * Initialize the theme explorer
 *
 * @param state  Theme explorer state to initialize
 * @return       0 on success, -1 on error
 */
int theme_explorer_init(ThemeExplorerState *state);

/**
 * Clean up theme explorer resources
 *
 * @param state  Theme explorer state to clean up
 */
void theme_explorer_cleanup(ThemeExplorerState *state);

/**
 * Run the theme explorer in a modal loop
 *
 * This function takes over the screen and allows the user to browse
 * through available themes with live preview.
 *
 * Controls:
 *   j/↓   - Move to next theme
 *   k/↑   - Move to previous theme
 *   Enter - Select theme and exit
 *   q/ESC - Cancel and exit
 *
 * @param state  Theme explorer state
 * @return       THEME_EXPLORER_SELECTED if theme was selected,
 *               THEME_EXPLORER_CANCELLED if cancelled,
 *               THEME_EXPLORER_ERROR on error
 */
ThemeExplorerResult theme_explorer_run(ThemeExplorerState *state);

/**
 * Get the name of the selected theme (after run returns SELECTED)
 *
 * @param state  Theme explorer state
 * @return       Theme name string (valid until cleanup)
 */
const char* theme_explorer_get_selected(ThemeExplorerState *state);

/**
 * Get the count of available themes
 *
 * @return  Number of available themes (built-in + current)
 */
int theme_explorer_get_theme_count(void);

/**
 * Get the name of a theme by index
 *
 * @param index  Theme index (0 to count-1)
 * @return       Theme name, or NULL if index out of range
 */
const char* theme_explorer_get_theme_name(int index);

#endif // THEME_EXPLORER_H
