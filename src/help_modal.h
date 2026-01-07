/*
 * help_modal.h - Help Modal Dialog
 *
 * Displays a TUI popup with helpful commands and keyboard shortcuts.
 */

#ifndef HELP_MODAL_H
#define HELP_MODAL_H

#include <ncurses.h>

// Help modal state
typedef struct {
    WINDOW *popup_win;       // Popup window
    int popup_height;        // Window height
    int popup_width;         // Window width
    int popup_y;             // Window Y position
    int popup_x;             // Window X position
    int scroll_offset;       // Scroll offset for content
    int content_lines;       // Total lines of content
    int is_active;           // Whether modal is active
} HelpModalState;

/**
 * Initialize help modal state
 *
 * @param state  Help modal state to initialize
 * @return       0 on success, -1 on error
 */
int help_modal_init(HelpModalState *state);

/**
 * Clean up help modal resources
 *
 * @param state  Help modal state to clean up
 */
void help_modal_cleanup(HelpModalState *state);

/**
 * Show the help modal dialog
 *
 * Creates a centered popup window and displays help content.
 * Blocks until user dismisses (q, ESC, Enter, or any key).
 *
 * @param state         Help modal state
 * @param screen_height Terminal height
 * @param screen_width  Terminal width
 * @return              0 on success, -1 on error
 */
int help_modal_show(HelpModalState *state, int screen_height, int screen_width);

/**
 * Close the help modal dialog
 *
 * @param state  Help modal state
 */
void help_modal_close(HelpModalState *state);

/**
 * Render the help modal content
 *
 * @param state  Help modal state
 */
void help_modal_render(HelpModalState *state);

/**
 * Handle a key press in the help modal
 *
 * @param state  Help modal state
 * @param ch     Key code
 * @return       1 if modal should close, 0 otherwise
 */
int help_modal_handle_key(HelpModalState *state, int ch);

/**
 * Run the help modal in a blocking loop
 *
 * Shows the help modal and blocks until the user dismisses it.
 * This function should be called after tui_suspend() and before tui_resume().
 *
 * @param state         Help modal state
 * @param screen_height Terminal height
 * @param screen_width  Terminal width
 * @return              0 on success, -1 on error
 */
int help_modal_run(HelpModalState *state, int screen_height, int screen_width);

#endif // HELP_MODAL_H
