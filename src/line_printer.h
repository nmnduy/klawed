/*
 * LinePrinter - Unified conversation pad line rendering
 *
 * Provides a single abstraction for drawing bordered lines to the ncurses
 * conversation pad.  Centralizes border glyph drawing, color management,
 * newline handling, and width calculations.
 */

#ifndef LINE_PRINTER_H
#define LINE_PRINTER_H

#include <stddef.h>

struct TUIStateStruct;
typedef struct TUIStateStruct TUIState;

typedef struct _win_st WINDOW;

typedef struct {
    WINDOW *pad;
    const char *border_str;      /* e.g. "│ " or NULL */
    int border_pair;             /* ncurses COLOR_PAIR for border glyph */
    int text_pair;               /* ncurses COLOR_PAIR for text */
    int pad_width;               /* cached pad width */
    int content_width;           /* pad_width - border_display_width */
    const char *search_pattern;  /* NULL if no active search */
} LinePrinter;

/* Initialize a LinePrinter.  border_str may be NULL for no border. */
void lp_init(LinePrinter *lp, WINDOW *pad, const char *border_str,
             int border_pair, int text_pair, int pad_width);

/* Draw border glyph + space at current cursor position.
 * Leaves text_pair active for subsequent writing. */
void lp_border(LinePrinter *lp);

/* Emit '\n' only if cursor is not already at column 0.
 * This avoids double newlines after ncurses auto-wrap. */
void lp_newline(LinePrinter *lp);

/* Print raw text without markdown interpretation.
 * If dim != 0, applies A_DIM attribute. */
void lp_print_raw(LinePrinter *lp, const char *text, size_t len, int dim);

/* Print text with inline markdown formatting. */
void lp_print_md(LinePrinter *lp, TUIState *tui, const char *text, size_t len);

/* Print text with automatic wrapping and border injection on continuation
 * lines.  Used for live streaming updates in bordered mode. */
void lp_print_text_wrapped(LinePrinter *lp, const char *text);

/* Find byte position that fits within max_display_width. */
size_t find_wrap_point(const char *text, size_t text_len, int max_display_width);

#endif /* LINE_PRINTER_H */
