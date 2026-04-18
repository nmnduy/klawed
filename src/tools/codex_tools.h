/*
 * Codex-compatible tools for OpenAI subscription provider
 *
 * This header defines the tool interface that matches OpenAI Codex's tool schema
 * when accessed via the ChatGPT subscription API.
 */

#ifndef CODEX_TOOLS_H
#define CODEX_TOOLS_H

#include <cjson/cJSON.h>

/* Forward declaration for ConversationState */
struct ConversationState;
typedef struct ConversationState ConversationState;

/**
 * Get Codex-compatible tool definitions for OpenAI subscription provider.
 *
 * Returns a cJSON array containing tool definitions that match exactly what
 * OpenAI Codex uses. These tools differ from klawed's standard tools:
 *
 * - apply_patch: Uses Codex's structured patch format (not simple Edit)
 * - shell/shell_command: Codex's shell execution interface
 * - list_dir: Directory listing with pagination
 * - view_image: View local images
 * - spawn_agent: Agent coordination
 *
 * Returns: cJSON array of tool definitions (caller must free)
 */
cJSON* get_codex_tool_definitions(void);

/**
 * Get Codex tool definitions formatted for the Responses API.
 *
 * This is similar to get_codex_tool_definitions() but returns tools in the
 * format expected by the OpenAI Responses API (type: "function" at top level,
 * parameters as object, etc.).
 *
 * This function is used by the openai_sub_provider to provide Codex-compatible
 * tools when using the ChatGPT subscription API.
 *
 * Returns: cJSON array of tool definitions (caller must free)
 */
cJSON* get_codex_tool_definitions_for_responses_api(void);

/**
 * Execute the apply_patch tool.
 *
 * Parses and applies a patch in Codex's format:
 * *** Begin Patch
 * *** Update File: path/to/file
 * @@ context
 *   context line
 * - removed line
 * + added line
 * *** End Patch
 *
 * Supports: Add File, Delete File, Update File with Move to
 *
 * Returns: cJSON object with result (success/error)
 */
cJSON* codex_tool_apply_patch(const char *input);

/**
 * Execute the shell tool.
 *
 * Executes a command using execvp-style array.
 *
 * Returns: cJSON object with result
 */
cJSON* codex_tool_shell(cJSON *args);

/**
 * Execute the shell_command tool.
 *
 * Executes a shell command string.
 *
 * Returns: cJSON object with result
 */
cJSON* codex_tool_shell_command(cJSON *args);

/**
 * Execute the list_dir tool.
 *
 * Lists directory entries with pagination support.
 *
 * Returns: cJSON object with entries array
 */
cJSON* codex_tool_list_dir(cJSON *args);

/**
 * Execute the view_image tool.
 *
 * Returns image data for a local file.
 *
 * Returns: cJSON object with image_url
 */
cJSON* codex_tool_view_image(cJSON *args);

/**
 * Execute the spawn_agent tool.
 *
 * Spawns a new klawed subagent with the given task.
 *
 * Returns: cJSON object with agent_id/task_name
 */
cJSON* codex_tool_spawn_agent(cJSON *args);

/* ============================================================================
 * Wrapper functions for tool registry integration
 * These wrap the Codex tools to match the standard tool handler signature
 * ============================================================================ */

cJSON* codex_tool_apply_patch_wrapper(cJSON *params, ConversationState *state);
cJSON* codex_tool_shell_wrapper(cJSON *params, ConversationState *state);
cJSON* codex_tool_shell_command_wrapper(cJSON *params, ConversationState *state);
cJSON* codex_tool_list_dir_wrapper(cJSON *params, ConversationState *state);
cJSON* codex_tool_view_image_wrapper(cJSON *params, ConversationState *state);
cJSON* codex_tool_spawn_agent_wrapper(cJSON *params, ConversationState *state);

#endif // CODEX_TOOLS_H
