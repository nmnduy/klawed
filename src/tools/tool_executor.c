#include "tool_executor.h"
#include "tool_registry.h"
#include "tool_definitions.h"
#include "../tool_utils.h"
#include "../logger.h"
#include "../util/timestamp_utils.h"
#include "../util/file_utils.h"
#include "../util/format_utils.h"
#include "../base64.h"
#include "../mcp.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <strings.h>

// Validate that a tool name is in the provided tools list
// Returns 1 if valid, 0 if invalid (hallucinated)
int is_tool_allowed(const char *tool_name, ConversationState *state) {
    if (!tool_name || !state) {
        return 0;
    }

    // Get the list of tools that were sent to the API
    cJSON *tool_defs = get_tool_definitions(state, 0);  // Don't need caching for validation
    if (!tool_defs) {
        LOG_ERROR("Failed to get tool definitions for validation");
        return 0;  // Fail closed - reject if we can't verify
    }

    int found = 0;
    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, tool_defs) {
        // Tools are in format: { "type": "function", "function": { "name": "ToolName", ... } }
        cJSON *func = cJSON_GetObjectItem(tool, "function");
        if (func) {
            cJSON *name = cJSON_GetObjectItem(func, "name");
            if (name && cJSON_IsString(name) && strcmp(name->valuestring, tool_name) == 0) {
                found = 1;
                break;
            }
        }
    }

    cJSON_Delete(tool_defs);

    if (!found) {
        LOG_WARN("Tool validation failed: '%s' was not in the provided tools list (possible model hallucination)", tool_name);
    }

    return found;
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
        } else if (is_tool_disabled(tool_name)) {
            // Check if the tool is disabled via KLAWED_DISABLE_TOOLS
            LOG_INFO("Tool '%s' execution blocked - disabled via KLAWED_DISABLE_TOOLS", tool_name);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "This tool has been disabled via KLAWED_DISABLE_TOOLS environment variable");
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
