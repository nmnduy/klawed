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

#endif /* MARKDOWN_RENDER_H */
