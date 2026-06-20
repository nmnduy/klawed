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

/* ============================================================================
 * Static helpers
 * ============================================================================ */

static int utf8_display_width(const char *str) {
    if (!str || !*str) {
        return 0;
    }

    char *old_locale = setlocale(LC_ALL, NULL);
    if (old_locale) {
        old_locale = strdup(old_locale);
    }
    setlocale(LC_ALL, "C.UTF-8");

    size_t len = mbstowcs(NULL, str, 0);
    if (len == (size_t)-1) {
        if (old_locale) {
            setlocale(LC_ALL, old_locale);
            free(old_locale);
        }
        return (int)strlen(str);
    }

    wchar_t *wstr = malloc((len + 1) * sizeof(wchar_t));
    if (!wstr) {
        if (old_locale) {
            setlocale(LC_ALL, old_locale);
            free(old_locale);
        }
        return (int)strlen(str);
    }

    mbstowcs(wstr, str, len + 1);
    int width = wcswidth(wstr, len);
    free(wstr);

    if (old_locale) {
        setlocale(LC_ALL, old_locale);
        free(old_locale);
    }

    return width >= 0 ? width : (int)strlen(str);
}

size_t find_wrap_point(const char *text, size_t text_len, int max_display_width) {
    if (max_display_width <= 0) {
        return 1;
    }

    char *old_locale = setlocale(LC_ALL, NULL);
    if (old_locale) {
        old_locale = strdup(old_locale);
    }
    setlocale(LC_ALL, "C.UTF-8");

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

    if (old_locale) {
        setlocale(LC_ALL, old_locale);
        free(old_locale);
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

void lp_init(LinePrinter *lp, WINDOW *pad, const char *border_str,
             int border_pair, int text_pair, int pad_width) {
    if (!lp) {
        return;
    }
    lp->pad = pad;
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
        text_pair == NCURSES_PAIR_ERROR_BG) {
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
        while (cur_x < lp->pad_width) {
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
    int cur_y = 0, cur_x = 0;
    getyx(lp->pad, cur_y, cur_x);
    int orig_x = cur_x;

    // Fill remaining width with background spaces without adding a newline.
    // Used at the end of a streaming chunk to paint the incomplete line,
    // then the cursor is restored so the next chunk continues from the
    // correct position.
    if (lp->fill_bg_pair > 0 && cur_x < lp->pad_width) {
        if (has_colors()) {
            wattron(lp->pad, COLOR_PAIR((unsigned)lp->fill_bg_pair));
        }
        while (cur_x < lp->pad_width) {
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

        /* Calculate display width of segment */
        char *seg_copy = malloc(seg_len + 1);
        if (!seg_copy) {
            waddch(lp->pad, (chtype)(unsigned char)*p);
            p++;
            continue;
        }
        memcpy(seg_copy, p, seg_len);
        seg_copy[seg_len] = '\0';
        int seg_width = utf8_display_width(seg_copy);
        free(seg_copy);

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
