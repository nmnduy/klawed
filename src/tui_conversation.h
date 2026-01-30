/*
 * TUI Conversation Management
 *
 * Manages conversation display entries including:
 * - Adding conversation lines
 * - Updating conversation content
 * - Message type detection
 * - Entry lifecycle management
 */

#ifndef TUI_CONVERSATION_H
#define TUI_CONVERSATION_H

#include "tui.h"

// Forward declarations
typedef struct TUIStateStruct TUIState;

// Message type categories for spacing logic
typedef enum {
    MSG_TYPE_UNKNOWN = 0,
    MSG_TYPE_USER,
    MSG_TYPE_ASSISTANT,
    MSG_TYPE_TOOL,
    MSG_TYPE_SYSTEM,
    MSG_TYPE_EMPTY
} MessageType;

// Add a conversation entry to the TUI state
// Returns 0 on success, -1 on failure
int tui_conversation_add_entry(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair);

// Free all conversation entries
void tui_conversation_free_entries(TUIState *tui);

// Classify message type from prefix
MessageType tui_conversation_get_message_type(const char *prefix);

// Infer color pair from message prefix
// Returns appropriate TUIColorPair based on prefix content
TUIColorPair tui_conversation_infer_color_from_prefix(const char *prefix);

#endif // TUI_CONVERSATION_H
