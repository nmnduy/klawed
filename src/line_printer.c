/*
 * LinePrinter implementation
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "line_printer.h"
#include "markdown_render.h"
#include "tui.h"

#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <wchar.h>
#include <locale.h>
#include <langinfo.h>

/* ============================================================================
 * Static helpers
 * ============================================================================ */

/* Ensure LC_CTYPE is UTF-8 so multibyte conversion (mbrtowc/wcwidth) yields
 * correct terminal column widths.  Runs ONCE — afterwards the locale is left
 * alone.  The old code called setlocale() + strdup() + restore on EVERY
 * invocation of utf8_display_width()/find_wrap_point(); setlocale() loads
 * locale data files each time it changes the locale, which showed up as a
 * top CPU consumer (open$NOCANCEL in profile stacks) when rendering long
 * conversations. */
static void ensure_utf8_locale(void) {
    static int checked = 0;
    if (checked) {
        return;
    }
    /* Set the flag FIRST so this never re-runs: the old code called
     * setlocale() on EVERY line, and setlocale() loads locale data files
     * each time it changes the locale (a top CPU cost — open$NOCANCEL in
     * profile stacks when rendering long conversations). */
    checked = 1;

    const char *codeset = nl_langinfo(CODESET);
    if (codeset &&
        (strstr(codeset, "UTF-8") != NULL ||
         strstr(codeset, "utf-8") != NULL ||
         strstr(codeset, "UTF8") != NULL)) {
        return;  // Already UTF-8
    }

    /* Non-UTF-8 locale — switch LC_CTYPE only, so isspace()/collation in
     * other categories keep the user's configured behavior.  Try a couple
     * of common names; if none is available we keep the current locale and
     * fall back to per-byte width accounting (degraded, not broken). */
    static const char *const candidates[] = {"C.UTF-8", "UTF-8", NULL};
    for (int i = 0; candidates[i] != NULL; i++) {
        if (setlocale(LC_CTYPE, candidates[i]) != NULL) {
            break;
        }
    }
}

/* Display width (terminal columns) of a length-bounded UTF-8 string.
 * Allocation-free and locale-switch-free. */
int utf8_display_width_n(const char *str, size_t len) {
    if (!str || len == 0) {
        return 0;
    }

    ensure_utf8_locale();

    int width = 0;
    size_t remaining = len;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (remaining > 0) {
        wchar_t wc;
        size_t char_bytes = mbrtowc(&wc, str, remaining, &state);

        if (char_bytes == 0) {
            break;  /* Embedded NUL */
        } else if (char_bytes == (size_t)-1 || char_bytes == (size_t)-2) {
            /* Invalid or incomplete sequence: count remaining bytes as 1 col */
            width += (int)remaining;
            break;
        } else {
            int char_width = wcwidth(wc);
            if (char_width < 0) {
                char_width = 1;
            }
            width += char_width;
            str += char_bytes;
            remaining -= char_bytes;
        }
    }

    return width;
}

static int utf8_display_width(const char *str) {
    if (!str || !*str) {
        return 0;
    }
    return utf8_display_width_n(str, strlen(str));
}

size_t find_wrap_point(const char *text, size_t text_len, int max_display_width) {
    if (max_display_width <= 0) {
        return 1;
    }

    ensure_utf8_locale();

    size_t bytes_used = 0;
    int display_width = 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (bytes_used < text_len && display_width < max_display_width) {
        wchar_t wc;
        size_t char_bytes = mbrtowc(&wc, text + bytes_used, text_len - bytes_used, &state);

        if (char_bytes == 0) {
            break;
        } else if (char_bytes == (size_t)-1 || char_bytes == (size_t)-2) {
            bytes_used++;
            display_width++;
        } else {
            int char_width = wcwidth(wc);
            if (char_width < 0) {
                char_width = 1;
            }
            if (display_width + char_width > max_display_width) {
                break;
            }
            bytes_used += char_bytes;
            display_width += char_width;
        }
    }

    return bytes_used > 0 ? bytes_used : 1;
}

/*
 * Like find_wrap_point() but prefers word boundaries.
 * If the column-based break falls mid-word, backs up to the previous space.
 * If no space is found in the entire segment (word longer than width),
 * falls back to a column-based force-break.
 */
size_t find_wrap_point_word(const char *text, size_t text_len, int max_display_width) {
    /* Find the column-based break point first */
    size_t col_break = find_wrap_point(text, text_len, max_display_width);
    if (col_break == 0) return 1;

    /* If we've consumed all the text, nothing to do */
    if (col_break >= text_len) return col_break;

    /* If the break is at a space, it's already at a word boundary */
    if (text[col_break] == ' ' || text[col_break] == '\n') {
        return col_break;
    }

    /* Check if there's a space before the break point.
     * Walk backwards from the break point looking for a space. */
    size_t pos = col_break;
    while (pos > 0) {
        pos--;
        if (text[pos] == ' ') {
            /* Break BEFORE the space (so the space is consumed/skipped) */
            return pos;
        }
    }

    /* No space found in the segment — word is longer than available width.
     * Force-break at the column boundary. */
    return col_break;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/* Ensure the pad has room below the cursor before writing.  Prevents
 * ncurses bottom-edge scrolling (wscrl), which memmoves the entire pad
 * buffer per character — catastrophic O(n^2) cost for long sessions. */
static void lp_ensure_room(LinePrinter *lp) {
    if (!lp || !lp->tui || !lp->pad) {
        return;
    }
    (void)window_manager_ensure_cursor_room(&lp->tui->wm, WM_PAD_GROW_MARGIN);
}

void lp_init(LinePrinter *lp, TUIState *tui, WINDOW *pad, const char *border_str,
             int border_pair, int text_pair, int pad_width) {
    if (!lp) {
        return;
    }
    lp->pad = pad;
    lp->tui = tui;
    lp->border_str = border_str;
    lp->border_pair = border_pair;
    lp->text_pair = text_pair;
    lp->pad_width = pad_width;
    lp->search_pattern = NULL;
    lp->fill_bg_pair = 0;  // No background fill by default

    // Explicit: enable full-width background fill for the assistant BG
    // response style and the error BG style.  This is simpler and more
    // maintainable than runtime introspection via pair_content() — the
    // intent is immediately clear.
    if (text_pair == NCURSES_PAIR_ASSISTANT_BG ||
        text_pair == NCURSES_PAIR_ERROR_BG ||
        text_pair == NCURSES_PAIR_CODE_BLOCK) {
        lp->fill_bg_pair = text_pair;
    }

    if (border_str) {
        int bw = utf8_display_width(border_str);
        lp->content_width = pad_width - bw;
        if (lp->content_width < 1) {
            lp->content_width = 1;
        }
    } else {
        lp->content_width = pad_width;
        if (lp->content_width < 1) {
            lp->content_width = 1;
        }
    }
}

void lp_border(LinePrinter *lp) {
    if (!lp || !lp->pad) {
        return;
    }

    lp_ensure_room(lp);

    if (lp->border_str) {
        /* Draw non-space glyphs with border color; draw trailing spaces
         * with text color so they don't pick up the border background. */
        size_t border_len = strlen(lp->border_str);
        size_t glyph_len = border_len;
        while (glyph_len > 0 && lp->border_str[glyph_len - 1] == ' ') {
            glyph_len--;
        }

        if (glyph_len > 0) {
            if (lp->border_pair > 0 && has_colors()) {
                wattron(lp->pad, COLOR_PAIR((unsigned)lp->border_pair) | A_BOLD);
            }
            waddnstr(lp->pad, lp->border_str, (int)glyph_len);
            if (lp->border_pair > 0 && has_colors()) {
                wattroff(lp->pad, COLOR_PAIR((unsigned)lp->border_pair) | A_BOLD);
            }
        }

        if (lp->text_pair > 0 && has_colors()) {
            wattron(lp->pad, COLOR_PAIR((unsigned)lp->text_pair));
        }
        for (size_t i = glyph_len; i < border_len; i++) {
            waddch(lp->pad, ' ');
        }
    } else {
        if (lp->text_pair > 0 && has_colors()) {
            wattron(lp->pad, COLOR_PAIR((unsigned)lp->text_pair));
        }
    }
}

void lp_newline(LinePrinter *lp) {
    if (!lp || !lp->pad) {
        return;
    }

    lp_ensure_room(lp);

    int cur_y = 0, cur_x = 0;
    getyx(lp->pad, cur_y, cur_x);
    (void)cur_y;

    // If background fill is active, fill remaining width with spaces
    // to create a full-width painted background effect
    // Fill even when cur_x == 0 (empty lines) so blank separator lines
    // within the response also get the background color painted.
    if (lp->fill_bg_pair > 0 && cur_x < lp->pad_width) {
        if (has_colors()) {
            wattron(lp->pad, COLOR_PAIR((unsigned)lp->fill_bg_pair));
        }
        /* Fill to pad_width-1, not pad_width.  scrollok is enabled on
         * the conversation pad, so writing a character at the last
         * column (pad_width-1) triggers ncurses auto-wrap.  If we fill
         * to pad_width, the auto-wrap moves the cursor to column 0 of
         * the next line, and the subsequent waddch('\n') below creates
         * a blank line after every background-filled line (code blocks,
         * assistant bg, error bg).  Stopping at pad_width-1 leaves the
         * cursor at the last column so waddch('\n') handles the newline
         * correctly via its built-in clrtoeol+linefeed. */
        while (cur_x < lp->pad_width - 1) {
            waddch(lp->pad, ' ');
            cur_x++;
        }
        if (has_colors()) {
            wattroff(lp->pad, COLOR_PAIR((unsigned)lp->fill_bg_pair));
        }
    }

    waddch(lp->pad, '\n');
}

void lp_fill_line(LinePrinter *lp) {
    if (!lp || !lp->pad) {
        return;
    }

    lp_ensure_room(lp);

    int cur_y = 0, cur_x = 0;
    getyx(lp->pad, cur_y, cur_x);
    int orig_x = cur_x;

    // Fill remaining width with background spaces without adding a newline.
    // Used at the end of a streaming chunk to paint the incomplete line,
    // then the cursor is restored so the next chunk continues from the
    // correct position.
    // See lp_newline for explanation of pad_width-1 vs pad_width.
    if (lp->fill_bg_pair > 0 && cur_x < lp->pad_width) {
        if (has_colors()) {
            wattron(lp->pad, COLOR_PAIR((unsigned)lp->fill_bg_pair));
        }
        while (cur_x < lp->pad_width - 1) {
            waddch(lp->pad, ' ');
            cur_x++;
        }
        if (has_colors()) {
            wattroff(lp->pad, COLOR_PAIR((unsigned)lp->fill_bg_pair));
        }
        // Restore cursor to where it was before filling
        wmove(lp->pad, cur_y, orig_x);
    }
}

void lp_print_raw(LinePrinter *lp, const char *text, size_t len, int dim) {
    if (!lp || !lp->pad || !text || len == 0) {
        return;
    }
    lp_ensure_room(lp);
    if (dim) {
        wattron(lp->pad, A_DIM);
    }
    waddnstr(lp->pad, text, (int)len);
    if (dim) {
        wattroff(lp->pad, A_DIM);
    }
}

void lp_print_md(LinePrinter *lp, TUIState *tui, const char *text, size_t len) {
    if (!lp || !lp->pad || !text || len == 0) {
        return;
    }
    markdown_render_inline(tui, text, len, lp->text_pair);
}

void lp_print_text_wrapped(LinePrinter *lp, const char *text) {
    if (!lp || !lp->pad || !text) {
        return;
    }

    const char *p = text;
    int just_wrapped = 0;  /* true when ncurses auto-wrapped the previous line */
    while (*p) {
        int cur_y, cur_x;
        getyx(lp->pad, cur_y, cur_x);
        (void)cur_y;

        if (cur_x == 0 && lp->border_str) {
            lp_border(lp);
            getyx(lp->pad, cur_y, cur_x);
        }

        if (*p == '\n') {
            /* If the previous content write caused ncurses to auto-wrap,
             * a single \n would create a double newline.  Skip it but
             * preserve paragraph breaks (\n\n). */
            if (just_wrapped) {
                just_wrapped = 0;
                p++;
                if (*p == '\n') {
                    /* Paragraph break after wrap: emit one newline */
                    p++;
                    lp_newline(lp);
                    continue;
                }
                continue;
            }
            lp_newline(lp);
            just_wrapped = 0;
            p++;
            continue;
        }

        int available = lp->pad_width - cur_x;
        if (available <= 0) {
            /* Overflow: content filled the line.  Explicitly wrap
             * instead of calling lp_newline to avoid the double
             * newline interaction with ncurses auto-wrap. */
            lp_ensure_room(lp);
            wmove(lp->pad, cur_y + 1, 0);
            if (lp->border_str) {
                lp_border(lp);
            }
            just_wrapped = 1;
            continue;
        }

        const char *seg_end = p;
        while (*seg_end && *seg_end != '\n') {
            seg_end++;
        }
        size_t seg_len = (size_t)(seg_end - p);

        /* Calculate display width of segment (allocation-free) */
        int seg_width = utf8_display_width_n(p, seg_len);

        /* Save y before write to detect auto-wrap */
        int before_y = cur_y;
        if (seg_width <= available) {
            waddnstr(lp->pad, p, (int)seg_len);
            p = seg_end;
        } else {
            size_t chunk = find_wrap_point_word(p, seg_len, available);
            if (chunk > 0) {
                waddnstr(lp->pad, p, (int)chunk);
                p += chunk;
            } else {
                waddch(lp->pad, (chtype)(unsigned char)*p);
                p++;
            }
        }

        /* Detect whether ncurses auto-wrapped the line */
        getyx(lp->pad, cur_y, cur_x);
        if (cur_y != before_y) {
            /* ncurses auto-wrapped: the content filled the line exactly */
            just_wrapped = 1;
        } else if (cur_x >= lp->pad_width) {
            /* Cursor past the right margin but ncurses didn't auto-wrap
             * (platform-dependent).  Explicitly wrap to avoid double
             * newline when the next char is \n. */
            wmove(lp->pad, cur_y + 1, 0);
            if (lp->border_str) {
                lp_border(lp);
            }
            just_wrapped = 1;
        }
    }

    // Fill the rest of the current line for a clean visual when
    // text_pair has a background (e.g., BG response style). Does
    // NOT add a newline so the caller can append more text later.
    lp_fill_line(lp);
}
