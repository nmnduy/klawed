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

void markdown_render_table(TUIState *tui, const char **rows, const size_t *row_lens,
                           size_t num_rows, int base_pair) {
    WINDOW *pad;
    size_t display_rows[TABLE_MAX_ROWS];
    size_t num_display = 0;
    const char *cells[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    size_t cell_lens[TABLE_MAX_ROWS][TABLE_MAX_COLS];
    size_t col_counts[TABLE_MAX_ROWS];
    size_t num_cols = 0;
    int col_widths[TABLE_MAX_COLS];
    size_t i, j;

    if (!tui || !rows || num_rows == 0) {
        return;
    }

    pad = tui->wm.conv_pad;
    if (!pad) {
        return;
    }

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
        if (base_pair > 0 && has_colors()) {
            wattron(pad, COLOR_PAIR((unsigned)base_pair));
        }
        waddch(pad, '|');

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
                /* waddnstr with spaces */
                while (pad_w > 0) {
                    waddch(pad, ' ');
                    pad_w--;
                }
            }
            waddch(pad, ' ');
            waddch(pad, '|');
        }

        if (base_pair > 0 && has_colors()) {
            wattroff(pad, COLOR_PAIR((unsigned)base_pair));
        }

        waddch(pad, '\n');

        /* Separator line after header */
        if (i == 0 && num_display > 1) {
            if (base_pair > 0 && has_colors()) {
                wattron(pad, COLOR_PAIR((unsigned)base_pair));
            }
            waddch(pad, '|');
            for (j = 0; j < num_cols; j++) {
                int w;
                waddch(pad, '-');
                for (w = 0; w < col_widths[j]; w++) {
                    waddch(pad, '-');
                }
                waddch(pad, '-');
                waddch(pad, '|');
            }
            if (base_pair > 0 && has_colors()) {
                wattroff(pad, COLOR_PAIR((unsigned)base_pair));
            }
            waddch(pad, '\n');
        }
    }
}
