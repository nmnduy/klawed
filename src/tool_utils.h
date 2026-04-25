/*
 * tool_utils.h - Helper utilities for tool argument summarization
 */

#ifndef TOOL_UTILS_H
#define TOOL_UTILS_H

#include <stddef.h>

struct ConversationState;

// Summarize a bash command for display purposes.
// - Writes a concise preview into `out` (NUL-terminated).
// - If the command begins with "cd <dir> &&" or "cd <dir>;" and <dir> is the
//   current working directory, the leading cd segment is stripped.
// - The output is truncated to fit `outsz` (including NUL). If truncation
//   occurs and there is room (outsz > 4), an ellipsis "..." is appended.
// Returns 0 on success.
int summarize_bash_command(const char *cmd, char *out, size_t outsz);

// Trim trailing whitespace from a string in-place.
// Modifies the original string by removing whitespace characters
// (space, tab, newline, carriage return, formfeed, vertical tab)
// from the end of the string.
void trim_trailing_whitespace(char *str);

// Securely free memory containing sensitive data
// Wipes the memory with zeros before freeing to prevent sensitive data
// from remaining in memory after deallocation
void secure_free(void *ptr, size_t size);

// Check if a tool is disabled via KLAWED_DISABLE_TOOLS environment variable.
// The env var should be a comma-separated list of tool names to disable.
// Example: KLAWED_DISABLE_TOOLS="UploadImage,Subagent"
// Returns 1 if the tool is disabled, 0 otherwise.
int is_tool_disabled(const char *tool_name);

// Set the process-wide runtime disabled tool list used by interactive
// settings. Pass NULL or an empty string to clear the override.
void set_runtime_disabled_tools(const char *disabled_tools);

// Check if a tool is disabled for the current runtime state. Uses the
// state-specific disabled tool list when available, otherwise falls back to
// KLAWED_DISABLE_TOOLS.
int is_tool_disabled_for_state(const char *tool_name, const struct ConversationState *state);

// Check whether streaming is enabled for the current runtime state. Uses the
// state-specific toggle when available, otherwise falls back to
// KLAWED_ENABLE_STREAMING.
int is_streaming_enabled(const struct ConversationState *state);

// Check whether the UploadImage tool should be exposed.
// Most providers do not support image uploads by default, so this tool is
// opt-in via KLAWED_ENABLE_UPLOAD_IMAGE=1 (or "true"/"yes").
// Returns 1 if enabled, 0 otherwise.
int is_upload_image_enabled(void);

#endif // TOOL_UTILS_H
