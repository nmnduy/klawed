/*
 * TUI Virtual Scrolling - Implementation
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_virtual_scroll.h"
#include "tui_render.h"
#include "tui_conversation.h"
#include "window_manager.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include <wchar.h>
#include <locale.h>

/* ============================================================================
 * UTF-8 display width (lightweight approximation)
 * ============================================================================ */

static int utf8_display_width_simple(const char *str) {
    if (!str || !*str) {
        return 0;
    }

    int width = 0;
    const unsigned char *p = (const unsigned char *)str;

    while (*p) {
        if (*p < 0x80) {
            width++;
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            width += 2;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            width += 2;
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            width += 2;
            p += 4;
        } else {
            width++;
            p++;
        }
    }

    return width;
}

/* ============================================================================
 * Height computation
 * ============================================================================ */

// Count wrapped lines for plain text at a given content width.
static int text_wrapped_height(const char *text, int content_width) {
    if (!text || !text[0] || content_width <= 0) {
        return 0;
    }

    int total_lines = 0;
    const char *p = text;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

        if (line_len == 0) {
            total_lines++; // explicit empty line
        } else {
            char *line = malloc(line_len + 1);
            if (line) {
                memcpy(line, p, line_len);
                line[line_len] = '\0';
                int display_width = utf8_display_width_simple(line);
                free(line);
                total_lines += (display_width + content_width - 1) / content_width;
            } else {
                total_lines += (int)((line_len + (size_t)content_width - 1) / (size_t)content_width);
            }
        }

        if (nl) {
            p = nl + 1;
        } else {
            break;
        }
    }

    return total_lines;
}

// Compute height of a single conversation entry.
int tui_virtual_entry_height(TUIState *tui, ConversationEntry *entry) {
    if (!tui || !entry) {
        return 0;
    }

    WINDOW *pad = tui->wm.conv_pad;
    if (!pad) {
        return 1;
    }

    int pad_height = 0, pad_width = 0;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;

    int content_width = pad_width;
    int prefix_display_width = 0;
    int extra_lines = 0;

    int is_user_message = (entry->prefix && strcmp(entry->prefix, tui_icon_user()) == 0);
    int is_assistant_message = (entry->prefix && strcmp(entry->prefix, tui_icon_assistant()) == 0);

    if (is_user_message) {
        // User message: blank line + "❯ " prefix + text + blank line
        prefix_display_width = utf8_display_width_simple("❯ ");
        extra_lines = 2; // blank line before and after
    } else if (is_assistant_message) {
        if (tui->response_style == RESPONSE_STYLE_BORDER) {
            // Border style: "│ " on every line
            prefix_display_width = utf8_display_width_simple("│ ");
        } else if (tui->response_style == RESPONSE_STYLE_CARET) {
            // Caret style: ">>> " on first line only
            prefix_display_width = utf8_display_width_simple(">>> ");
            extra_lines = 0;
        } else if (tui->response_style == RESPONSE_STYLE_ROBOT) {
            prefix_display_width = 0;
            extra_lines = 2; // "  ┬ ┬\n┌[◉_◉]┐\n"
        } else if (tui->response_style == RESPONSE_STYLE_CAT) {
            prefix_display_width = 0;
            extra_lines = 1; // "=^..^=\n"
        }
    } else if (entry->prefix && entry->prefix[0] != '\0') {
        // Other prefix: rendered once at start of entry
        prefix_display_width = utf8_display_width_simple(entry->prefix) + 1; // +1 for space
    }

    content_width -= prefix_display_width;
    if (content_width < 1) {
        content_width = 1;
    }

    int height = extra_lines;

    // Count text lines (with wrapping)
    if (entry->text && entry->text[0]) {
        height += text_wrapped_height(entry->text, content_width);
    }

    // If there's a prefix and text, they share the first line (prefix + text on same line)
    // UNLESS it's user message (❯ is on its own conceptual line but we counted it in prefix)
    // Actually for user messages, "❯ " and text are on the same line
    // For assistant caret, ">>> " and text are on the same line
    // For other prefixes, prefix and text are on the same line
    // So if both prefix and text exist, they share a line - no extra line needed

    // Ensure at least 1 line if there's any content
    if (height == 0 && (entry->prefix || (entry->text && entry->text[0]))) {
        height = 1;
    }

    return height;
}

/* ============================================================================
 * Bulk height recomputation
 * ============================================================================ */

void tui_virtual_recompute_heights(TUIState *tui) {
    if (!tui) {
        return;
    }

    tui->total_visual_lines = 0;

    for (int i = 0; i < tui->entries_count; i++) {
        tui->entries[i].virtual_line_start = tui->total_visual_lines;
        tui->entries[i].visual_height = tui_virtual_entry_height(tui, &tui->entries[i]);
        tui->total_visual_lines += tui->entries[i].visual_height;
    }

    LOG_DEBUG("[VS] Recomputed heights: %d entries, %d virtual lines",
              tui->entries_count, tui->total_visual_lines);
}

void tui_virtual_invalidate_heights(TUIState *tui) {
    if (!tui) {
        return;
    }
    // Heights will be recomputed on next render or scroll
    // For now, just set total to 0 as a sentinel; recompute_heights will fix it
    tui->total_visual_lines = 0;
}

// Update height for a single entry at a given index.
// Call after modifying entry->text. Adjusts total_visual_lines by the delta.
void tui_virtual_update_entry_height(TUIState *tui, int entry_idx) {
    if (!tui || entry_idx < 0 || entry_idx >= tui->entries_count) {
        return;
    }

    ConversationEntry *entry = &tui->entries[entry_idx];
    int old_height = entry->visual_height;
    int new_height = tui_virtual_entry_height(tui, entry);
    int delta = new_height - old_height;

    entry->visual_height = new_height;
    tui->total_visual_lines += delta;

    // Update virtual_line_start for all subsequent entries
    for (int i = entry_idx + 1; i < tui->entries_count; i++) {
        tui->entries[i].virtual_line_start += delta;
    }

    LOG_DEBUG("[VS] Updated entry %d height: %d -> %d (delta=%d, total=%d)",
              entry_idx, old_height, new_height, delta, tui->total_visual_lines);
}

/* ============================================================================
 * Entry lookup by virtual line
 * ============================================================================ */

int tui_virtual_find_entry_at_line(TUIState *tui, int virtual_line, int *offset_in_entry) {
    if (!tui || tui->entries_count == 0) {
        if (offset_in_entry) {
            *offset_in_entry = 0;
        }
        return 0;
    }

    if (virtual_line < 0) {
        if (offset_in_entry) {
            *offset_in_entry = 0;
        }
        return 0;
    }

    // If heights are stale, recompute first
    if (tui->total_visual_lines == 0 && tui->entries_count > 0) {
        tui_virtual_recompute_heights(tui);
    }

    int last_entry = tui->entries_count - 1;
    int last_start = tui->entries[last_entry].virtual_line_start;
    if (virtual_line >= last_start + tui->entries[last_entry].visual_height) {
        if (offset_in_entry) {
            *offset_in_entry = tui->entries[last_entry].visual_height - 1;
        }
        return last_entry;
    }

    // Binary search for the entry containing this virtual line
    int lo = 0;
    int hi = last_entry;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int entry_start = tui->entries[mid].virtual_line_start;
        int entry_end = entry_start + tui->entries[mid].visual_height;

        if (virtual_line < entry_start) {
            hi = mid - 1;
        } else if (virtual_line >= entry_end) {
            lo = mid + 1;
        } else {
            if (offset_in_entry) {
                *offset_in_entry = virtual_line - entry_start;
            }
            return mid;
        }
    }

    // Fallback: linear scan (shouldn't happen often)
    for (int i = 0; i < tui->entries_count; i++) {
        int entry_start = tui->entries[i].virtual_line_start;
        int entry_end = entry_start + tui->entries[i].visual_height;
        if (virtual_line >= entry_start && virtual_line < entry_end) {
            if (offset_in_entry) {
                *offset_in_entry = virtual_line - entry_start;
            }
            return i;
        }
    }

    if (offset_in_entry) {
        *offset_in_entry = 0;
    }
    return 0;
}

/* ============================================================================
 * Viewport rendering
 * ============================================================================ */

static void render_entry_at_pad_y(TUIState *tui, int entry_idx, int pad_y) {
    if (!tui || entry_idx < 0 || entry_idx >= tui->entries_count) {
        return;
    }

    ConversationEntry *entry = &tui->entries[entry_idx];

    // Position cursor at target line
    wmove(tui->wm.conv_pad, pad_y, 0);

    // Save/restore content_lines around render_entry_to_pad
    int saved_content_lines = window_manager_get_content_lines(&tui->wm);
    window_manager_set_content_lines(&tui->wm, pad_y);

    render_entry_to_pad(tui, entry->prefix, entry->text, entry->color_pair);

    window_manager_set_content_lines(&tui->wm, saved_content_lines);

    // Update pad_start_line for this entry
    entry->pad_start_line = pad_y;
}

void tui_virtual_render_viewport(TUIState *tui) {
    if (!tui || !tui->wm.conv_pad) {
        return;
    }

    // Ensure heights are up to date
    if (tui->total_visual_lines == 0 && tui->entries_count > 0) {
        tui_virtual_recompute_heights(tui);
    }

    int viewport_height = tui->wm.conv_viewport_height;
    if (viewport_height <= 0) {
        viewport_height = 1;
    }

    int scroll_offset = tui->wm.conv_scroll_offset;
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    // Clamp scroll to valid range
    int max_scroll = tui->total_visual_lines - viewport_height;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll_offset > max_scroll) {
        scroll_offset = max_scroll;
        tui->wm.conv_scroll_offset = scroll_offset;
    }

    // Ensure pad has enough capacity for all virtual lines
    if (tui->total_visual_lines > tui->wm.conv_pad_capacity) {
        window_manager_ensure_pad_capacity(&tui->wm, tui->total_visual_lines + viewport_height);
    }

    // Clear only the viewport region so prefresh works with the existing
    // pad coordinate system (scroll_offset is the physical pad row).
    int pad_h = 0, pad_w = 0;
    getmaxyx(tui->wm.conv_pad, pad_h, pad_w);
    (void)pad_h;
    for (int y = scroll_offset; y < scroll_offset + viewport_height && y < tui->wm.conv_pad_capacity; y++) {
        wmove(tui->wm.conv_pad, y, 0);
        wclrtoeol(tui->wm.conv_pad);
    }

    // Find first visible entry
    int offset_in_entry = 0;
    int entry_idx = tui_virtual_find_entry_at_line(tui, scroll_offset, &offset_in_entry);

    LOG_DEBUG("[VS] Render viewport: scroll=%d, entry=%d, offset=%d, total=%d",
              scroll_offset, entry_idx, offset_in_entry, tui->total_visual_lines);

    int viewport_end = scroll_offset + viewport_height;

    for (int i = entry_idx; i < tui->entries_count; i++) {
        ConversationEntry *entry = &tui->entries[i];
        if (entry->virtual_line_start >= viewport_end) {
            break;
        }

        entry->pad_start_line = entry->virtual_line_start;
        render_entry_at_pad_y(tui, i, entry->virtual_line_start);
    }

    // Update content lines to total virtual lines so scrollbar math works
    window_manager_set_content_lines(&tui->wm, tui->total_visual_lines);
}
