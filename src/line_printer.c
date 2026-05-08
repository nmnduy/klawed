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
    if (cur_x > 0) {
        waddch(lp->pad, '\n');
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
    while (*p) {
        int cur_y, cur_x;
        getyx(lp->pad, cur_y, cur_x);
        (void)cur_y;

        if (cur_x == 0 && lp->border_str) {
            lp_border(lp);
            getyx(lp->pad, cur_y, cur_x);
        }

        if (*p == '\n') {
            waddch(lp->pad, '\n');
            p++;
            continue;
        }

        int available = lp->pad_width - cur_x;
        if (available <= 0) {
            waddch(lp->pad, '\n');
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

        if (seg_width <= available) {
            waddnstr(lp->pad, p, (int)seg_len);
            p = seg_end;
        } else {
            size_t chunk = find_wrap_point(p, seg_len, available);
            if (chunk > 0) {
                waddnstr(lp->pad, p, (int)chunk);
                p += chunk;
            } else {
                waddch(lp->pad, (chtype)(unsigned char)*p);
                p++;
            }
        }
    }
}
