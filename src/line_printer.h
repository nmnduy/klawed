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
    TUIState *tui;               /* owner state (for pad growth on demand) */
    const char *border_str;      /* e.g. "│ " or NULL */
    int border_pair;             /* ncurses COLOR_PAIR for border glyph */
    int text_pair;               /* ncurses COLOR_PAIR for text */
    int pad_width;               /* cached pad width */
    int content_width;           /* pad_width - border_display_width */
    const char *search_pattern;  /* NULL if no active search */
    int fill_bg_pair;            /* If > 0, fill remaining line width with spaces using this pair */
} LinePrinter;

/* Initialize a LinePrinter.  border_str may be NULL for no border. */
void lp_init(LinePrinter *lp, TUIState *tui, WINDOW *pad, const char *border_str,
             int border_pair, int text_pair, int pad_width);

/* Draw border glyph + space at current cursor position.
 * Leaves text_pair active for subsequent writing. */
void lp_border(LinePrinter *lp);

/* Emit '\n' to advance to the next line.
 * Always emits the newline, even at column 0, so that empty lines
 * (paragraph breaks between markdown blocks) are preserved.
 * If fill_bg_pair is active, fills the remainder of the current
 * line with background-tinted spaces before emitting the newline. */
void lp_newline(LinePrinter *lp);

/* Fill rest of current line with background-tinted spaces.
 * Does NOT add a newline — cursor is restored to original position
 * after filling so subsequent writes continue at the correct spot.
 * Only active when fill_bg_pair > 0. */
void lp_fill_line(LinePrinter *lp);

/* Print raw text without markdown interpretation.
 * If dim != 0, applies A_DIM attribute. */
void lp_print_raw(LinePrinter *lp, const char *text, size_t len, int dim);

/* Print text with inline markdown formatting. */
void lp_print_md(LinePrinter *lp, TUIState *tui, const char *text, size_t len);

/* Print text with automatic wrapping and border injection on continuation
 * lines.  Used for live streaming updates in bordered mode. */
void lp_print_text_wrapped(LinePrinter *lp, const char *text);

/* Find byte position that fits within max_display_width.
 * Breaks at column boundaries, not word boundaries. */
size_t find_wrap_point(const char *text, size_t text_len, int max_display_width);

/* Display width (terminal columns) of a length-bounded UTF-8 string.
 * Allocation-free and locale-switch-free (UTF-8 locale ensured once).
 * Falls back to one column per byte for invalid sequences. */
int utf8_display_width_n(const char *str, size_t len);

/* Find byte position that fits within max_display_width,
 * preferring word boundaries.  Backs up to the previous space
 * if the column-based break falls mid-word.  Falls back to
 * column-based force-break for words longer than the width. */
size_t find_wrap_point_word(const char *text, size_t text_len, int max_display_width);

#endif /* LINE_PRINTER_H */
