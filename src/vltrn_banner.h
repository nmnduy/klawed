/*
 * vltrn_banner.h - VLTRN Ultron-themed greeting banner
 *
 * Displays ANSI art with full 256-color support when VLTRN_MODE=1
 */

#ifndef VLTRN_BANNER_H
#define VLTRN_BANNER_H

#include <ncurses.h>

// Forward declaration
struct TUIStateStruct;
typedef struct TUIStateStruct TUIState;

/**
 * Render an ANSI art file with 256-color support
 *
 * @param win            ncurses window to render to
 * @param filepath       Path to ANSI art file
 * @param start_y        Starting Y position
 * @param start_x        Starting X position
 * @param transparent_bg If non-zero, makes background colors 195/231 transparent
 * @return               Final Y position after rendering, or -1 on error
 */
int vltrn_render_banner(WINDOW *win, const char *filepath, int start_y, int start_x, int transparent_bg);

/**
 * Show the VLTRN greeting screen
 * Only displays if VLTRN_MODE environment variable is set to "1"
 *
 * @param tui   TUI state structure
 */
void vltrn_show_greeting(TUIState *tui);

#endif // VLTRN_BANNER_H
