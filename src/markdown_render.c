/*
 * Lightweight markdown renderer for ncurses TUI.
 *
 * Parses common markdown syntax and applies ncurses attributes for
 * readable terminal output.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "markdown_render.h"
#include "tui.h"
#include "logger.h"

#include <ncurses.h>
#include <ctype.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

/* Forward declarations for test builds */
#ifdef TEST_BUILD
const char *find_bold_stars(const char *start, size_t len);
const char *find_bold_underscores(const char *start, size_t len);
const char *find_italic_star(const char *start, size_t len);
const char *find_italic_underscore(const char *start, size_t len);
#endif

/* ============================================================================
 * Inline markdown helpers
 * ============================================================================ */

#ifdef TEST_BUILD
const char *find_bold_stars(const char *start, size_t len) {
#else
static const char *find_bold_stars(const char *start, size_t len) {
#endif
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

#ifdef TEST_BUILD
const char *find_bold_underscores(const char *start, size_t len) {
#else
static const char *find_bold_underscores(const char *start, size_t len) {
#endif
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

#ifdef TEST_BUILD
const char *find_italic_star(const char *start, size_t len) {
#else
static const char *find_italic_star(const char *start, size_t len) {
#endif
    const char *p = start + 1;
    const char *end = start + len;

    while (p < end) {
        if (p[0] == '*' && (p + 1 >= end || p[1] != '*')) {
            if ((size_t)(p - start) > 1) {
                /* Closing delimiter: next char must be end-of-string or non-alphanumeric */
                if (p + 1 >= end || !isalnum((unsigned char)p[1])) {
                    return p;
                }
            }
        }
        p++;
    }
    return NULL;
}

#ifdef TEST_BUILD
const char *find_italic_underscore(const char *start, size_t len) {
#else
static const char *find_italic_underscore(const char *start, size_t len) {
#endif
    const char *p = start + 1;
    const char *end = start + len;

    while (p < end) {
        if (p[0] == '_' && (p + 1 >= end || p[1] != '_')) {
            if ((size_t)(p - start) > 1) {
                /* Closing delimiter: next char must be end-of-string or non-alphanumeric */
                if (p + 1 >= end || !isalnum((unsigned char)p[1])) {
                    return p;
                }
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
            /* Skip if preceded by alphanumeric (intra-word star is not emphasis) */
            if (p > line && isalnum((unsigned char)p[-1])) {
                md_output_plain(pad, p, 1);
                p++;
            } else {
                md_render_italic_star(pad, &p, remaining, base_pair);
            }
        } else if (p[0] == '_') {
            /* Skip if preceded by alphanumeric (intra-word underscore is not emphasis) */
            if (p > line && isalnum((unsigned char)p[-1])) {
                md_output_plain(pad, p, 1);
                p++;
            } else {
                md_render_italic_underscore(pad, &p, remaining, base_pair);
            }
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

/* ============================================================================
 * Table detection and rendering
 * ============================================================================ */

int markdown_is_table_row(const char *line, size_t len) {
    size_t start = 0;
    size_t end = len;

    if (!line || len == 0) {
        return 0;
    }

    while (start < len && isspace((unsigned char)line[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)line[end - 1])) {
        end--;
    }

    if (end - start < 2) {
        return 0;
    }
    if (line[start] != '|' || line[end - 1] != '|') {
        return 0;
    }

    /* Must have at least one interior | (at least two columns) */
    for (size_t i = start + 1; i < end - 1; i++) {
        if (line[i] == '|') {
            return 1;
        }
    }
    return 0;
}

int markdown_is_table_separator(const char *line, size_t len) {
    size_t start = 0;
    size_t end = len;
    int has_dash = 0;

    if (!line || len == 0) {
        return 0;
    }

    while (start < len && isspace((unsigned char)line[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)line[end - 1])) {
        end--;
    }

    if (end - start < 3 || line[start] != '|' || line[end - 1] != '|') {
        return 0;
    }

    for (size_t i = start; i < end; i++) {
        char c = line[i];
        if (c != '|' && c != '-' && c != ':' && c != ' ') {
            return 0;
        }
        if (c == '-') {
            has_dash = 1;
        }
    }

    return has_dash ? 1 : 0;
}

/*
 * Split a table row into cells.  Returns number of cells found.
 * Each cell's text pointer and byte length are stored in cells/cell_lens.
 * Leading/trailing whitespace is trimmed from each cell.
 */
static size_t table_split_cells(const char *row, size_t len,
                                const char **cells, size_t *cell_lens,
                                size_t max_cells) {
    size_t n = 0;
    size_t start = 0;
    size_t i;

    if (len < 2) {
        return 0;
    }

    /* Skip leading | */
    if (row[0] == '|') {
        start = 1;
    }

    i = start;
    while (i < len && n < max_cells) {
        /* Find next | or end */
        size_t cell_start = i;
        while (i < len && row[i] != '|') {
            i++;
        }
        size_t cell_end = i;

        /* Trim trailing whitespace */
        while (cell_end > cell_start && isspace((unsigned char)row[cell_end - 1])) {
            cell_end--;
        }
        /* Trim leading whitespace */
        while (cell_start < cell_end && isspace((unsigned char)row[cell_start])) {
            cell_start++;
        }

        if (cell_end > cell_start) {
            cells[n] = row + cell_start;
            cell_lens[n] = cell_end - cell_start;
            n++;
        } else {
            /* Empty cell */
            cells[n] = NULL;
            cell_lens[n] = 0;
            n++;
        }

        if (i < len && row[i] == '|') {
            i++;
        }

        /* Skip trailing | at end of row */
        if (i == len - 1 && row[i] == '|') {
            break;
        }
    }

    return n;
}

/*
 * Compute display width of a UTF-8 string.
 * Simple implementation: count bytes, treating values >= 0x80 as
 * combining/narrow.  This is an approximation that works well enough
 * for most terminal content.
 */
static int cell_display_width(const char *text, size_t len) {
    int w = 0;
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            w++;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            w++;  /* 2-byte sequence */
            i += 2;
            if (i > len) break;
        } else if ((c & 0xF0) == 0xE0) {
            w++;  /* 3-byte sequence */
            i += 3;
            if (i > len) break;
        } else if ((c & 0xF8) == 0xF0) {
            w += 2;  /* 4-byte sequence (wide chars, emoji etc) */
            i += 4;
            if (i > len) break;
        } else {
            w++;
            i++;
        }
    }
    return w;
}

/* Maximum table dimensions to keep rendering bounded */
#define TABLE_MAX_ROWS 64
#define TABLE_MAX_COLS 16

/* Wrapping mode thresholds */
#define TABLE_WRAP_MIN_COL_WIDTH 8
#define TABLE_WRAP_MAX_COLS 6
#define TABLE_WRAP_MIN_SCREEN_WIDTH 40
#define TABLE_WRAP_MAX_WRAP_LINES 32
/* Per-column rendering overhead: space + cell + space + | = 3 chars beyond col width */
#define TABLE_WRAP_PER_COL_OVERHEAD 3

/*
 * Compute the display width of a UTF-8 byte sequence using mbrtowc/wcwidth.
 * Returns the number of terminal columns the text occupies (capped at max_width).
 */
static int cell_display_width_mb(const char *text, size_t len, int max_width) {
    int w = 0;
    size_t i = 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (i < len && w < max_width) {
        wchar_t wc;
        size_t char_bytes = mbrtowc(&wc, text + i, len - i, &state);
        if (char_bytes == 0) {
            break;
        } else if (char_bytes == (size_t)-1 || char_bytes == (size_t)-2) {
            i++;
            w++;
        } else {
            int char_width = wcwidth(wc);
            if (char_width < 0) {
                char_width = 1;
            }
            if (w + char_width > max_width) {
                break;
            }
            i += char_bytes;
            w += char_width;
        }
    }
    return i > 0 ? w : 0;
}

/*
 * Find the byte length of text that fits within max_display_width columns.
 * Uses mbrtowc/wcwidth for accurate UTF-8 display width calculation.
 */
static size_t cell_wrap_point(const char *text, size_t text_len, int max_display_width) {
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
 * Get the n-th wrapped line of a cell (0-indexed).
 * Returns 1 and sets *out_start and *out_len on success, 0 if beyond end.
 */
static int cell_get_wrapped_line(const char *text, size_t text_len, int max_width,
                                 int line_idx, const char **out_start, size_t *out_len) {
    const char *p = text;
    size_t remaining = text_len;
    int current_line = 0;

    while (remaining > 0 && current_line < line_idx) {
        size_t chunk = cell_wrap_point(p, remaining, max_width);
        p += chunk;
        remaining -= chunk;
        current_line++;
    }

    if (remaining == 0) {
        *out_start = NULL;
        *out_len = 0;
        return 0;
    }

    size_t chunk = cell_wrap_point(p, remaining, max_width);
    *out_start = p;
    *out_len = chunk;
    return 1;
}

/*
 * Count how many wrapped lines a cell will produce at the given column width.
 */
static int cell_wrap_count(const char *text, size_t text_len, int max_width) {
    if (!text || text_len == 0 || max_width <= 0) {
        return 1;
    }
    int count = 0;
    const char *p = text;
    size_t remaining = text_len;
    while (remaining > 0) {
        size_t chunk = cell_wrap_point(p, remaining, max_width);
        p += chunk;
        remaining -= chunk;
        count++;
        if (count >= TABLE_WRAP_MAX_WRAP_LINES) {
            break;
        }
    }
    return count;
}

/*
 * Decide whether wrapped table rendering is viable.
 * Returns 1 if wrapping should be used, 0 to fall back to fixed-width mode.
 */
static int table_should_wrap(size_t num_cols, int pad_width,
                             int left_border_width, const int *max_content_widths) {
    /* Must have 2-6 columns */
    if (num_cols < 2 || num_cols > TABLE_WRAP_MAX_COLS) {
        return 0;
    }

    /* Screen must be wide enough */
    if (pad_width < TABLE_WRAP_MIN_SCREEN_WIDTH) {
        return 0;
    }

    /* Left border width: if non-NULL border string, measure it; else 1 for '|' */
    int left_bw = (left_border_width > 0) ? left_border_width : 1;

    /* Available width for column content */
    int avail = pad_width - left_bw - ((int)num_cols * TABLE_WRAP_PER_COL_OVERHEAD);

    /* Check if table would overflow without wrapping */
    size_t total_natural_width = 0;
    for (size_t j = 0; j < num_cols; j++) {
        int w = max_content_widths[j] < 3 ? 3 : max_content_widths[j];
        total_natural_width += (size_t)(w + TABLE_WRAP_PER_COL_OVERHEAD);
    }
    int natural_total = left_bw + (int)total_natural_width;
    if (natural_total <= pad_width) {
        /* Table fits naturally; no need to wrap */
        return 0;
    }

    /* Check if wrapping is viable: each column must get at least min width */
    if (avail < (int)num_cols * TABLE_WRAP_MIN_COL_WIDTH) {
        return 0;
    }

    return 1;
}

/*
 * Distribute available width among columns.
 * First assigns min width, then distributes extra proportionally to content width.
 */
static void table_distribute_widths(int *col_widths, size_t num_cols, int pad_width,
                                    int left_border_width, const int *max_content_widths) {
    int left_bw = (left_border_width > 0) ? left_border_width : 1;
    int avail = pad_width - left_bw - ((int)num_cols * TABLE_WRAP_PER_COL_OVERHEAD);

    /* Minimum guaranteed */
    int used = 0;
    for (size_t j = 0; j < num_cols; j++) {
        col_widths[j] = TABLE_WRAP_MIN_COL_WIDTH;
        used += TABLE_WRAP_MIN_COL_WIDTH;
    }

    int extra = avail - used;
    if (extra <= 0) {
        return;
    }

    /* Compute total content width for proportional distribution */
    int total_content = 0;
    for (size_t j = 0; j < num_cols; j++) {
        total_content += max_content_widths[j];
    }

    if (total_content > 0) {
        /* Distribute proportionally */
        int assigned = 0;
        for (size_t j = 0; j < num_cols - 1; j++) {
            int share = (max_content_widths[j] * extra) / total_content;
            col_widths[j] += share;
            assigned += share;
        }
        /* Last column gets remainder to avoid rounding errors */
        col_widths[num_cols - 1] += (extra - assigned);
    } else {
        /* Equal distribution if all content widths are 0 */
        int each = extra / (int)num_cols;
        int remainder = extra - each * (int)num_cols;
        for (size_t j = 0; j < num_cols; j++) {
            col_widths[j] += each;
            if ((int)j < remainder) {
                col_widths[j]++;
            }
        }
    }
}

/*
 * Render a single wrapped row.
 * Each cell's text is wrapped at col_widths[j]; all cells align on the same
 * visual line boundaries (the row may span multiple pad lines).
 */
static void table_render_wrapped_row(TUIState *tui, WINDOW *pad,
                                     const char **cells, const size_t *cell_lens,
                                     size_t num_cols, const int *col_widths,
                                     int row_idx, int base_pair, int edge_pair,
                                     const char *left_border) {
    size_t j;
    int max_lines = 1;

    /* Determine max wrapped lines for this row */
    for (j = 0; j < num_cols; j++) {
        int lines;
        if (cells[j] && cell_lens[j] > 0) {
            lines = cell_wrap_count(cells[j], cell_lens[j], col_widths[j]);
        } else {
            lines = 1;
        }
        if (lines > max_lines) {
            max_lines = lines;
        }
    }

    if (max_lines > TABLE_WRAP_MAX_WRAP_LINES) {
        max_lines = TABLE_WRAP_MAX_WRAP_LINES;
    }

    /* Render each visual line */
    for (int line = 0; line < max_lines; line++) {
        /* Left border */
        if (edge_pair > 0 && has_colors()) {
            wattron(pad, COLOR_PAIR((unsigned)edge_pair));
        }
        if (left_border) {
            waddstr(pad, left_border);
        } else {
            waddch(pad, '|');
        }

        for (j = 0; j < num_cols; j++) {
            const char *seg = NULL;
            size_t seg_len = 0;
            int seg_w = 0;

            if (cells[j] && cell_lens[j] > 0) {
                if (cell_get_wrapped_line(cells[j], cell_lens[j], col_widths[j],
                                         line, &seg, &seg_len) && seg_len > 0) {
                    seg_w = cell_display_width_mb(seg, seg_len, col_widths[j]);
                }
            }

            int pad_w = col_widths[j] - seg_w;
            if (pad_w < 0) {
                pad_w = 0;
            }

            waddch(pad, ' ');

            if (row_idx == 0 && base_pair > 0 && has_colors()) {
                wattron(pad, A_BOLD);
            }

            if (seg && seg_len > 0) {
                markdown_render_inline(tui, seg, seg_len, base_pair);
            }

            if (row_idx == 0 && base_pair > 0 && has_colors()) {
                wattroff(pad, A_BOLD);
            }

            /* Pad to column width */
            while (pad_w > 0) {
                waddch(pad, ' ');
                pad_w--;
            }

            waddch(pad, ' ');
            waddch(pad, '|');
        }

        if (edge_pair > 0 && has_colors()) {
            wattroff(pad, COLOR_PAIR((unsigned)edge_pair));
        }

        waddch(pad, '\n');
    }
}

/*
 * Render a separator line at the given column widths.
 */
static void table_render_separator(WINDOW *pad, size_t num_cols,
                                   const int *col_widths, int edge_pair,
                                   const char *left_border) {
    if (edge_pair > 0 && has_colors()) {
        wattron(pad, COLOR_PAIR((unsigned)edge_pair));
    }
    if (left_border) {
        waddstr(pad, left_border);
    } else {
        waddch(pad, '|');
    }
    for (size_t j = 0; j < num_cols; j++) {
        waddch(pad, '-');
        for (int w = 0; w < col_widths[j]; w++) {
            waddch(pad, '-');
        }
        waddch(pad, '-');
        waddch(pad, '|');
    }
    if (edge_pair > 0 && has_colors()) {
        wattroff(pad, COLOR_PAIR((unsigned)edge_pair));
    }
    waddch(pad, '\n');
}

void markdown_render_table(TUIState *tui, const char **rows, const size_t *row_lens,
                           size_t num_rows, int base_pair,
                           const char *left_border, int left_border_pair,
                           int pad_width) {
    WINDOW *pad;
    size_t display_rows[TABLE_MAX_ROWS];
    size_t num_display = 0;
    const char *cells[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    size_t cell_lens[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    size_t col_counts[TABLE_MAX_ROWS];
    size_t num_cols = 0;
    int col_widths[TABLE_MAX_COLS];
    int max_content_widths[TABLE_MAX_COLS];
    size_t i, j;
    int edge_pair;

    if (!tui || !rows || num_rows == 0) {
        return;
    }

    pad = tui->wm.conv_pad;
    if (!pad) {
        return;
    }

    edge_pair = (left_border_pair > 0) ? left_border_pair : base_pair;

    if (num_rows > TABLE_MAX_ROWS) {
        num_rows = TABLE_MAX_ROWS;
    }

    /* Collect non-separator rows */
    for (i = 0; i < num_rows; i++) {
        if (!markdown_is_table_separator(rows[i], row_lens[i])) {
            if (num_display < TABLE_MAX_ROWS) {
                display_rows[num_display] = i;
                num_display++;
            }
        }
    }

    if (num_display == 0) {
        return;
    }

    /* Parse all cells */
    for (i = 0; i < num_display; i++) {
        size_t ri = display_rows[i];
        size_t nc = table_split_cells(rows[ri], row_lens[ri],
                                      cells[i], cell_lens[i], TABLE_MAX_COLS);
        col_counts[i] = nc;
        if (nc > num_cols) {
            num_cols = nc;
        }
    }

    if (num_cols == 0) {
        return;
    }
    if (num_cols > TABLE_MAX_COLS) {
        num_cols = TABLE_MAX_COLS;
    }

    /* Calculate max content widths for each column */
    memset(max_content_widths, 0, sizeof(max_content_widths));
    for (i = 0; i < num_display; i++) {
        for (j = 0; j < col_counts[i] && j < num_cols; j++) {
            int w;
            if (cells[i][j] && cell_lens[i][j] > 0) {
                w = cell_display_width(cells[i][j], cell_lens[i][j]);
            } else {
                w = 0;
            }
            if (w > max_content_widths[j]) {
                max_content_widths[j] = w;
            }
        }
    }

    /* Compute left border display width for wrapping decisions */
    int left_bw = left_border ? cell_display_width(left_border, strlen(left_border)) : 0;

    /* Decide whether to use wrapping mode */
    if (table_should_wrap(num_cols, pad_width, left_bw, max_content_widths)) {
        /* --- Wrapped rendering mode --- */
        table_distribute_widths(col_widths, num_cols, pad_width, left_bw,
                                max_content_widths);

        /* Render rows with wrapping */
        for (i = 0; i < num_display; i++) {
            /* Build per-cell pointer array for this row */
            const char *row_cells[TABLE_MAX_COLS];
            size_t row_cell_lens[TABLE_MAX_COLS];
            size_t nc = col_counts[i];
            if (nc > num_cols) nc = num_cols;
            for (j = 0; j < nc; j++) {
                row_cells[j] = cells[i][j];
                row_cell_lens[j] = cell_lens[i][j];
            }
            /* Pad missing columns with empty cells */
            for (j = nc; j < num_cols; j++) {
                row_cells[j] = NULL;
                row_cell_lens[j] = 0;
            }

            table_render_wrapped_row(tui, pad, row_cells, row_cell_lens,
                                     num_cols, col_widths,
                                     (int)i, base_pair, edge_pair, left_border);

            /* Separator line after header */
            if (i == 0 && num_display > 1) {
                table_render_separator(pad, num_cols, col_widths,
                                       edge_pair, left_border);
            }
        }

        return;
    }

    /* --- Traditional fixed-width rendering (fallback) --- */

    /* Calculate column widths */
    memset(col_widths, 0, sizeof(col_widths));
    for (i = 0; i < num_display; i++) {
        for (j = 0; j < col_counts[i] && j < num_cols; j++) {
            int w;
            if (cells[i][j] && cell_lens[i][j] > 0) {
                w = cell_display_width(cells[i][j], cell_lens[i][j]);
            } else {
                w = 0;
            }
            if (w > col_widths[j]) {
                col_widths[j] = w;
            }
        }
    }

    /* Ensure minimum column width of 3 */
    for (j = 0; j < num_cols; j++) {
        if (col_widths[j] < 3) {
            col_widths[j] = 3;
        }
    }

    /* Render rows */
    for (i = 0; i < num_display; i++) {
        /* Left border */
        if (edge_pair > 0 && has_colors()) {
            wattron(pad, COLOR_PAIR((unsigned)edge_pair));
        }
        if (left_border) {
            waddstr(pad, left_border);
        } else {
            waddch(pad, '|');
        }

        for (j = 0; j < col_counts[i] && j < num_cols; j++) {
            const char *cell_text;
            size_t cell_len;
            int cell_w, pad_w;

            cell_text = cells[i][j];
            cell_len = cell_lens[i][j];
            if (!cell_text || cell_len == 0) {
                cell_text = "";
                cell_len = 0;
            }

            cell_w = cell_display_width(cell_text, cell_len);
            pad_w = col_widths[j] - cell_w;

            waddch(pad, ' ');

            if (i == 0 && base_pair > 0 && has_colors()) {
                /* Header row: bold */
                wattron(pad, A_BOLD);
            }

            if (cell_len > 0) {
                markdown_render_inline(tui, cell_text, cell_len, base_pair);
            }

            if (i == 0 && base_pair > 0 && has_colors()) {
                wattroff(pad, A_BOLD);
            }

            /* Pad to column width */
            if (pad_w > 0) {
                while (pad_w > 0) {
                    waddch(pad, ' ');
                    pad_w--;
                }
            }
            waddch(pad, ' ');
            waddch(pad, '|');
        }

        if (edge_pair > 0 && has_colors()) {
            wattroff(pad, COLOR_PAIR((unsigned)edge_pair));
        }

        waddch(pad, '\n');

        /* Separator line after header */
        if (i == 0 && num_display > 1) {
            if (edge_pair > 0 && has_colors()) {
                wattron(pad, COLOR_PAIR((unsigned)edge_pair));
            }
            if (left_border) {
                waddstr(pad, left_border);
            } else {
                waddch(pad, '|');
            }
            for (j = 0; j < num_cols; j++) {
                int w;
                waddch(pad, '-');
                for (w = 0; w < col_widths[j]; w++) {
                    waddch(pad, '-');
                }
                waddch(pad, '-');
                waddch(pad, '|');
            }
            if (edge_pair > 0 && has_colors()) {
                wattroff(pad, COLOR_PAIR((unsigned)edge_pair));
            }
            waddch(pad, '\n');
        }
    }
}
