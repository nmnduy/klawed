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

// ============================================================================
// Tool Output Connector (Tree Drawing) Functions
// ============================================================================

// Extract tool name from tool prefix
// Format: "● ToolName" (● is UTF-8: 0xE2 0x97 0x8F)
// Returns allocated string with tool name, or NULL if not a tool prefix
// Caller must free the returned string
char* tui_conversation_extract_tool_name(const char *prefix);

// Check if a prefix is a tool message (starts with ●)
// Returns 1 if tool message, 0 otherwise
int tui_conversation_is_tool_message(const char *prefix);

// Determine the display prefix for a tool message
// If same tool as last output, returns "└─"
// Otherwise returns the full prefix
// Updates last_tool_name in TUIState
// Returns the prefix to use (may be the input prefix or "└─")
// Note: The returned string is either a static "└─" or the input prefix
// Caller should not free the returned string
const char* tui_conversation_get_tool_display_prefix(TUIState *tui, const char *prefix);

// Reset tool tracking state
// Should be called when conversation is cleared or on message type change
void tui_conversation_reset_tool_tracking(TUIState *tui);

#endif // TUI_CONVERSATION_H
