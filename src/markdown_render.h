/*
 * Lightweight markdown renderer for ncurses TUI.
 *
 * Provides inline and block-level markdown formatting for AI assistant
 * responses without external dependencies.
 */

#ifndef MARKDOWN_RENDER_H
#define MARKDOWN_RENDER_H

#include <stddef.h>
#include <stdint.h>

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
 *
 * When the table would overflow the pad, and conditions are favorable
 * (2-6 columns, viewport >= 40 chars), columns are auto-wrapped to fit.
 * Otherwise falls back to traditional fixed-width rendering.
 *
 * left_border:      if non-NULL, drawn as the left edge instead of "|".
 * left_border_pair: COLOR_PAIR for left_border (0 to use base_pair).
 * pad_width:        total width available for the table (from pad dimensions).
 */
void markdown_render_table(TUIState *tui, const char **rows, const size_t *row_lens,
                           size_t num_rows, int base_pair,
                           const char *left_border, int left_border_pair,
                           int pad_width);

/*
 * Markdown pre-parse cache
 *
 * Pre-computes line boundaries and classifies each line (header, code fence,
 * table row, etc) so that conversation pad rebuilds can skip re-scanning text
 * and re-calling all the line-type detection functions.
 *
 * Attached to ConversationEntry::md_cache.  Freed when the entry is cleared
 * or its text changes (streaming append).
 */

typedef enum {
    MD_LINE_REGULAR = 0,
    MD_LINE_EMPTY,
    MD_LINE_HEADER,
    MD_LINE_CODE_OPEN,
    MD_LINE_CODE_CLOSE,
    MD_LINE_CODE_CONTENT,
    MD_LINE_TABLE_ROW,
    MD_LINE_TABLE_SEP,
    MD_LINE_HRULE,
    MD_LINE_LIST_ITEM,
    MD_LINE_BLOCKQUOTE,
} MDParsedLineType;

typedef struct {
    MDParsedLineType type;
    int      header_level;  /* 1-6 for headers, 0 otherwise */
    char     list_type;      /* '-','*','+','1' for lists, 0 otherwise */
    int      list_number;    /* for ordered lists */
    uint32_t line_offset;    /* byte offset of this line within source_text */
    uint16_t line_len;       /* length of this line (excluding newline) */
    uint16_t content_off;    /* byte offset past prefix (## , > , -  etc.) */
} MDParsedLine;

typedef struct {
    MDParsedLine *lines;
    int           line_count;
    int           capacity;
    const char   *source_text;  /* pointer to text we parsed; NULL after free */
} MDParsedDoc;

/* Pre-parse markdown text into line descriptors.  Returns NULL if text is
 * empty, allocation fails, or the text is too long for 16-bit offsets. */
MDParsedDoc *markdown_cache_parse(const char *text);

/* Free a pre-parsed document.  Safe to call with NULL. */
void markdown_cache_free(MDParsedDoc *doc);

#endif /* MARKDOWN_RENDER_H */
