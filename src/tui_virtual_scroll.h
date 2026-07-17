/*
 * TUI Virtual Scrolling
 *
 * Provides virtual viewport rendering for the conversation panel.
 * Instead of rendering all conversation history to an ever-growing ncurses
 * pad, we compute virtual line heights for each entry and only render
 * the entries visible in the current viewport.
 */

#ifndef TUI_VIRTUAL_SCROLL_H
#define TUI_VIRTUAL_SCROLL_H

#include "tui.h"

// Compute the visual height of a single entry at the current pad width.
// Returns the number of screen lines the entry would occupy.
int tui_virtual_entry_height(TUIState *tui, ConversationEntry *entry);

// Recompute visual heights for all entries and update total_virtual_lines.
void tui_virtual_recompute_heights(TUIState *tui);

// Find the entry index that contains the given virtual line.
// Returns entry index, and sets *offset_in_entry to the line offset within
// that entry (0 if the line starts at the entry boundary).
// If virtual_line is past all content, returns the last entry.
int tui_virtual_find_entry_at_line(TUIState *tui, int virtual_line, int *offset_in_entry);

// Render visible entries to the pad for the current scroll offset.
// Only entries overlapping the viewport [scroll_offset, scroll_offset + viewport_height)
// are rendered. This is O(viewport_height) instead of O(entries_count).
void tui_virtual_render_viewport(TUIState *tui);

// Mark virtual line heights as dirty (call after adding/updating entries).
void tui_virtual_invalidate_heights(TUIState *tui);

// Update height for a single entry after its text changes.
// Adjusts total_visual_lines and virtual_line_start for subsequent entries.
void tui_virtual_update_entry_height(TUIState *tui, int entry_idx);

// Get total virtual lines.
static inline int tui_virtual_total_lines(TUIState *tui) {
    return tui ? tui->total_visual_lines : 0;
}

#endif /* TUI_VIRTUAL_SCROLL_H */
