/*
 * TUI Conversation Management
 *
 * Manages conversation display entries including:
 * - Adding conversation lines
 * - Updating conversation content
 * - Message type detection
 * - Entry lifecycle management
 */

// Define feature test macros before any includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_conversation.h"
#include "tui.h"
#include "logger.h"
#include "window_manager.h"
#include "array_resize.h"
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <ncurses.h>
#include <limits.h>
#include <ctype.h>

// Initial capacity for conversation entries array
#define INITIAL_CONV_CAPACITY 1000

// Helper: Classify message type from prefix
MessageType tui_conversation_get_message_type(const char *prefix) {
    if (!prefix || prefix[0] == '\0') {
        return MSG_TYPE_EMPTY;
    }

    if (strcmp(prefix, "[User]") == 0) {
        return MSG_TYPE_USER;
    }
    if (strcmp(prefix, "[Assistant]") == 0) {
        return MSG_TYPE_ASSISTANT;
    }
    if (strcmp(prefix, "[System]") == 0 || strcmp(prefix, "[Error]") == 0 ||
        strcmp(prefix, "[Transcription]") == 0) {
        return MSG_TYPE_SYSTEM;
    }
    // Check for tools - must come after checking specific system prefixes
    // Matches "[Tool: ...]" or any tool name in brackets like "[Bash]", "[Read]", etc.
    if (prefix[0] == '[') {
        return MSG_TYPE_TOOL;
    }

    return MSG_TYPE_UNKNOWN;
}

// Helper: Add a conversation entry to the TUI state
static int add_conversation_entry(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    if (!tui) return -1;

    // Ensure capacity
    if (tui->entries_count >= tui->entries_capacity) {
        int new_capacity = tui->entries_capacity == 0 ? INITIAL_CONV_CAPACITY : tui->entries_capacity * 2;
        void *entries_ptr = (void *)tui->entries;
        size_t capacity = (size_t)tui->entries_capacity;
        if (array_ensure_capacity(&entries_ptr, &capacity, (size_t)new_capacity,
                                  sizeof(ConversationEntry), NULL) != 0) {
            LOG_ERROR("[TUI] Failed to allocate memory for conversation entries");
            return -1;
        }
        tui->entries = (ConversationEntry *)entries_ptr;
        tui->entries_capacity = (int)capacity;
    }

    // Allocate and copy strings
    ConversationEntry *entry = &tui->entries[tui->entries_count];
    entry->prefix = prefix ? strdup(prefix) : NULL;
    entry->text = text ? strdup(text) : NULL;
    entry->color_pair = color_pair;

    if ((prefix && !entry->prefix) || (text && !entry->text)) {
        free(entry->prefix);
        free(entry->text);
        LOG_ERROR("[TUI] Failed to allocate memory for conversation entry strings");
        return -1;
    }

    tui->entries_count++;
    return 0;
}

// Helper: Free all conversation entries
void tui_conversation_free_entries(TUIState *tui) {
    if (!tui || !tui->entries) return;

    for (int i = 0; i < tui->entries_count; i++) {
        free(tui->entries[i].prefix);
        free(tui->entries[i].text);
    }
    free(tui->entries);
    tui->entries = NULL;
    tui->entries_count = 0;
    tui->entries_capacity = 0;
}

// Add a conversation line to the display (public API implementation)
void tui_add_conversation_line(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    if (!tui || !tui->is_initialized) return;

    // Validate conversation pad exists (critical - prevent segfault)
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot add conversation line - conv_pad is NULL");
        return;
    }

    // IMPORTANT: Capture "at bottom" state BEFORE adding new content
    // This is needed because after content is added, max_scroll increases
    // and the scroll_offset (which was at bottom) will appear to be less than max_scroll
    int was_at_bottom = 0;
    if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        int scroll_offset = window_manager_get_scroll_offset(&tui->wm);
        int max_scroll = window_manager_get_max_scroll(&tui->wm);
        int content_lines = window_manager_get_content_lines(&tui->wm);

        if (content_lines == 0 || max_scroll <= 0) {
            // No content or everything fits in viewport
            was_at_bottom = 1;
        } else if (scroll_offset >= max_scroll - 1) {
            // Already at bottom (with 1-line tolerance for 98-100% range)
            was_at_bottom = 1;
        }
        LOG_DEBUG("[TUI] Pre-add scroll state: scroll_offset=%d, max_scroll=%d, was_at_bottom=%d",
                  scroll_offset, max_scroll, was_at_bottom);
    }

    // Check if we need to add spacing between different message types
    // Look at the most recent non-empty entry to determine if spacing is needed
    MessageType current_type = tui_conversation_get_message_type(prefix);
    MessageType previous_type = MSG_TYPE_UNKNOWN;

    // Find the most recent non-empty entry
    for (int i = tui->entries_count - 1; i >= 0; i--) {
        MessageType entry_type = tui_conversation_get_message_type(tui->entries[i].prefix);
        if (entry_type != MSG_TYPE_EMPTY) {
            previous_type = entry_type;
            break;
        }
    }

    // Add blank line if transitioning between different message types
    // (but not for empty lines or unknown types, and not if previous was empty/unknown)
    int should_add_spacing = 0;
    if (current_type != MSG_TYPE_EMPTY && current_type != MSG_TYPE_UNKNOWN &&
        previous_type != MSG_TYPE_EMPTY && previous_type != MSG_TYPE_UNKNOWN &&
        current_type != previous_type) {
        should_add_spacing = 1;
    }

    // Get pad dimensions for capacity estimation
    int pad_height, pad_width;
    getmaxyx(tui->wm.conv_pad, pad_height, pad_width);
    (void)pad_height;

    // Calculate how many lines the entries will take when wrapped
    int prefix_len = (prefix && prefix[0] != '\0') ? (int)strlen(prefix) + 1 : 0; // +1 for space
    int text_len = (text && text[0] != '\0') ? (int)strlen(text) : 0;

    // Estimate wrapped lines (conservative)
    int estimated_lines = 1; // At least 1 line for the entry
    if (should_add_spacing) {
        estimated_lines += 1; // Add one for the spacing line
    }

    if (text_len > 0) {
        // Count newlines in text (each newline is a line break)
        int newline_count = 0;
        for (int i = 0; i < text_len; i++) {
            if (text[i] == '\n') {
                newline_count++;
            }
        }

        // Each newline in text is definitely a line break
        // Text without newlines might wrap
        // Be conservative: assume worst-case wrapping
        estimated_lines += newline_count + ((prefix_len + text_len) / (pad_width / 2)) + 5;
    }

    // Ensure pad has enough capacity (centralized via WindowManager)
    int current_lines = window_manager_get_content_lines(&tui->wm);
    // Check for integer overflow before calculating needed capacity
    int needed_capacity;
    if (current_lines > INT_MAX - estimated_lines ||
        current_lines + estimated_lines > INT_MAX - 500) {
        LOG_ERROR("[TUI] Capacity calculation would overflow! current=%d, estimated=%d",
                 current_lines, estimated_lines);
        needed_capacity = INT_MAX;
    } else {
        needed_capacity = current_lines + estimated_lines + 500; // Increased safety buffer
    }

    if (needed_capacity > tui->wm.conv_pad_capacity) {
        if (window_manager_ensure_pad_capacity(&tui->wm, needed_capacity) != 0) {
            LOG_ERROR("[TUI] Failed to ensure pad capacity via WindowManager");
        }
    }

    // Double-check pad exists before writing
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot write to conversation - conv_pad is NULL");
        return;
    }

    // Insert blank line for spacing if needed
    if (should_add_spacing) {
        if (add_conversation_entry(tui, NULL, "", COLOR_PAIR_FOREGROUND) != 0) {
            LOG_ERROR("[TUI] Failed to add spacing entry");
            // Continue anyway - spacing is not critical
        } else {
            // Render the spacing line
            render_entry_to_pad(tui, NULL, "", COLOR_PAIR_FOREGROUND);
        }
    }

    // Add entry to conversation history
    if (add_conversation_entry(tui, prefix, text, color_pair) != 0) {
        LOG_ERROR("[TUI] Failed to add conversation entry");
        return;
    }

    // Render the actual entry
    int start_line = window_manager_get_content_lines(&tui->wm);
    if (render_entry_to_pad(tui, prefix, text, color_pair) != 0) {
        LOG_ERROR("[TUI] Failed to render entry to pad");
        return;
    }

    int cur_y = window_manager_get_content_lines(&tui->wm);
    LOG_DEBUG("[TUI] Added line, total_lines now %d (estimated %d, actual %d)",
              cur_y, estimated_lines, cur_y - start_line);

    // Auto-scroll logic:
    // - In INSERT mode: always auto-scroll
    // - In NORMAL/COMMAND mode: auto-scroll only if we WERE at 98-100% scroll height
    //   BEFORE content was added (using was_at_bottom captured earlier)
    if (tui->mode == TUI_MODE_INSERT) {
        window_manager_scroll_to_bottom(&tui->wm);
    } else if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        // Use the was_at_bottom state captured BEFORE content was added
        if (was_at_bottom) {
            window_manager_scroll_to_bottom(&tui->wm);
            LOG_DEBUG("[TUI] Auto-scroll: scrolling to bottom (was_at_bottom=1)");
        } else {
            LOG_DEBUG("[TUI] Auto-scroll: not scrolling (was_at_bottom=0)");
        }
    }
    window_manager_refresh_conversation(&tui->wm);

    if (tui->wm.status_height > 0) {
        // Call render_status_window (declared in tui.h)
        render_status_window(tui);
    }

    // Redraw input window to ensure it stays visible
    if (tui->wm.input_win) {
        touchwin(tui->wm.input_win);
        wrefresh(tui->wm.input_win);
    }

    // Re-render file search popup if active (must be on top of conversation)
    if (tui->mode == TUI_MODE_FILE_SEARCH && tui->file_search.is_active) {
        file_search_render(&tui->file_search);
    }
}


// Update the last conversation line (for streaming responses)
void tui_update_last_conversation_line(TUIState *tui, const char *text) {
    if (!tui || !tui->is_initialized || !text) return;

    // Validate conversation pad exists
    if (!tui->wm.conv_pad) {
        LOG_ERROR("[TUI] Cannot update conversation line - conv_pad is NULL");
        return;
    }

    // IMPORTANT: Capture "at bottom" state BEFORE adding new content
    // This is needed because after content is added, max_scroll increases
    // and the scroll_offset (which was at bottom) will appear to be less than max_scroll
    int was_at_bottom = 0;
    if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        int scroll_offset = window_manager_get_scroll_offset(&tui->wm);
        int max_scroll = window_manager_get_max_scroll(&tui->wm);
        int content_lines = window_manager_get_content_lines(&tui->wm);

        if (content_lines == 0 || max_scroll <= 0) {
            // No content or everything fits in viewport
            was_at_bottom = 1;
        } else if (scroll_offset >= max_scroll - 1) {
            // Already at bottom (with 1-line tolerance for 98-100% range)
            was_at_bottom = 1;
        }
        LOG_DEBUG("[TUI] Pre-update scroll state: scroll_offset=%d, max_scroll=%d, was_at_bottom=%d",
                  scroll_offset, max_scroll, was_at_bottom);
    }

    // Update the last entry in the conversation history
    if (tui->entries_count > 0) {
        ConversationEntry *last_entry = &tui->entries[tui->entries_count - 1];

        // Append new text to the existing text
        size_t old_len = last_entry->text ? strlen(last_entry->text) : 0;
        size_t new_len = strlen(text);
        char *new_text = realloc(last_entry->text, old_len + new_len + 1);
        if (new_text) {
            if (old_len == 0) {
                new_text[0] = '\0';
            }
            strlcat(new_text, text, old_len + new_len + 1);
            last_entry->text = new_text;

            // Just append to the end of the pad (simple approach)
            // Get current cursor position
            int cur_y, cur_x;
            getyx(tui->wm.conv_pad, cur_y, cur_x);

            // If we're at the beginning of a line and there's a prefix,
            // we need to handle it differently
            if (cur_x == 0 && last_entry->prefix && last_entry->prefix[0] != '\0') {
                // We shouldn't get here in streaming mode
                LOG_WARN("[TUI] Streaming update but at start of line");
                return;
            }

            // Write the new text at current position
            waddstr(tui->wm.conv_pad, text);

            // Update content lines
            getyx(tui->wm.conv_pad, cur_y, cur_x);
            (void)cur_x;
            window_manager_set_content_lines(&tui->wm, cur_y);
        }
    } else {
        // No entries exist - create a new one
        add_conversation_entry(tui, "", text, COLOR_PAIR_ASSISTANT);
    }

    // Auto-scroll logic:
    // - In INSERT mode: always auto-scroll
    // - In NORMAL/COMMAND mode: auto-scroll only if we WERE at 98-100% scroll height
    //   BEFORE content was added (using was_at_bottom captured earlier)
    if (tui->mode == TUI_MODE_INSERT) {
        window_manager_scroll_to_bottom(&tui->wm);
    } else if (tui->mode == TUI_MODE_NORMAL || tui->mode == TUI_MODE_COMMAND) {
        // Use the was_at_bottom state captured BEFORE content was added
        if (was_at_bottom) {
            window_manager_scroll_to_bottom(&tui->wm);
            LOG_DEBUG("[TUI] Auto-scroll (update): scrolling to bottom (was_at_bottom=1)");
        } else {
            LOG_DEBUG("[TUI] Auto-scroll (update): not scrolling (was_at_bottom=0)");
        }
    }
    window_manager_refresh_conversation(&tui->wm);

    // Re-render file search popup if active (must be on top of conversation)
    if (tui->mode == TUI_MODE_FILE_SEARCH && tui->file_search.is_active) {
        file_search_render(&tui->file_search);
    }
}

// Infer color pair from message prefix
TUIColorPair tui_conversation_infer_color_from_prefix(const char *prefix) {
    if (!prefix) {
        return COLOR_PAIR_DEFAULT;
    }
    if (strstr(prefix, "User")) {
        return COLOR_PAIR_USER;
    }
    if (strstr(prefix, "Assistant")) {
        return COLOR_PAIR_ASSISTANT;
    }
    if (strstr(prefix, "Tool")) {
        return COLOR_PAIR_TOOL;
    }
    if (strstr(prefix, "Error")) {
        return COLOR_PAIR_ERROR;
    }
    if (strstr(prefix, "Diff")) {
        return COLOR_PAIR_STATUS;
    }
    if (strstr(prefix, "System")) {
        return COLOR_PAIR_STATUS;
    }
    if (strstr(prefix, "Prompt")) {
        return COLOR_PAIR_PROMPT;
    }
    // Heuristic: any other bracketed role tag like "[Bash]", "[Read]" is a tool
    if (prefix[0] == '[') {
        const char *close = strchr(prefix, ']');
        if (close && close > prefix + 1) {
            // Extract inner label and compare against known non-tool roles
            size_t len = (size_t)(close - (prefix + 1));
            char buf[32];
            if (len >= sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, prefix + 1, len);
            buf[len] = '\0';
            // Normalize to lowercase for comparison
            for (size_t i = 0; i < len; i++) {
                if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
            }
            if (strcmp(buf, "user") != 0 &&
                strcmp(buf, "assistant") != 0 &&
                strcmp(buf, "error") != 0 &&
                strcmp(buf, "system") != 0 &&
                strcmp(buf, "status") != 0 &&
                strcmp(buf, "prompt") != 0) {
                return COLOR_PAIR_TOOL;
            }
        }
    }
    return COLOR_PAIR_DEFAULT;
}
