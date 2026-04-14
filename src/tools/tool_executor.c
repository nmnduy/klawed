#include "tool_registry.h"
#include "tool_definitions.h"
#include "codex_tools.h"
#include "../openai_responses.h"
#include "../tool_utils.h"
#include "../logger.h"
#include "../util/timestamp_utils.h"
#include "../util/file_utils.h"
#include "../util/format_utils.h"
#include "../base64.h"
#include "../mcp.h"
#include "../klawed_internal.h"
#include "../dynamic_tools.h"
#include "../process_utils.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <strings.h>

// Helper function to check for a tool name in a tool definition array
// Handles both formats:
//   - Chat Completions: { "type": "function", "function": { "name": "...", ... } }
//   - Responses API:    { "type": "function", "name": "...", "description": "...", ... }
static int check_tool_in_defs(cJSON *tool_defs, const char *name_to_find) {
    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, tool_defs) {
        // Try Chat Completions format first (nested "function" object)
        cJSON *func = cJSON_GetObjectItem(tool, "function");
        if (func) {
            cJSON *name = cJSON_GetObjectItem(func, "name");
            if (name && cJSON_IsString(name) && strcmp(name->valuestring, name_to_find) == 0) {
                return 1;
            }
        }

        // Try Responses API format (top-level "name" field)
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        if (name && cJSON_IsString(name) && strcmp(name->valuestring, name_to_find) == 0) {
            return 1;
        }
    }
    return 0;
}

// Validate that a tool name is in the provided tools list
// Returns 1 if valid, 0 if invalid (hallucinated)
int is_tool_allowed(const char *tool_name, ConversationState *state) {
    if (!tool_name || !state) {
        return 0;
    }

    // Check standard tool definitions (Chat Completions format)
    cJSON *tool_defs = get_tool_definitions(state, 0);
    if (tool_defs) {
        if (check_tool_in_defs(tool_defs, tool_name)) {
            cJSON_Delete(tool_defs);
            return 1;
        }
        cJSON_Delete(tool_defs);
    }

    // Check Responses API tool definitions (used by some providers)
    cJSON *responses_defs = get_tool_definitions_for_responses_api(state, 0);
    if (responses_defs) {
        if (check_tool_in_defs(responses_defs, tool_name)) {
            cJSON_Delete(responses_defs);
            return 1;
        }
        cJSON_Delete(responses_defs);
    }

    // Check Codex tool definitions (used by OpenAI subscription provider)
    cJSON *codex_defs = get_codex_tool_definitions();
    if (codex_defs) {
        if (check_tool_in_defs(codex_defs, tool_name)) {
            cJSON_Delete(codex_defs);
            return 1;
        }
        cJSON_Delete(codex_defs);
    }

    LOG_WARN("Tool validation failed: '%s' was not in any provided tools list (possible model hallucination)", tool_name);
    return 0;
}

cJSON* execute_tool(const char *tool_name, cJSON *input, ConversationState *state) {
    // Time the tool execution
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    cJSON *result = NULL;

    // Check for verbose tool logging
    int tool_verbose = 0;
    const char *tool_verbose_env = getenv("KLAWED_TOOL_VERBOSE");
    if (tool_verbose_env) {
        tool_verbose = atoi(tool_verbose_env);
        if (tool_verbose < 0) tool_verbose = 0;
        if (tool_verbose > 2) tool_verbose = 2;  // 0=off, 1=basic, 2=detailed
    }

    // Log tool execution attempt
    char *input_str = cJSON_PrintUnformatted(input);
    LOG_DEBUG("execute_tool: Attempting to execute tool '%s' with input: %s",
              tool_name, input_str ? input_str : "null");
    if (input_str) free(input_str);

    // Verbose logging: print to log file if enabled
    if (tool_verbose >= 1) {
        LOG_DEBUG("[TOOL VERBOSE] Executing tool: %s", tool_name);
        if (tool_verbose >= 2) {
            char *formatted_input = cJSON_Print(input);
            if (formatted_input) {
                LOG_DEBUG("[TOOL VERBOSE] Input parameters:\\n%s", formatted_input);
                free(formatted_input);
            }
        }
    }

    // Try built-in tools first
    const Tool *tool = find_tool_by_name(tool_name);
    if (tool) {
        // Check if we're running as a subagent and exclude subagent-related tools to prevent recursion
        const char *is_subagent_env = getenv("KLAWED_IS_SUBAGENT");
        int is_subagent = is_subagent_env && (strcmp(is_subagent_env, "1") == 0 ||
                                             strcasecmp(is_subagent_env, "true") == 0 ||
                                             strcasecmp(is_subagent_env, "yes") == 0);

        if (is_subagent && (strcmp(tool_name, "Subagent") == 0 ||
                           strcmp(tool_name, "CheckSubagentProgress") == 0 ||
                           strcmp(tool_name, "InterruptSubagent") == 0)) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Subagent-related tools are disabled when running as a subagent to prevent recursion");
            result = error;
        } else if (is_tool_disabled_for_state(tool_name, state)) {
            LOG_INFO("Tool '%s' execution blocked - disabled via runtime settings", tool_name);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "This tool has been disabled in settings");
            result = error;
        } else {
            LOG_DEBUG("execute_tool: Found built-in tool '%s'", tool_name);
            result = tool->handler(input, state);
        }
    }

#ifndef TEST_BUILD
    // If not found in built-in tools, try MCP tools
    if (!result && state && state->mcp_config && strncmp(tool_name, "mcp_", 4) == 0) {
        LOG_DEBUG("execute_tool: Tool '%s' matches MCP pattern, attempting MCP lookup", tool_name);
        MCPServer *server = mcp_find_tool_server(state->mcp_config, tool_name);
        if (server) {
            LOG_DEBUG("execute_tool: Found MCP server '%s' for tool '%s'", server->name, tool_name);
            // Extract the actual tool name (remove mcp_<server>_ prefix)
            const char *actual_tool_name = strchr(tool_name + 4, '_');
            if (actual_tool_name) {
                actual_tool_name++;  // Skip the underscore

                LOG_INFO("Calling MCP tool '%s' on server '%s' (original tool name: '%s')",
                         actual_tool_name, server->name, tool_name);

                MCPToolResult *mcp_result = mcp_call_tool(server, actual_tool_name, input);
                if (mcp_result) {
                    LOG_DEBUG("execute_tool: MCP tool call succeeded, is_error=%d", mcp_result->is_error);
                    result = cJSON_CreateObject();

                    if (mcp_result->is_error) {
                        LOG_WARN("execute_tool: MCP tool returned error: %s",
                                mcp_result->result ? mcp_result->result : "MCP tool error");
                        cJSON_AddStringToObject(result, "error", mcp_result->result ? mcp_result->result : "MCP tool error");
                    } else {
                        LOG_DEBUG("execute_tool: MCP tool returned success, result length: %zu, blob size: %zu, mime_type: %s",
                                 mcp_result->result ? strlen(mcp_result->result) : 0,
                                 mcp_result->blob_size,
                                 mcp_result->mime_type ? mcp_result->mime_type : "none");

                        // Handle different content types
                        if (mcp_result->blob && mcp_result->blob_size > 0) {
                            // Binary content (e.g., images) - auto-save to file
                            const char *mime_type = mcp_result->mime_type ? mcp_result->mime_type : "application/octet-stream";

                            // Generate appropriate filename based on tool and MIME type
                            char filename[256];
                            if (strncmp(actual_tool_name, "screenshot", 10) == 0 ||
                                strncmp(actual_tool_name, "take_screenshot", 15) == 0) {
                                generate_timestamped_filename(filename, sizeof(filename), "screenshot", mime_type);
                            } else if (strncmp(mime_type, "image/", 6) == 0) {
                                generate_timestamped_filename(filename, sizeof(filename), "image", mime_type);
                            } else {
                                generate_timestamped_filename(filename, sizeof(filename), "file", mime_type);
                            }

                            // Save binary data to file
                            int save_result = save_binary_file(filename, mcp_result->blob, mcp_result->blob_size);

                            if (save_result == 0) {
                                // Success - encode base64 for image content (if it's an image)
                                int is_image = (strncmp(mime_type, "image/", 6) == 0);

                                if (is_image) {
                                    // For images, encode to base64 and mark as image content
                                    // This allows the TUI to display it properly like UploadImage
                                    size_t encoded_size = 0;
                                    char *encoded_data = base64_encode(mcp_result->blob, mcp_result->blob_size, &encoded_size);
                                    if (encoded_data) {
                                        cJSON_AddStringToObject(result, "content_type", "image");
                                        cJSON_AddStringToObject(result, "file_path", filename);
                                        cJSON_AddStringToObject(result, "mime_type", mime_type);
                                        cJSON_AddStringToObject(result, "base64_data", encoded_data);
                                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)mcp_result->blob_size);
                                        free(encoded_data);
                                        LOG_INFO("execute_tool: Saved image to '%s' (%zu bytes)", filename, mcp_result->blob_size);
                                    } else {
                                        // Encoding failed, fall back to file info only
                                        LOG_WARN("execute_tool: Failed to encode image to base64, returning file info only");
                                        cJSON_AddStringToObject(result, "status", "success");
                                        cJSON_AddStringToObject(result, "message", "Image saved to file");
                                        cJSON_AddStringToObject(result, "file_path", filename);
                                        cJSON_AddStringToObject(result, "file_type", mime_type);
                                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)mcp_result->blob_size);
                                        cJSON_AddStringToObject(result, "file_size_human", format_file_size(mcp_result->blob_size));
                                    }
                                } else {
                                    // For non-image binary content, return file info only
                                    cJSON_AddStringToObject(result, "status", "success");
                                    cJSON_AddStringToObject(result, "message", "Binary content saved to file");
                                    cJSON_AddStringToObject(result, "file_path", filename);
                                    cJSON_AddStringToObject(result, "file_type", mime_type);
                                    cJSON_AddNumberToObject(result, "file_size_bytes", (double)mcp_result->blob_size);
                                    cJSON_AddStringToObject(result, "file_size_human", format_file_size(mcp_result->blob_size));
                                    LOG_INFO("execute_tool: Saved binary content to '%s' (%zu bytes)", filename, mcp_result->blob_size);
                                }
                            } else {
                                // Failed to save - fall back to base64 (but this shouldn't happen)
                                LOG_WARN("execute_tool: Failed to save binary content to file, falling back to base64");
                                cJSON_AddStringToObject(result, "content_type", "binary");
                                cJSON_AddStringToObject(result, "mime_type", mime_type);

                                size_t encoded_size = 0;
                                char *encoded_data = base64_encode(mcp_result->blob, mcp_result->blob_size, &encoded_size);
                                if (encoded_data) {
                                    cJSON_AddStringToObject(result, "content", encoded_data);
                                    free(encoded_data);
                                } else {
                                    cJSON_AddStringToObject(result, "content", "[binary data received - saving and encoding failed]");
                                }
                            }
                        } else {
                            // Text content
                            cJSON_AddStringToObject(result, "content_type", "text");
                            if (mcp_result->mime_type) {
                                cJSON_AddStringToObject(result, "mime_type", mcp_result->mime_type);
                            }
                            cJSON_AddStringToObject(result, "content", mcp_result->result ? mcp_result->result : "");
                        }
                    }

                    mcp_free_tool_result(mcp_result);
                } else {
                    LOG_ERROR("execute_tool: MCP tool call failed for tool '%s' on server '%s'",
                              actual_tool_name, server->name);
                    result = cJSON_CreateObject();
                    cJSON_AddStringToObject(result, "error", "MCP tool call failed");
                }
            } else {
                LOG_ERROR("execute_tool: Failed to extract actual tool name from '%s'", tool_name);
            }
        } else {
            LOG_WARN("execute_tool: No MCP server found for tool '%s'", tool_name);
        }
    } else if (!result && state && state->mcp_config) {
        LOG_DEBUG("execute_tool: Tool '%s' not found in built-in tools and doesn't match MCP pattern", tool_name);
    }
#endif

    // Try dynamic tools (exec-based)
    if (!result) {
        DynamicToolsRegistry dyn_registry;
        dynamic_tools_init(&dyn_registry);
        char dyn_path[DYNAMIC_TOOLS_PATH_MAX];
        if (dynamic_tools_get_path(dyn_path, sizeof(dyn_path)) == 0) {
            dynamic_tools_load_from_file(&dyn_registry, dyn_path);
        }
        const DynamicToolDef *dyn_tool = dynamic_tools_has_tool(&dyn_registry, tool_name)
            ? dynamic_tools_get_tool(&dyn_registry, tool_name)
            : NULL;

        if (dyn_tool && dyn_tool->exec[0] != '\0') {
            // Serialize the input JSON and pass it as a single argument to the exec command
            char *input_json = cJSON_PrintUnformatted(input);
            if (input_json) {
                // Build: exec_template 'JSON'
                // We shell-quote the JSON: replace each ' with '\''
                size_t json_len = strlen(input_json);
                size_t quoted_len = json_len * 4 + 3; // worst case all single-quotes
                char *quoted = malloc(quoted_len);
                if (quoted) {
                    size_t qi = 0;
                    quoted[qi++] = '\'';
                    for (size_t ci = 0; ci < json_len && qi < quoted_len - 4; ci++) {
                        if (input_json[ci] == '\'') {
                            quoted[qi++] = '\'';
                            quoted[qi++] = '\\';
                            quoted[qi++] = '\'';
                            quoted[qi++] = '\'';
                        } else {
                            quoted[qi++] = input_json[ci];
                        }
                    }
                    quoted[qi++] = '\'';
                    quoted[qi] = '\0';

                    size_t cmd_len = strlen(dyn_tool->exec) + qi + 2;
                    char *full_cmd = malloc(cmd_len);
                    if (full_cmd) {
                        snprintf(full_cmd, cmd_len, "%s %s", dyn_tool->exec, quoted);
                        LOG_DEBUG("execute_tool: Running dynamic tool exec: %s", full_cmd);

                        int timed_out = 0;
                        char *output = NULL;
                        size_t output_size = 0;
                        volatile int *interrupt_flag = state ? &state->interrupt_requested : NULL;
                        execute_command_with_timeout(full_cmd, NULL, 35, &timed_out, &output, &output_size, interrupt_flag);

                        result = cJSON_CreateObject();
                        if (timed_out) {
                            cJSON_AddStringToObject(result, "error", "Dynamic tool exec timed out");
                        } else if (output && output_size > 0) {
                            // Try to parse output as JSON, otherwise wrap as plain output
                            cJSON *parsed = cJSON_Parse(output);
                            if (parsed) {
                                cJSON_Delete(result);
                                result = parsed;
                            } else {
                                cJSON_AddStringToObject(result, "output", output);
                            }
                        } else {
                            cJSON_AddStringToObject(result, "output", "");
                        }
                        free(output);
                        free(full_cmd);
                    }
                    free(quoted);
                }
                free(input_json);
            }
        } else if (dyn_tool) {
            // Tool defined but no exec — not executable
            result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "error",
                "Dynamic tool has no exec command configured");
            LOG_WARN("execute_tool: Dynamic tool '%s' has no exec field", tool_name);
        }
        dynamic_tools_cleanup(&dyn_registry);
    }

    if (!result) {
        LOG_WARN("execute_tool: No result generated for tool '%s'", tool_name);
        result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "error", "Unknown tool");
    }

    // Log execution time and result
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                       (end.tv_nsec - start.tv_nsec) / 1000000;

    char *result_str = cJSON_PrintUnformatted(result);
    LOG_DEBUG("execute_tool: Tool '%s' executed in %ld ms, result: %s",
              tool_name, duration_ms, result_str ? result_str : "null");
    if (result_str) free(result_str);

    LOG_INFO("Tool '%s' executed in %ld ms", tool_name, duration_ms);

    // Verbose logging: print result summary to log file if enabled
    if (tool_verbose >= 1) {
        LOG_DEBUG("[TOOL VERBOSE] Tool '%s' completed in %ld ms", tool_name, duration_ms);
        if (tool_verbose >= 2) {
            char *formatted_result = cJSON_Print(result);
            if (formatted_result) {
                LOG_DEBUG("[TOOL VERBOSE] Result:\\n%s", formatted_result);
                free(formatted_result);
            }
        }
    }

    return result;
}
