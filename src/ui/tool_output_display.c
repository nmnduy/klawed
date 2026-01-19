/*
 * tool_output_display.c - Tool-specific output formatting implementation
 *
 * Provides human-readable output for tool execution results.
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

#include "tool_output_display.h"
#include "../klawed_internal.h"
#include "../logger.h"
#include "../persistence.h"
#include "../tool_utils.h"  // For summarize_bash_command

void print_human_readable_tool_output(const char *tool_name,
                                      const char *tool_details,
                                      cJSON *tool_result) {
    if (!tool_name) return;

    // Print tool header - more concise format
    printf("→ ");
    if (tool_details && strlen(tool_details) > 0) {
        printf("%s: %s\n", tool_name, tool_details);
    } else {
        printf("%s\n", tool_name);
    }
    fflush(stdout);

    // Print tool result based on tool type
    if (tool_result) {
        if (strcmp(tool_name, "Bash") == 0) {
            cJSON *exit_code = cJSON_GetObjectItem(tool_result, "exit_code");
            cJSON *output = cJSON_GetObjectItem(tool_result, "output");

            // Only show exit code if non-zero
            if (exit_code && cJSON_IsNumber(exit_code) && exit_code->valueint != 0) {
                printf("  Exit code: %d\n", exit_code->valueint);
            }

            if (output && cJSON_IsString(output)) {
                const char *output_str = output->valuestring;
                if (output_str && strlen(output_str) > 0) {
                    // Print output directly without extra indentation
                    printf("%s", output_str);
                    // Ensure newline at end if not present
                    if (output_str[strlen(output_str)-1] != '\n') {
                        printf("\n");
                    }
                }
            }
        } else if (strcmp(tool_name, "Read") == 0) {
            cJSON *content = cJSON_GetObjectItem(tool_result, "content");

            if (content && cJSON_IsString(content)) {
                const char *content_str = content->valuestring;
                if (content_str && strlen(content_str) > 0) {
                    printf("%s", content_str);
                    // Ensure newline at end if not present
                    if (content_str[strlen(content_str)-1] != '\n') {
                        printf("\n");
                    }
                }
            }
        } else if (strcmp(tool_name, "Grep") == 0) {
            cJSON *match_count = cJSON_GetObjectItem(tool_result, "match_count");
            cJSON *total_matches = cJSON_GetObjectItem(tool_result, "total_matches");
            cJSON *matches = cJSON_GetObjectItem(tool_result, "matches");

            if (match_count && cJSON_IsNumber(match_count) &&
                total_matches && cJSON_IsNumber(total_matches)) {
                int shown = match_count->valueint;
                int total = total_matches->valueint;

                if (shown < total) {
                    printf("  Found %d/%d matches (showing first %d)\n", shown, total, shown);
                } else {
                    printf("  Found %d match%s\n", total, total == 1 ? "" : "es");
                }
            } else if (match_count && cJSON_IsNumber(match_count)) {
                // Fallback for older format without total_matches
                printf("  Found %d match%s\n", match_count->valueint,
                       match_count->valueint == 1 ? "" : "es");
            }

            if (matches && cJSON_IsArray(matches)) {
                int array_size = cJSON_GetArraySize(matches);
                if (array_size > 0) {
                    // Get display limit from environment or use default (increased from 10 to 20)
                    int display_limit = 20;  // Default limit
                    const char *display_env = getenv("KLAWED_GREP_DISPLAY_LIMIT");
                    if (display_env) {
                        int display_val = atoi(display_env);
                        if (display_val > 0) {
                            display_limit = display_val;
                        }
                    }

                    for (int i = 0; i < array_size && i < display_limit; i++) {
                        cJSON *match = cJSON_GetArrayItem(matches, i);
                        if (match && cJSON_IsString(match)) {
                            printf("  %s\n", match->valuestring);
                        }
                    }
                    if (array_size > display_limit) {
                        printf("  ... and %d more\n", array_size - display_limit);
                    }
                }
            }
        } else if (strcmp(tool_name, "Glob") == 0) {
            cJSON *files = cJSON_GetObjectItem(tool_result, "files");

            if (files && cJSON_IsArray(files)) {
                int array_size = cJSON_GetArraySize(files);
                printf("  Found %d file%s\n", array_size, array_size == 1 ? "" : "s");

                if (array_size > 0) {
                    // Get display limit from environment or use default
                    int display_limit = 10;  // Default limit for Glob
                    const char *display_env = getenv("KLAWED_GLOB_DISPLAY_LIMIT");
                    if (display_env) {
                        int display_val = atoi(display_env);
                        if (display_val > 0) {
                            display_limit = display_val;
                        }
                    }

                    for (int i = 0; i < array_size && i < display_limit; i++) {
                        cJSON *file = cJSON_GetArrayItem(files, i);
                        if (file && cJSON_IsString(file)) {
                            printf("  %s\n", file->valuestring);
                        }
                    }
                    if (array_size > display_limit) {
                        printf("  ... and %d more\n", array_size - display_limit);
                    }
                }
            }
        } else if (strcmp(tool_name, "TodoWrite") == 0) {
            cJSON *status = cJSON_GetObjectItem(tool_result, "status");
            cJSON *added = cJSON_GetObjectItem(tool_result, "added");
            cJSON *total = cJSON_GetObjectItem(tool_result, "total");

            if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
                if (added && cJSON_IsNumber(added) && total && cJSON_IsNumber(total)) {
                    printf("  Updated todo list: %d/%d tasks\n", added->valueint, total->valueint);
                }
            }
        } else if (strcmp(tool_name, "Edit") == 0 || strcmp(tool_name, "MultiEdit") == 0) {
            cJSON *status = cJSON_GetObjectItem(tool_result, "status");
            cJSON *replacements = cJSON_GetObjectItem(tool_result, "replacements");

            if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
                if (replacements && cJSON_IsNumber(replacements)) {
                    printf("  Made %d change%s\n", replacements->valueint,
                           replacements->valueint == 1 ? "" : "s");
                } else {
                    printf("  Success\n");
                }
            }
        } else if (strcmp(tool_name, "Subagent") == 0) {
            cJSON *exit_code = cJSON_GetObjectItem(tool_result, "exit_code");
            cJSON *output = cJSON_GetObjectItem(tool_result, "output");

            if (exit_code && cJSON_IsNumber(exit_code)) {
                printf("  Subagent completed with exit code: %d\n", exit_code->valueint);
            }

            if (output && cJSON_IsString(output)) {
                const char *output_str = output->valuestring;
                if (output_str && strlen(output_str) > 0) {
                    // Show last few lines of subagent output
                    char *output_copy = strdup(output_str);
                    if (output_copy) {
                        // Count lines
                        int line_count = 0;
                        char *p = output_copy;
                        while (*p) {
                            if (*p == '\n') line_count++;
                            p++;
                        }

                        // Show last 3 lines
                        if (line_count > 3) {
                            printf("  ...\n");
                            // Find the start of the last 3 lines
                            int lines_to_skip = line_count - 3;
                            p = output_copy;
                            while (lines_to_skip > 0 && *p) {
                                if (*p == '\n') lines_to_skip--;
                                p++;
                            }
                            printf("%s", p);
                        } else {
                            printf("%s", output_str);
                        }
                        free(output_copy);
                    }
                }
            }
        } else {
            // For other tools, print a simplified version
            char *result_str = cJSON_Print(tool_result);
            if (result_str) {
                // Truncate if too long
                size_t len = strlen(result_str);
                if (len > 100) {
                    result_str[100] = '\0';
                    printf("  Result: %s...\n", result_str);
                } else {
                    printf("  Result: %s\n", result_str);
                }
                free(result_str);
            }
        }
    }

    printf("\n");
    fflush(stdout);
}

char* get_tool_details(const char *tool_name, cJSON *arguments) {
    if (!arguments || !cJSON_IsObject(arguments)) {
        return NULL;
    }

    static char details[8192] = {0}; // static buffer for thread safety (increased for subagent prompts)
    details[0] = '\0';

    if (strcmp(tool_name, "Bash") == 0) {
        cJSON *command = cJSON_GetObjectItem(arguments, "command");
        if (cJSON_IsString(command)) {
            summarize_bash_command(command->valuestring, details, sizeof(details));
        }
    } else if (strcmp(tool_name, "Subagent") == 0) {
        cJSON *prompt = cJSON_GetObjectItem(arguments, "prompt");
        if (cJSON_IsString(prompt)) {
            // Show entire prompt without truncation (up to buffer size)
            const char *prompt_str = prompt->valuestring;
            size_t src_len = strlen(prompt_str);
            if (src_len < sizeof(details)) {
                // Prompt fits in buffer
                strlcpy(details, prompt_str, sizeof(details));
            } else {
                // Prompt is too long for even 8KB buffer, truncate with "..."
                if (sizeof(details) > 4) {
                    size_t copy_len = sizeof(details) - 4; // Leave room for "..." and null terminator
                    memcpy(details, prompt_str, copy_len);
                    details[copy_len] = '.';
                    details[copy_len + 1] = '.';
                    details[copy_len + 2] = '.';
                    details[copy_len + 3] = '\0';
                } else {
                    // Buffer is too small even for "...", just truncate
                    strlcpy(details, prompt_str, sizeof(details));
                }
            }
        }
    } else if (strcmp(tool_name, "Read") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        cJSON *start_line = cJSON_GetObjectItem(arguments, "start_line");
        cJSON *end_line = cJSON_GetObjectItem(arguments, "end_line");

        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Show the full path (could be relative or absolute) instead of just filename
            // This provides better context about which file is being read

            if (cJSON_IsNumber(start_line) && cJSON_IsNumber(end_line)) {
                snprintf(details, sizeof(details), "%s:%d-%d", path,
                        start_line->valueint, end_line->valueint);
            } else if (cJSON_IsNumber(start_line)) {
                snprintf(details, sizeof(details), "%s:%d", path, start_line->valueint);
            } else {
                strlcpy(details, path, sizeof(details));
            }
        }
    } else if (strcmp(tool_name, "Write") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Show the full path (could be relative or absolute) instead of just filename
            // This provides better context about which file is being written
            strlcpy(details, path, sizeof(details));
        }
    } else if (strcmp(tool_name, "Edit") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        cJSON *use_regex = cJSON_GetObjectItem(arguments, "use_regex");

        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Show the full path (could be relative or absolute) instead of just filename
            // This provides better context about which file is being edited

            const char *op_type = cJSON_IsTrue(use_regex) ? "(regex)" : "(string)";
            snprintf(details, sizeof(details), "%s %s", path, op_type);
        }
    } else if (strcmp(tool_name, "Glob") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(arguments, "pattern");
        if (cJSON_IsString(pattern)) {
            strlcpy(details, pattern->valuestring, sizeof(details));
        }
    } else if (strcmp(tool_name, "Grep") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(arguments, "pattern");
        cJSON *path = cJSON_GetObjectItem(arguments, "path");

        if (cJSON_IsString(pattern)) {
            if (cJSON_IsString(path) && strlen(path->valuestring) > 0 &&
                strcmp(path->valuestring, ".") != 0) {
                snprintf(details, sizeof(details), "\"%s\" in %s",
                        pattern->valuestring, path->valuestring);
            } else {
                snprintf(details, sizeof(details), "\"%s\"", pattern->valuestring);
            }
        }
    } else if (strcmp(tool_name, "TodoWrite") == 0) {
        cJSON *todos = cJSON_GetObjectItem(arguments, "todos");
        if (cJSON_IsArray(todos)) {
            int count = cJSON_GetArraySize(todos);
            snprintf(details, sizeof(details), "%d task%s", count, count == 1 ? "" : "s");
        }
    } else if (strcmp(tool_name, "Sleep") == 0) {
        cJSON *duration = cJSON_GetObjectItem(arguments, "duration");
        if (cJSON_IsNumber(duration)) {
            int seconds = duration->valueint;
            if (seconds == 1) {
                snprintf(details, sizeof(details), "for 1 second");
            } else {
                snprintf(details, sizeof(details), "for %d seconds", seconds);
            }
        }
    } else if (strcmp(tool_name, "UploadImage") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Show the full path (could be relative or absolute) instead of just filename
            // This provides better context about which image is being uploaded
            strlcpy(details, path, sizeof(details));
        }
    } else if (strcmp(tool_name, "CheckSubagentProgress") == 0) {
        cJSON *pid = cJSON_GetObjectItem(arguments, "pid");
        cJSON *log_file = cJSON_GetObjectItem(arguments, "log_file");
        if (cJSON_IsNumber(pid)) {
            snprintf(details, sizeof(details), "PID %d", pid->valueint);
        } else if (cJSON_IsString(log_file)) {
            const char *path = log_file->valuestring;
            // Extract just the filename from the path
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;
            snprintf(details, sizeof(details), "log: %s", filename);
        } else {
            snprintf(details, sizeof(details), "checking subagent");
        }
    } else if (strcmp(tool_name, "InterruptSubagent") == 0) {
        cJSON *pid = cJSON_GetObjectItem(arguments, "pid");
        if (cJSON_IsNumber(pid)) {
            snprintf(details, sizeof(details), "PID %d", pid->valueint);
        } else {
            snprintf(details, sizeof(details), "interrupt subagent");
        }
    } else if (strcmp(tool_name, "ListMcpResources") == 0) {
        cJSON *server = cJSON_GetObjectItem(arguments, "server");
        if (cJSON_IsString(server)) {
            snprintf(details, sizeof(details), "server: %s", server->valuestring);
        } else {
            snprintf(details, sizeof(details), "all servers");
        }
    } else if (strcmp(tool_name, "ReadMcpResource") == 0) {
        cJSON *server = cJSON_GetObjectItem(arguments, "server");
        cJSON *uri = cJSON_GetObjectItem(arguments, "uri");
        if (cJSON_IsString(server) && cJSON_IsString(uri)) {
            // Show server and truncate URI if too long
            const char *uri_str = uri->valuestring;
            size_t uri_len = strlen(uri_str);
            if (uri_len > 30) {
                snprintf(details, sizeof(details), "%s: %.27s...", server->valuestring, uri_str);
            } else {
                snprintf(details, sizeof(details), "%s: %s", server->valuestring, uri_str);
            }
        } else if (cJSON_IsString(server)) {
            snprintf(details, sizeof(details), "server: %s", server->valuestring);
        } else if (cJSON_IsString(uri)) {
            const char *uri_str = uri->valuestring;
            size_t uri_len = strlen(uri_str);
            if (uri_len > 30) {
                snprintf(details, sizeof(details), "%.27s...", uri_str);
            } else {
                snprintf(details, sizeof(details), "%s", uri_str);
            }
        }
    } else if (strncmp(tool_name, "mcp_", 4) == 0) {
        // Handle MCP tools (format: mcp_<server>_<toolname>)
        // Extract the actual tool name after the server prefix for display
        const char *actual_tool = strchr(tool_name + 4, '_');
        if (actual_tool) {
            actual_tool++; // Skip the underscore

            // Try to extract the most relevant argument for display
            // Common patterns: url, text, path, element, values, etc.
            cJSON *url = cJSON_GetObjectItem(arguments, "url");
            cJSON *text = cJSON_GetObjectItem(arguments, "text");
            cJSON *path = cJSON_GetObjectItem(arguments, "path");
            cJSON *element = cJSON_GetObjectItem(arguments, "element");

            if (cJSON_IsString(url)) {
                // Tools with URL parameter (navigate, fetch, etc.)
                snprintf(details, sizeof(details), "%s: %s", actual_tool, url->valuestring);
            } else if (cJSON_IsString(text) && strlen(text->valuestring) > 0) {
                // Tools with text parameter (type, search, etc.)
                snprintf(details, sizeof(details), "%s: %.30s%s", actual_tool,
                        text->valuestring,
                        strlen(text->valuestring) > 30 ? "..." : "");
            } else if (cJSON_IsString(path)) {
                // Tools with path parameter (read, write, etc.)
                snprintf(details, sizeof(details), "%s: %s", actual_tool, path->valuestring);
            } else if (cJSON_IsString(element)) {
                // Tools with element parameter (click, hover, etc.)
                snprintf(details, sizeof(details), "%s: %s", actual_tool, element->valuestring);
            } else {
                // Generic display: just show the tool name
                snprintf(details, sizeof(details), "%s", actual_tool);
            }
            details[sizeof(details) - 1] = '\0';
        } else {
            // Fallback: show the full tool name without "mcp_" prefix
            snprintf(details, sizeof(details), "%s", tool_name + 4);
            details[sizeof(details) - 1] = '\0';
        }
    }

    return strlen(details) > 0 ? details : NULL;
}

void print_token_usage(ConversationState *state) {
    if (!state || !state->persistence_db) {
        return;
    }

    int prompt_tokens = 0;
    int completion_tokens = 0;
    int cached_tokens = 0;

    // Get token usage for this session
    int result = persistence_get_session_token_usage(
        state->persistence_db,
        state->session_id,
        &prompt_tokens,
        &completion_tokens,
        &cached_tokens
    );

    if (result == 0) {
        // Calculate total tokens (excluding cached tokens since they're free)
        int total_tokens = prompt_tokens + completion_tokens;

        // Print token usage summary
        fprintf(stderr, "\n=== Token Usage Summary ===\n");
        fprintf(stderr, "Session: %s\n", state->session_id ? state->session_id : "unknown");
        fprintf(stderr, "Prompt tokens: %d\n", prompt_tokens);
        fprintf(stderr, "Completion tokens: %d\n", completion_tokens);
        if (cached_tokens > 0) {
            fprintf(stderr, "Cached tokens (free): %d\n", cached_tokens);
            fprintf(stderr, "Total billed tokens: %d (excluding %d cached)\n",
                    total_tokens, cached_tokens);
        } else {
            fprintf(stderr, "Total tokens: %d\n", total_tokens);
        }
        fprintf(stderr, "===========================\n");
    } else {
        fprintf(stderr, "\nNote: Token usage statistics unavailable\n");
    }
}
