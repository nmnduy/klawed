/*
 * Lightweight markdown renderer for ncurses TUI.
 *
 * Parses common markdown syntax and applies ncurses attributes for
 * readable terminal output.
 */

#include "markdown_render.h"
#include "tui.h"
#include "logger.h"

#include <ncurses.h>
#include <ctype.h>
#include <string.h>

/* ============================================================================
 * Inline markdown helpers
 * ============================================================================ */

static const char *find_bold_stars(const char *start, size_t len) {
    const char *p = start + 2;
    const char *end = start + len;

    while (p + 1 < end) {
        if (p[0] == '*' && p[1] == '*') {
            if ((size_t)(p - start) > 2) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static const char *find_bold_underscores(const char *start, size_t len) {
    const char *p = start + 2;
    const char *end = start + len;

    while (p + 1 < end) {
        if (p[0] == '_' && p[1] == '_') {
            if ((size_t)(p - start) > 2) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static const char *find_italic_star(const char *start, size_t len) {
    const char *p = start + 1;
    const char *end = start + len;

    while (p < end) {
        if (p[0] == '*' && (p + 1 >= end || p[1] != '*')) {
            if ((size_t)(p - start) > 1) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static const char *find_italic_underscore(const char *start, size_t len) {
    const char *p = start + 1;
    const char *end = start + len;

    while (p < end) {
        if (p[0] == '_' && (p + 1 >= end || p[1] != '_')) {
            if ((size_t)(p - start) > 1) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static const char *find_code_ticks(const char *start, size_t len, size_t tick_len) {
    const char *p = start + tick_len;
    const char *end = start + len;

    while (p + tick_len <= end) {
        if (memcmp(p, start, tick_len) == 0) {
            if ((size_t)(p - start) > tick_len) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static const char *find_strike_tildes(const char *start, size_t len) {
    const char *p = start + 2;
    const char *end = start + len;

    while (p + 1 < end) {
        if (p[0] == '~' && p[1] == '~') {
            if ((size_t)(p - start) > 2) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

/* ============================================================================
 * Inline rendering
 * ============================================================================ */

static void md_apply_attr(WINDOW *win, chtype attr, int base_pair) {
    if (base_pair > 0 && has_colors()) {
        wattron(win, COLOR_PAIR((unsigned)base_pair) | attr);
    } else {
        wattron(win, attr);
    }
}

static void md_remove_attr(WINDOW *win, chtype attr, int base_pair) {
    if (base_pair > 0 && has_colors()) {
        wattroff(win, COLOR_PAIR((unsigned)base_pair) | attr);
        wattron(win, COLOR_PAIR((unsigned)base_pair));
    } else {
        wattroff(win, attr);
    }
}

static void md_output_plain(WINDOW *win, const char *text, size_t len) {
    if (len > 0) {
        (void)waddnstr(win, text, (int)len);
    }
}

static void md_render_bold_stars(WINDOW *win, const char **pp, size_t len, int base_pair) {
    const char *close = find_bold_stars(*pp, len);

    if (close) {
        md_apply_attr(win, A_BOLD, base_pair);
        md_output_plain(win, *pp + 2, (size_t)(close - (*pp + 2)));
        md_remove_attr(win, A_BOLD, base_pair);
        *pp = close + 2;
    } else {
        md_output_plain(win, *pp, 1);
        (*pp)++;
    }
}

static void md_render_bold_underscores(WINDOW *win, const char **pp, size_t len,
                                       int base_pair) {
    const char *close = find_bold_underscores(*pp, len);

    if (close) {
        md_apply_attr(win, A_BOLD, base_pair);
        md_output_plain(win, *pp + 2, (size_t)(close - (*pp + 2)));
        md_remove_attr(win, A_BOLD, base_pair);
        *pp = close + 2;
    } else {
        md_output_plain(win, *pp, 1);
        (*pp)++;
    }
}

static void md_render_italic_star(WINDOW *win, const char **pp, size_t len, int base_pair) {
    const char *close = find_italic_star(*pp, len);

    if (close) {
        md_apply_attr(win, A_ITALIC, base_pair);
        md_output_plain(win, *pp + 1, (size_t)(close - (*pp + 1)));
        md_remove_attr(win, A_ITALIC, base_pair);
        *pp = close + 1;
    } else {
        md_output_plain(win, *pp, 1);
        (*pp)++;
    }
}

static void md_render_italic_underscore(WINDOW *win, const char **pp, size_t len,
                                        int base_pair) {
    const char *close = find_italic_underscore(*pp, len);

    if (close) {
        md_apply_attr(win, A_ITALIC, base_pair);
        md_output_plain(win, *pp + 1, (size_t)(close - (*pp + 1)));
        md_remove_attr(win, A_ITALIC, base_pair);
        *pp = close + 1;
    } else {
        md_output_plain(win, *pp, 1);
        (*pp)++;
    }
}

static void md_render_code(WINDOW *win, const char **pp, size_t len, int base_pair) {
    size_t tick_len = 1;
    const char *end = *pp + len;

    while (*pp + tick_len < end && (*pp)[tick_len] == '`') {
        tick_len++;
    }

    const char *close = find_code_ticks(*pp, len, tick_len);

    if (close) {
        (void)base_pair;
        wattron(win, A_DIM);
        md_output_plain(win, *pp + tick_len, (size_t)(close - (*pp + tick_len)));
        wattroff(win, A_DIM);
        *pp = close + tick_len;
    } else {
        md_output_plain(win, *pp, 1);
        (*pp)++;
    }
}

static void md_render_strike(WINDOW *win, const char **pp, size_t len, int base_pair) {
    const char *close = find_strike_tildes(*pp, len);

    if (close) {
        (void)base_pair;
        wattron(win, A_DIM);
        md_output_plain(win, *pp + 2, (size_t)(close - (*pp + 2)));
        wattroff(win, A_DIM);
        *pp = close + 2;
    } else {
        md_output_plain(win, *pp, 1);
        (*pp)++;
    }
}

void markdown_render_inline(TUIState *tui, const char *line, size_t len, int base_pair) {
    WINDOW *pad = NULL;
    const char *p = NULL;
    const char *end = NULL;

    if (!tui || !line || len == 0) {
        return;
    }

    pad = tui->wm.conv_pad;
    if (!pad) {
        return;
    }

    p = line;
    end = line + len;

    if (base_pair > 0 && has_colors()) {
        wattron(pad, COLOR_PAIR(base_pair));
    }

    while (p < end) {
        size_t remaining = (size_t)(end - p);

        if (remaining >= 2 && p[0] == '*' && p[1] == '*') {
            md_render_bold_stars(pad, &p, remaining, base_pair);
        } else if (remaining >= 2 && p[0] == '_' && p[1] == '_') {
            md_render_bold_underscores(pad, &p, remaining, base_pair);
        } else if (remaining >= 2 && p[0] == '~' && p[1] == '~') {
            md_render_strike(pad, &p, remaining, base_pair);
        } else if (p[0] == '*') {
            md_render_italic_star(pad, &p, remaining, base_pair);
        } else if (p[0] == '_') {
            md_render_italic_underscore(pad, &p, remaining, base_pair);
        } else if (p[0] == '`') {
            md_render_code(pad, &p, remaining, base_pair);
        } else {
            md_output_plain(pad, p, 1);
            p++;
        }
    }

    if (base_pair > 0 && has_colors()) {
        wattroff(pad, COLOR_PAIR(base_pair));
    }
}

/* ============================================================================
 * Block-level detection
 * ============================================================================ */

int markdown_header_level(const char *line, size_t len) {
    size_t i = 0;
    int level = 0;

    if (!line || len == 0) {
        return 0;
    }

    while (i < len && isspace((unsigned char)line[i])) {
        i++;
    }

    while (i < len && line[i] == '#' && level < 6) {
        level++;
        i++;
    }

    if (level == 0) {
        return 0;
    }

    if (i < len && isspace((unsigned char)line[i])) {
        return level;
    }

    return 0;
}

int markdown_code_fence(const char *line, size_t len) {
    size_t i = 0;

    if (!line || len == 0) {
        return 0;
    }

    while (i < len && isspace((unsigned char)line[i])) {
        i++;
    }

    if (i + 2 < len && line[i] == '`' && line[i + 1] == '`' && line[i + 2] == '`') {
        return 1;
    }

    return 0;
}

int markdown_hrule(const char *line, size_t len) {
    size_t i = 0;
    char c = '\0';
    int count = 0;

    if (!line || len == 0) {
        return 0;
    }

    while (i < len && isspace((unsigned char)line[i])) {
        i++;
    }

    if (i >= len) {
        return 0;
    }

    c = line[i];
    if (c != '-' && c != '*' && c != '_') {
        return 0;
    }

    while (i < len) {
        if (line[i] == c) {
            count++;
        } else if (!isspace((unsigned char)line[i])) {
            return 0;
        }
        i++;
    }

    return (count >= 3) ? 1 : 0;
}

char markdown_list_item(const char *line, size_t len, size_t *prefix_len, int *number) {
    size_t i = 0;

    if (!line || len == 0 || !prefix_len || !number) {
        return 0;
    }

    *prefix_len = 0;
    *number = 0;

    while (i < len && isspace((unsigned char)line[i])) {
        i++;
    }

    if (i >= len) {
        return 0;
    }

    /* Unordered list */
    if (line[i] == '-' || line[i] == '*' || line[i] == '+') {
        size_t marker_end = i + 1;

        while (marker_end < len && isspace((unsigned char)line[marker_end])) {
            marker_end++;
        }
        *prefix_len = marker_end;
        return line[i];
    }

    /* Ordered list */
    if (isdigit((unsigned char)line[i])) {
        int num = 0;

        while (i < len && isdigit((unsigned char)line[i])) {
            num = num * 10 + (line[i] - '0');
            i++;
        }
        if (i < len && line[i] == '.') {
            i++;
            while (i < len && isspace((unsigned char)line[i])) {
                i++;
            }
            *prefix_len = i;
            *number = num;
            return '1';
        }
    }

    return 0;
}

int markdown_blockquote(const char *line, size_t len, size_t *prefix_len) {
    size_t i = 0;

    if (!line || len == 0 || !prefix_len) {
        return 0;
    }

    *prefix_len = 0;

    while (i < len && isspace((unsigned char)line[i])) {
        i++;
    }

    if (i < len && line[i] == '>') {
        i++;
        while (i < len && isspace((unsigned char)line[i])) {
            i++;
        }
        *prefix_len = i;
        return 1;
    }

    return 0;
}
