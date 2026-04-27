/*
 * Lightweight markdown renderer for ncurses TUI.
 *
 * Provides inline and block-level markdown formatting for AI assistant
 * responses without external dependencies.
 */

#ifndef MARKDOWN_RENDER_H
#define MARKDOWN_RENDER_H

#include <stddef.h>

struct TUIStateStruct;
typedef struct TUIStateStruct TUIState;

/*
 * Inline markdown formatting
 *
 * Renders a single line of text with inline markdown converted to
 * ncurses attributes.  Control characters (**, *, `, ~~) are consumed
 * and not drawn.
 *
 * Supported inline elements:
 *   **bold**       -> A_BOLD
 *   __bold__       -> A_BOLD
 *   *italic*       -> A_ITALIC
 *   _italic_       -> A_ITALIC
 *   `code`         -> A_DIM
 *   ~~strikethrough~~ -> A_DIM
 *
 * base_pair: default ncurses COLOR_PAIR() value (0 for none).
 */
void markdown_render_inline(TUIState *tui, const char *line, size_t len, int base_pair);

/*
 * Block-level markdown detection (single logical line)
 */

/* Returns header level 1-6, or 0 if not a header. */
int markdown_header_level(const char *line, size_t len);

/*
 * Returns  1 if line is an opening code fence (```),
 *         -1 if line is a closing code fence,
 *          0 otherwise.
 */
int markdown_code_fence(const char *line, size_t len);

/* Returns 1 if line is a horizontal rule, 0 otherwise. */
int markdown_hrule(const char *line, size_t len);

/*
 * Detect list item.  Returns bullet character:
 *   '-', '*', '+'  for unordered lists
 *   '1'            for ordered lists
 *   0              if not a list item
 *
 * prefix_len receives the byte length of the list marker plus trailing
 * whitespace.  For ordered lists, number receives the list number.
 */
char markdown_list_item(const char *line, size_t len, size_t *prefix_len, int *number);

/*
 * Detect blockquote.  Returns 1 if line starts with '> ', 0 otherwise.
 * prefix_len receives the byte length of the quote marker plus space.
 */
int markdown_blockquote(const char *line, size_t len, size_t *prefix_len);

/*
 * Table detection and rendering
 */

/* Returns 1 if line is a table row (starts and ends with | after whitespace trim). */
int markdown_is_table_row(const char *line, size_t len);

/* Returns 1 if line is a table separator row (only |, -, :, and spaces). */
int markdown_is_table_separator(const char *line, size_t len);

/*
 * Render a multi-line table.  rows/row_lens arrays contain num_rows entries.
 * The first row is treated as header, any separator rows are skipped, and
 * remaining rows are data.  Columns are padded for even-width display with
 * text rendered through the inline markdown formatter.
 */
void markdown_render_table(TUIState *tui, const char **rows, const size_t *row_lens,
                           size_t num_rows, int base_pair);

#endif /* MARKDOWN_RENDER_H */
