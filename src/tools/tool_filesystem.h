/*
 * tool_filesystem.h - Filesystem tool implementations (Read, Write, Edit, MultiEdit, Glob)
 */

#ifndef TOOL_FILESYSTEM_H
#define TOOL_FILESYSTEM_H

#include <cjson/cJSON.h>

// Forward declaration of ConversationState
typedef struct ConversationState ConversationState;

/**
 * tool_read - Reads a file from the filesystem with optional line range support
 *
 * @param params JSON object with: { "file_path": <path>, "start_line": <optional>, "end_line": <optional> }
 * @param state Conversation state containing working_dir
 * @return JSON object with content, total_lines, and optional start_line/end_line
 */
cJSON* tool_read(cJSON *params, ConversationState *state);

/**
 * tool_write - Writes content to a file
 *
 * @param params JSON object with: { "file_path": <path>, "content": <content> }
 * @param state Conversation state containing working_dir
 * @return JSON object with status
 */
cJSON* tool_write(cJSON *params, ConversationState *state);

/**
 * tool_edit - Performs simple string replacement in files
 *
 * @param params JSON object with: { "file_path": <path>, "old_string": <old>, "new_string": <new> }
 * @param state Conversation state containing working_dir
 * @return JSON object with status and replacement count
 */
cJSON* tool_edit(cJSON *params, ConversationState *state);

/**
 * tool_multiedit - Performs multiple string replacements in a file
 *
 * @param params JSON object with: { "file_path": <path>, "edits": [{"old_string": <old>, "new_string": <new>}, ...] }
 * @param state Conversation state containing working_dir
 * @return JSON object with status, total_replacements, and failed_edits
 */
cJSON* tool_multiedit(cJSON *params, ConversationState *state);

/**
 * tool_glob - Finds files matching a pattern
 *
 * @param params JSON object with: { "pattern": <glob_pattern> }
 * @param state Conversation state containing working_dir and additional_dirs
 * @return JSON object with files array and count
 */
cJSON* tool_glob(cJSON *params, ConversationState *state);

#endif // TOOL_FILESYSTEM_H
