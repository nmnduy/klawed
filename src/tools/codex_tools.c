/*
 * Codex-compatible tools for OpenAI subscription provider
 *
 * This file provides tool definitions that match the exact tool schema used by
 * OpenAI Codex when accessed via the ChatGPT subscription API.
 *
 * Key differences from standard klawed tools:
 * - apply_patch: Uses Codex's structured patch format instead of simple string replacement
 * - shell/shell_command: Codex's shell execution interface
 * - list_dir: Directory listing tool
 * - spawn_agent/send_message: Agent coordination tools
 */

#include "codex_tools.h"
#include "../klawed_internal.h"
#include "../logger.h"
#include "../util/file_utils.h"
#include "../base64.h"
#include "../process_utils.h"
#include <string.h>
#include <bsd/string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <signal.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <spawn.h>
extern char **environ;
#endif

/* Tool parameter builders */

static cJSON* create_string_param(const char *description) {
    cJSON *param = cJSON_CreateObject();
    cJSON_AddStringToObject(param, "type", "string");
    cJSON_AddStringToObject(param, "description", description);
    return param;
}

static cJSON* create_number_param(const char *description) {
    cJSON *param = cJSON_CreateObject();
    cJSON_AddStringToObject(param, "type", "number");
    cJSON_AddStringToObject(param, "description", description);
    return param;
}

/* Currently unused but kept for API completeness */
__attribute__((unused))
static cJSON* create_boolean_param(const char *description) {
    cJSON *param = cJSON_CreateObject();
    cJSON_AddStringToObject(param, "type", "boolean");
    cJSON_AddStringToObject(param, "description", description);
    return param;
}

static cJSON* create_array_param(const char *description, cJSON *items) {
    cJSON *param = cJSON_CreateObject();
    cJSON_AddStringToObject(param, "type", "array");
    cJSON_AddStringToObject(param, "description", description);
    if (items) {
        cJSON_AddItemToObject(param, "items", items);
    }
    return param;
}

/* apply_patch tool - The Codex file editing tool */

static cJSON* create_apply_patch_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "apply_patch");
    cJSON_AddStringToObject(tool, "description",
        "Use the `apply_patch` tool to edit files. "
        "Your patch language is a stripped‑down, file‑oriented diff format designed to be easy to parse and safe to apply. "
        "You can think of it as a high‑level envelope:\n\n"
        "*** Begin Patch\n"
        "[ one or more file sections ]\n"
        "*** End Patch\n\n"
        "Within that envelope, you get a sequence of file operations. "
        "You MUST include a header to specify the action you are taking. "
        "Each operation starts with one of three headers:\n\n"
        "*** Add File: <path> - create a new file. Every following line is a + line (the initial contents).\n"
        "*** Delete File: <path> - remove an existing file. Nothing follows.\n"
        "*** Update File: <path> - patch an existing file in place (optionally with a rename).\n\n"
        "May be immediately followed by *** Move to: <new path> if you want to rename the file. "
        "Then one or more \"hunks\", each introduced by @@ (optionally followed by a hunk header). "
        "Within a hunk each line starts with:\n\n"
        "- space: context line (preserved)\n"
        "- - : line to remove\n"
        "- + : line to add\n\n"
        "For instructions on context_before and context_after:\n"
        "- By default, show 3 lines of code immediately above and 3 lines immediately below each change. "
        "If a change is within 3 lines of a previous change, do NOT duplicate the first change's context_after lines in the second change's context_before lines.\n"
        "- If 3 lines of context is insufficient to uniquely identify the snippet of code within the file, "
        "use the @@ operator to indicate the class or function to which the snippet belongs.\n\n"
        "Example:\n"
        "*** Begin Patch\n"
        "*** Update File: src/app.py\n"
        "@@ def greet():\n"
        "  pass\n"
        "- print(\"Hi\")\n"
        "+ print(\"Hello, world!\")\n"
        "*** End Patch");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(props, "input", create_string_param(
        "The entire contents of the apply_patch command in the patch format described above."
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("input"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

static cJSON* create_apply_patch_custom_tool(void) {
    static const char *apply_patch_lark_grammar =
        "start: begin_patch hunk+ end_patch\n"
        "begin_patch: \"*** Begin Patch\" LF\n"
        "end_patch: \"*** End Patch\" LF?\n"
        "\n"
        "hunk: add_hunk | delete_hunk | update_hunk\n"
        "add_hunk: \"*** Add File: \" filename LF add_line+\n"
        "delete_hunk: \"*** Delete File: \" filename LF\n"
        "update_hunk: \"*** Update File: \" filename LF change_move? change?\n"
        "\n"
        "filename: /(.+)/\n"
        "add_line: \"+\" /(.*)/ LF -> line\n"
        "\n"
        "change_move: \"*** Move to: \" filename LF\n"
        "change: (change_context | change_line)+ eof_line?\n"
        "change_context: (\"@@\" | \"@@ \" /(.+)/) LF\n"
        "change_line: (\"+\" | \"-\" | \" \") /(.*)/ LF\n"
        "eof_line: \"*** End of File\" LF\n"
        "\n"
        "%import common.LF\n";

    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "custom");
    cJSON_AddStringToObject(tool, "name", "apply_patch");
    cJSON_AddStringToObject(tool, "description",
        "Use the `apply_patch` tool to edit files. This is a FREEFORM tool, so do not wrap the patch in JSON.");

    cJSON *format = cJSON_CreateObject();
    cJSON_AddStringToObject(format, "type", "grammar");
    cJSON_AddStringToObject(format, "syntax", "lark");
    cJSON_AddStringToObject(format, "definition", apply_patch_lark_grammar);
    cJSON_AddItemToObject(tool, "format", format);

    return tool;
}

/* shell tool - Run shell commands */

static cJSON* create_shell_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "shell");
    cJSON_AddStringToObject(tool, "description",
        "Runs a shell command and returns its output.\n"
        "- The arguments to `shell` will be passed to execvp(). Most terminal commands should be prefixed with [\"bash\", \"-lc\"].\n"
        "- Always set the `workdir` param when using the shell function. Do not use `cd` unless absolutely necessary.");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();

    cJSON *command_items = cJSON_CreateObject();
    cJSON_AddStringToObject(command_items, "type", "string");
    cJSON_AddItemToObject(props, "command", create_array_param(
        "The command to execute",
        command_items
    ));
    cJSON_AddItemToObject(props, "workdir", create_string_param(
        "The working directory to execute the command in"
    ));
    cJSON_AddItemToObject(props, "timeout_ms", create_number_param(
        "The timeout for the command in milliseconds"
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("command"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

/* shell_command tool - Alternative shell interface */

static cJSON* create_shell_command_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "shell_command");
    cJSON_AddStringToObject(tool, "description",
        "Runs a shell command and returns its output.\n"
        "- Always set the `workdir` param when using the shell_command function. Do not use `cd` unless absolutely necessary.");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(props, "command", create_string_param(
        "The shell script to execute in the user's default shell"
    ));
    cJSON_AddItemToObject(props, "workdir", create_string_param(
        "The working directory to execute the command in"
    ));
    cJSON_AddItemToObject(props, "timeout_ms", create_number_param(
        "The timeout for the command in milliseconds"
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("command"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

/* list_dir tool - Directory listing */

static cJSON* create_list_dir_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "list_dir");
    cJSON_AddStringToObject(tool, "description",
        "Lists entries in a local directory with 1-indexed entry numbers and simple type labels.");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(props, "dir_path", create_string_param(
        "Absolute path to the directory to list."
    ));
    cJSON_AddItemToObject(props, "offset", create_number_param(
        "The entry number to start listing from. Must be 1 or greater."
    ));
    cJSON_AddItemToObject(props, "limit", create_number_param(
        "The maximum number of entries to return."
    ));
    cJSON_AddItemToObject(props, "depth", create_number_param(
        "The maximum directory depth to traverse. Must be 1 or greater."
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("dir_path"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

/* view_image tool - View local images */

static cJSON* create_view_image_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "view_image");
    cJSON_AddStringToObject(tool, "description",
        "View a local image from the filesystem (only use if given a full filepath by the user, "
        "and the image isn't already attached to the thread context within <image ...> tags).");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(props, "path", create_string_param(
        "Local filesystem path to an image file"
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("path"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

/* spawn_agent tool - Spawn a subagent */

static cJSON* create_spawn_agent_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "spawn_agent");
    cJSON_AddStringToObject(tool, "description",
        "Spawns a new agent to work on a delegated task in a fresh context. "
        "Returns the canonical task name for the spawned agent. "
        "Use this when: (1) you need a fresh context without conversation history, "
        "(2) delegating a complex independent task, "
        "(3) avoiding context limit issues.");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(props, "task_name", create_string_param(
        "Task name for the new agent. Use lowercase letters, digits, and underscores."
    ));
    cJSON_AddItemToObject(props, "message", create_string_param(
        "The task prompt for the agent. Be specific and include all necessary context."
    ));
    cJSON_AddItemToObject(props, "model", create_string_param(
        "Optional model to use for the agent."
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("task_name"));
    cJSON_AddItemToArray(required, cJSON_CreateString("message"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

/* send_message tool - Send message to an agent */

static cJSON* create_send_message_tool(void) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "send_message");
    cJSON_AddStringToObject(tool, "description",
        "Send a message to an existing agent. Use this to communicate with a spawned agent.");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", "object");
    cJSON_AddBoolToObject(params, "additionalProperties", 0);

    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(props, "target", create_string_param(
        "Agent id or canonical task name to message (from spawn_agent)."
    ));
    cJSON_AddItemToObject(props, "message", create_string_param(
        "Message text to queue on the target agent."
    ));

    cJSON_AddItemToObject(params, "properties", props);

    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("target"));
    cJSON_AddItemToArray(required, cJSON_CreateString("message"));
    cJSON_AddItemToObject(params, "required", required);

    cJSON_AddItemToObject(tool, "parameters", params);

    return tool;
}

/* Get all Codex-compatible tool definitions */

cJSON* get_codex_tool_definitions(void) {
    cJSON *tools = cJSON_CreateArray();
    if (!tools) {
        return NULL;
    }

    cJSON_AddItemToArray(tools, create_apply_patch_tool());
    cJSON_AddItemToArray(tools, create_shell_tool());
    cJSON_AddItemToArray(tools, create_shell_command_tool());
    cJSON_AddItemToArray(tools, create_list_dir_tool());
    cJSON_AddItemToArray(tools, create_view_image_tool());
    cJSON_AddItemToArray(tools, create_spawn_agent_tool());
    cJSON_AddItemToArray(tools, create_send_message_tool());

    return tools;
}

cJSON* get_codex_tool_definitions_for_responses_api(void) {
    cJSON *tools = cJSON_CreateArray();
    if (!tools) {
        return NULL;
    }

    cJSON_AddItemToArray(tools, create_apply_patch_custom_tool());
    cJSON_AddItemToArray(tools, create_shell_tool());
    cJSON_AddItemToArray(tools, create_shell_command_tool());
    cJSON_AddItemToArray(tools, create_list_dir_tool());
    cJSON_AddItemToArray(tools, create_view_image_tool());
    cJSON_AddItemToArray(tools, create_spawn_agent_tool());
    cJSON_AddItemToArray(tools, create_send_message_tool());

    return tools;
}

/* ============================================================================
 * apply_patch Tool Implementation
 * ============================================================================ */

/* Helper: Get next line from input (modifies pos pointer) */
static char* get_next_line(const char **pos) {
    if (!pos || !*pos || !**pos) {
        return NULL;
    }

    const char *start = *pos;
    const char *end = strchr(start, '\n');

    size_t len;
    if (end) {
        len = (size_t)(end - start);
        *pos = end + 1;
    } else {
        len = strlen(start);
        *pos = start + len;
    }

    char *line = malloc(len + 1);
    if (!line) return NULL;

    memcpy(line, start, len);
    line[len] = '\0';

    return line;
}

/* Helper: Check if line starts with prefix */
static int line_starts_with(const char *line, const char *prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

/* Helper: Extract path from header line */
static char* extract_path(const char *line, const char *prefix) {
    const char *path_start = line + strlen(prefix);
    /* Skip leading whitespace */
    while (*path_start == ' ' || *path_start == '\t') {
        path_start++;
    }
    return strdup(path_start);
}

/* Helper: Apply a single hunk to file content */
static char* apply_hunk(const char *content, char **hunk_lines, int hunk_count, const char **error_msg) {
    if (!content || !hunk_lines || hunk_count == 0) {
        *error_msg = "Invalid hunk data";
        return NULL;
    }

    /* Build the search pattern from context and removal lines */
    char *search_pattern = NULL;
    size_t search_len = 0;
    char *replacement = NULL;
    size_t replace_len = 0;

    /* First pass: calculate sizes */
    for (int i = 0; i < hunk_count; i++) {
        const char *line = hunk_lines[i];
        if (line[0] == ' ' || line[0] == '-') {
            /* Context or removal - part of search pattern */
            size_t line_len = strlen(line + 1);
            search_len += line_len + 1; /* +1 for newline */
        }
        if (line[0] == ' ' || line[0] == '+') {
            /* Context or addition - part of replacement */
            size_t line_len = strlen(line + 1);
            replace_len += line_len + 1;
        }
    }

    search_pattern = malloc(search_len + 1);
    replacement = malloc(replace_len + 1);

    if (!search_pattern || !replacement) {
        free(search_pattern);
        free(replacement);
        *error_msg = "Out of memory";
        return NULL;
    }

    search_pattern[0] = '\0';
    replacement[0] = '\0';

    /* Second pass: build strings using strlcat for safety */
    for (int i = 0; i < hunk_count; i++) {
        const char *line = hunk_lines[i];
        if (line[0] == ' ' || line[0] == '-') {
            strlcat(search_pattern, line + 1, search_len + 1);
            strlcat(search_pattern, "\n", search_len + 1);
        }
        if (line[0] == ' ' || line[0] == '+') {
            strlcat(replacement, line + 1, replace_len + 1);
            strlcat(replacement, "\n", replace_len + 1);
        }
    }

    /* Remove trailing newline from search pattern */
    size_t sp_len = strlen(search_pattern);
    if (sp_len > 0 && search_pattern[sp_len - 1] == '\n') {
        search_pattern[sp_len - 1] = '\0';
    }

    /* Remove trailing newline from replacement (Bug #1: must match search pattern handling) */
    size_t rp_len = strlen(replacement);
    if (rp_len > 0 && replacement[rp_len - 1] == '\n') {
        replacement[rp_len - 1] = '\0';
    }

    /* Find and replace */
    char *pos = strstr(content, search_pattern);
    if (!pos) {
        free(search_pattern);
        free(replacement);
        *error_msg = "Hunk context not found in file";
        return NULL;
    }

    size_t content_len = strlen(content);
    size_t prefix_len = (size_t)(pos - content);
    size_t suffix_len = content_len - prefix_len - strlen(search_pattern);
    size_t replace_len_actual = strlen(replacement);

    char *new_content = malloc(prefix_len + replace_len_actual + suffix_len + 1);
    if (!new_content) {
        free(search_pattern);
        free(replacement);
        *error_msg = "Out of memory";
        return NULL;
    }

    memcpy(new_content, content, prefix_len);
    memcpy(new_content + prefix_len, replacement, replace_len_actual);
    memcpy(new_content + prefix_len + replace_len_actual, pos + strlen(search_pattern), suffix_len);
    new_content[prefix_len + replace_len_actual + suffix_len] = '\0';

    free(search_pattern);
    free(replacement);

    return new_content;
}

cJSON* codex_tool_apply_patch(const char *input) {
    if (!input || !*input) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Empty patch input");
        return error;
    }

    /* Check for Begin Patch marker */
    const char *pos = input;
    char *line = get_next_line(&pos);
    if (!line || !line_starts_with(line, "*** Begin Patch")) {
        free(line);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Patch must start with '*** Begin Patch'");
        return error;
    }
    free(line);

    /* Process file operations */
    while (1) {
        line = get_next_line(&pos);
        if (!line) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Unexpected end of patch (expected '*** End Patch')");
            return error;
        }

        /* Check for End Patch marker */
        if (line_starts_with(line, "*** End Patch")) {
            free(line);
            break;
        }

        /* Add File operation */
        if (line_starts_with(line, "*** Add File:")) {
            char *path = extract_path(line, "*** Add File:");
            free(line);

            if (!path || !*path) {
                free(path);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Add File: missing path");
                return error;
            }

            /* Collect content lines (all start with +) */
            char *content = strdup("");
            if (!content) {
                free(path);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Out of memory");
                return error;
            }

            while (1) {
                const char *saved_pos = pos;
                char *next = get_next_line(&pos);

                if (!next || line_starts_with(next, "*** ")) {
                    /* End of content or new operation */
                    free(next);
                    pos = saved_pos;
                    break;
                }

                if (next[0] == '+') {
                    /* Add line content (without the + prefix) */
                    size_t old_len = strlen(content);
                    size_t new_len = strlen(next + 1);
                    char *new_content = realloc(content, old_len + new_len + 2);
                    if (!new_content) {
                        free(content);
                        free(path);
                        free(next);
                        cJSON *error = cJSON_CreateObject();
                        cJSON_AddStringToObject(error, "error", "Out of memory");
                        return error;
                    }
                    content = new_content;
                    strcat(content, next + 1);
                    strcat(content, "\n");
                }
                free(next);
            }

            /* Write the file */
            if (write_file(path, content) != 0) {
                char err_msg[512];
                snprintf(err_msg, sizeof(err_msg), "Failed to write file '%s': %s", path, strerror(errno));
                free(content);
                free(path);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", err_msg);
                return error;
            }

            free(content);
            free(path);
            continue;
        }

        /* Delete File operation */
        if (line_starts_with(line, "*** Delete File:")) {
            char *path = extract_path(line, "*** Delete File:");
            free(line);

            if (!path || !*path) {
                free(path);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Delete File: missing path");
                return error;
            }

            if (unlink(path) != 0) {
                char err_msg[512];
                snprintf(err_msg, sizeof(err_msg), "Failed to delete file '%s': %s", path, strerror(errno));
                free(path);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", err_msg);
                return error;
            }

            free(path);
            continue;
        }

        /* Update File operation */
        if (line_starts_with(line, "*** Update File:")) {
            char *path = extract_path(line, "*** Update File:");
            free(line);

            if (!path || !*path) {
                free(path);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Update File: missing path");
                return error;
            }

            char *move_to = NULL;
            char **hunk_lines = NULL;
            int hunk_count = 0;
            int hunk_capacity = 0;

            /* Process update operations (hunks and Move to) */
            while (1) {
                const char *saved_pos = pos;
                char *next = get_next_line(&pos);

                if (!next || line_starts_with(next, "*** ")) {
                    /* But not *** Move to: which is part of Update File */
                    if (next && line_starts_with(next, "*** Move to:")) {
                        goto handle_move_to;
                    }
                    /* End of update or new operation */
                    if (hunk_count > 0) {
                        /* Apply accumulated hunk */
                        char *content = read_file(path);
                        if (!content) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to read file '%s': %s", path, strerror(errno));
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }

                        const char *error_msg = NULL;
                        char *new_content = apply_hunk(content, hunk_lines, hunk_count, &error_msg);
                        free(content);

                        if (!new_content) {
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", error_msg);
                            return error;
                        }

                        if (write_file(path, new_content) != 0) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to write file '%s': %s", path, strerror(errno));
                            free(new_content);
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }

                        free(new_content);

                        for (int i = 0; i < hunk_count; i++) {
                            free(hunk_lines[i]);
                        }
                        free(hunk_lines);
                        hunk_lines = NULL;
                        hunk_count = 0;
                        hunk_capacity = 0;
                    }

                    /* Handle Move to */
                    if (move_to) {
                        if (rename(path, move_to) != 0) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to rename '%s' to '%s': %s", path, move_to, strerror(errno));
                            free(path);
                            free(move_to);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }
                        free(move_to);
                        move_to = NULL;
                    }

                    /* Check if this is the end or a new operation */
                    if (next && line_starts_with(next, "*** End Patch")) {
                        free(path);
                        free(next);
                        cJSON *result = cJSON_CreateObject();
                        cJSON_AddBoolToObject(result, "success", 1);
                        return result;
                    }

                    pos = saved_pos;
                    free(next);
                    break;
                }

handle_move_to:
                /* Check for Move to directive */
                if (line_starts_with(next, "*** Move to:")) {
                    if (hunk_count > 0) {
                        /* Apply accumulated hunk first */
                        char *content = read_file(path);
                        if (!content) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to read file '%s': %s", path, strerror(errno));
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }

                        const char *error_msg = NULL;
                        char *new_content = apply_hunk(content, hunk_lines, hunk_count, &error_msg);
                        free(content);

                        if (!new_content) {
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", error_msg);
                            return error;
                        }

                        if (write_file(path, new_content) != 0) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to write file '%s': %s", path, strerror(errno));
                            free(new_content);
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }

                        free(new_content);

                        for (int i = 0; i < hunk_count; i++) {
                            free(hunk_lines[i]);
                        }
                        free(hunk_lines);
                        hunk_lines = NULL;
                        hunk_count = 0;
                        hunk_capacity = 0;
                    }

                    /* Bug #2: Free previous move_to if multiple directives found */
                    free(move_to);
                    move_to = extract_path(next, "*** Move to:");
                    free(next);
                    continue;
                }

                /* Check for hunk header */
                if (line_starts_with(next, "@@")) {
                    /* If we have accumulated hunk lines, apply them before starting new hunk */
                    if (hunk_count > 0) {
                        /* Apply accumulated hunk */
                        char *content = read_file(path);
                        if (!content) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to read file '%s': %s", path, strerror(errno));
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }

                        const char *error_msg = NULL;
                        char *new_content = apply_hunk(content, hunk_lines, hunk_count, &error_msg);
                        free(content);

                        if (!new_content) {
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", error_msg);
                            return error;
                        }

                        if (write_file(path, new_content) != 0) {
                            char err_msg[512];
                            snprintf(err_msg, sizeof(err_msg), "Failed to write file '%s': %s", path, strerror(errno));
                            free(new_content);
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", err_msg);
                            return error;
                        }

                        free(new_content);

                        for (int i = 0; i < hunk_count; i++) {
                            free(hunk_lines[i]);
                        }
                        free(hunk_lines);
                        hunk_lines = NULL;
                        hunk_count = 0;
                        hunk_capacity = 0;
                    }
                    free(next);
                    continue;
                }

                /* Check for hunk lines (context, removal, or addition) */
                if (next[0] == ' ' || next[0] == '-' || next[0] == '+') {
                    if (hunk_count >= hunk_capacity) {
                        hunk_capacity = hunk_capacity == 0 ? 16 : hunk_capacity * 2;
                        char **new_lines = realloc(hunk_lines, (size_t)hunk_capacity * sizeof(char *));
                        if (!new_lines) {
                            free(path);
                            free(move_to);
                            for (int i = 0; i < hunk_count; i++) {
                                free(hunk_lines[i]);
                            }
                            free(hunk_lines);
                            free(next);
                            cJSON *error = cJSON_CreateObject();
                            cJSON_AddStringToObject(error, "error", "Out of memory");
                            return error;
                        }
                        hunk_lines = new_lines;
                    }
                    hunk_lines[hunk_count++] = next;
                    continue;
                }

                /* Unknown line, skip it */
                free(next);
            }

            free(path);
            continue;
        }

        /* Unknown directive */
        free(line);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", 1);
    return result;
}

/* ============================================================================
 * Wrapper functions for tool registry
 * ============================================================================ */

/* Wrapper for apply_patch - takes object with "input" field */
cJSON* codex_tool_apply_patch_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    cJSON *input_json = cJSON_GetObjectItem(params, "input");
    if (!input_json || !cJSON_IsString(input_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'input' parameter");
        return error;
    }
    return codex_tool_apply_patch(input_json->valuestring);
}

/* Wrapper for shell tool */
cJSON* codex_tool_shell_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    return codex_tool_shell(params);
}

/* Wrapper for shell_command tool */
cJSON* codex_tool_shell_command_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    return codex_tool_shell_command(params);
}

/* Wrapper for list_dir tool */
cJSON* codex_tool_list_dir_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    return codex_tool_list_dir(params);
}

/* Wrapper for view_image tool */
cJSON* codex_tool_view_image_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    return codex_tool_view_image(params);
}

/* Wrapper for spawn_agent tool */
cJSON* codex_tool_spawn_agent_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    return codex_tool_spawn_agent(params);
}

/* Wrapper for send_message tool */
cJSON* codex_tool_send_message_wrapper(cJSON *params, ConversationState *state) {
    (void)state;
    return codex_tool_send_message(params);
}

/* ============================================================================
 * shell Tool Implementation
 * ============================================================================ */

cJSON* codex_tool_shell(cJSON *args) {
    if (!args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing arguments");
        return error;
    }

    /* Get command array */
    cJSON *command_json = cJSON_GetObjectItem(args, "command");
    if (!command_json || !cJSON_IsArray(command_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'command' parameter (expected array)");
        return error;
    }

    /* Get workdir (optional) */
    const char *workdir = NULL;
    cJSON *workdir_json = cJSON_GetObjectItem(args, "workdir");
    if (workdir_json && cJSON_IsString(workdir_json)) {
        workdir = workdir_json->valuestring;
    }

    /* Get timeout (optional, in milliseconds, convert to seconds) */
    int timeout_seconds = 30; /* Default 30 seconds */
    cJSON *timeout_json = cJSON_GetObjectItem(args, "timeout_ms");
    if (timeout_json && cJSON_IsNumber(timeout_json)) {
        int timeout_ms = timeout_json->valueint;
        if (timeout_ms > 0) {
            timeout_seconds = (timeout_ms + 999) / 1000; /* Round up to seconds */
        }
    }

    /* Build command array for execvp */
    int cmd_count = cJSON_GetArraySize(command_json);
    if (cmd_count == 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Empty command array");
        return error;
    }

    /* Allocate argv array */
    char **argv = calloc((size_t)cmd_count + 1, sizeof(char *));
    if (!argv) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Out of memory");
        return error;
    }

    /* Copy command arguments */
    for (int i = 0; i < cmd_count; i++) {
        cJSON *item = cJSON_GetArrayItem(command_json, i);
        if (!item || !cJSON_IsString(item)) {
            /* Free already allocated args */
            for (int j = 0; j < i; j++) {
                free(argv[j]);
            }
            free(argv);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Command array must contain only strings");
            return error;
        }
        argv[i] = strdup(item->valuestring);
        if (!argv[i]) {
            for (int j = 0; j < i; j++) {
                free(argv[j]);
            }
            free(argv);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Out of memory");
            return error;
        }
    }
    argv[cmd_count] = NULL;

    /* Execute using cross-platform process_utils function.
     * Uses posix_spawn on macOS (thread-safe) and fork/exec on Linux. */
    int timed_out = 0;
    char *output = NULL;
    size_t output_size = 0;
    volatile int interrupt_flag = 0;

    int exit_code = execute_command_with_timeout_argv(
        argv[0],           /* path - first element of argv is the program */
        argv,              /* argv array */
        workdir,           /* working directory */
        timeout_seconds,
        &timed_out,
        &output,
        &output_size,
        &interrupt_flag
    );

    /* Free argv */
    for (int i = 0; i < cmd_count; i++) {
        free(argv[i]);
    }
    free(argv);

    /* Build result - note: execute_command_with_timeout_argv combines stdout and stderr */
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "exit_code", exit_code);
    cJSON_AddStringToObject(result, "stdout", output ? output : "");
    cJSON_AddStringToObject(result, "stderr", "");

    if (timed_out) {
        cJSON_AddStringToObject(result, "error", "Command timed out");
    }

    free(output);
    return result;
}
/* ============================================================================
 * shell_command Tool Implementation
 * ============================================================================ */

cJSON* codex_tool_shell_command(cJSON *args) {
    if (!args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing arguments");
        return error;
    }

    /* Get command string */
    cJSON *command_json = cJSON_GetObjectItem(args, "command");
    if (!command_json || !cJSON_IsString(command_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'command' parameter (expected string)");
        return error;
    }
    const char *command = command_json->valuestring;

    /* Get workdir (optional) */
    const char *workdir = NULL;
    cJSON *workdir_json = cJSON_GetObjectItem(args, "workdir");
    if (workdir_json && cJSON_IsString(workdir_json)) {
        workdir = workdir_json->valuestring;
    }

    /* Get timeout (optional, in milliseconds, convert to seconds) */
    int timeout_seconds = 30; /* Default 30 seconds */
    cJSON *timeout_json = cJSON_GetObjectItem(args, "timeout_ms");
    if (timeout_json && cJSON_IsNumber(timeout_json)) {
        int timeout_ms = timeout_json->valueint;
        if (timeout_ms > 0) {
            timeout_seconds = (timeout_ms + 999) / 1000; /* Round up to seconds */
        }
    }

    /* Use process_utils for command execution */
    int timed_out = 0;
    char *output = NULL;
    size_t output_size = 0;
    volatile int interrupt_flag = 0;

    int exit_code = execute_command_with_timeout(
        command,
        workdir,
        timeout_seconds,
        &timed_out,
        &output,
        &output_size,
        &interrupt_flag
    );

    /* Build result */
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "exit_code", exit_code);
    cJSON_AddStringToObject(result, "stdout", output ? output : "");
    cJSON_AddStringToObject(result, "stderr", "");

    if (timed_out) {
        cJSON_AddStringToObject(result, "error", "Command timed out");
    }

    free(output);

    return result;
}

/* ============================================================================
 * list_dir Tool Implementation
 * ============================================================================ */

static int list_dir_recursive(const char *dir_path, const char *base_path,
                              int depth, int max_depth,
                              cJSON *entries, int *entry_number,
                              int offset, int limit, int *total_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        (*total_count)++;
        (*entry_number)++;

        if (*entry_number >= offset && cJSON_GetArraySize(entries) < limit) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

            struct stat st;
            const char *entry_type = "unknown";
            if (stat(full_path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    entry_type = "directory";
                } else if (S_ISREG(st.st_mode)) {
                    entry_type = "file";
                } else if (S_ISLNK(st.st_mode)) {
                    entry_type = "symlink";
                }
            }

            /* Compute relative path from base_path */
            const char *display_name = full_path + strlen(base_path);
            while (display_name[0] == '/') {
                display_name++;
            }

            cJSON *entry_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(entry_obj, "number", *entry_number);
            cJSON_AddStringToObject(entry_obj, "name", display_name);
            cJSON_AddStringToObject(entry_obj, "type", entry_type);
            cJSON_AddItemToArray(entries, entry_obj);
        }

        /* Recurse into subdirectories */
        if (depth < max_depth) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                list_dir_recursive(full_path, base_path, depth + 1, max_depth,
                                   entries, entry_number, offset, limit, total_count);
            }
        }
    }

    closedir(dir);
    return 0;
}

cJSON* codex_tool_list_dir(cJSON *args) {
    if (!args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing arguments");
        return error;
    }

    /* Get dir_path (required) */
    cJSON *dir_path_json = cJSON_GetObjectItem(args, "dir_path");
    if (!dir_path_json || !cJSON_IsString(dir_path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'dir_path' parameter");
        return error;
    }
    const char *dir_path = dir_path_json->valuestring;

    /* Get offset (optional, 1-indexed, default 1) */
    int offset = 1;
    cJSON *offset_json = cJSON_GetObjectItem(args, "offset");
    if (offset_json && cJSON_IsNumber(offset_json)) {
        offset = offset_json->valueint;
        if (offset < 1) offset = 1;
    }

    /* Get limit (optional, default 100) */
    int limit = 100;
    cJSON *limit_json = cJSON_GetObjectItem(args, "limit");
    if (limit_json && cJSON_IsNumber(limit_json)) {
        limit = limit_json->valueint;
        if (limit < 1) limit = 100;
        if (limit > 1000) limit = 1000; /* Cap at 1000 */
    }

    /* Get depth (optional, default 1) */
    int depth = 1;
    cJSON *depth_json = cJSON_GetObjectItem(args, "depth");
    if (depth_json && cJSON_IsNumber(depth_json)) {
        depth = depth_json->valueint;
        if (depth < 1) depth = 1;
        if (depth > 5) depth = 5; /* Cap at 5 to prevent excessive recursion */
    }

    cJSON *entries = cJSON_CreateArray();
    int entry_number = 0;
    int total_count = 0;

    if (list_dir_recursive(dir_path, dir_path, 1, depth, entries,
                           &entry_number, offset, limit, &total_count) != 0) {
        cJSON_Delete(entries);
        cJSON *error = cJSON_CreateObject();
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Failed to open directory '%s': %s",
                 dir_path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    /* Build result */
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "entries", entries);
    cJSON_AddNumberToObject(result, "total_count", total_count);
    cJSON_AddBoolToObject(result, "has_more", entry_number > offset + limit - 1);

    return result;
}

/* ============================================================================
 * view_image Tool Implementation
 * ============================================================================ */

cJSON* codex_tool_view_image(cJSON *args) {
    if (!args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing arguments");
        return error;
    }

    /* Get path (required) */
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'path' parameter");
        return error;
    }
    const char *path = path_json->valuestring;

    /* Check if file exists and is readable */
    if (access(path, R_OK) != 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Cannot read image file '%s': %s",
                 path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    /* Open file */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Failed to open image file '%s': %s",
                 path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    /* Get file size */
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        cJSON *error = cJSON_CreateObject();
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Failed to get file size for '%s': %s",
                 path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    off_t file_size = st.st_size;

    /* Check file size limit (20MB) */
    if (file_size > 20 * 1024 * 1024) {
        close(fd);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Image file too large (max 20MB)");
        return error;
    }

    /* Read file content */
    unsigned char *image_data = malloc((size_t)file_size);
    if (!image_data) {
        close(fd);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to allocate memory for image");
        return error;
    }

    ssize_t bytes_read = read(fd, image_data, (size_t)file_size);
    close(fd);

    if (bytes_read != file_size) {
        free(image_data);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read image file");
        return error;
    }

    /* Detect MIME type from extension */
    const char *mime_type = "application/octet-stream";
    const char *ext = strrchr(path, '.');
    if (ext) {
        char lower_ext[16];
        size_t ext_len = strlen(ext);
        if (ext_len < sizeof(lower_ext)) {
            for (size_t i = 0; i <= ext_len; i++) {
                lower_ext[i] = (char)tolower((unsigned char)ext[i]);
            }

            if (strcmp(lower_ext, ".png") == 0) {
                mime_type = "image/png";
            } else if (strcmp(lower_ext, ".jpg") == 0 || strcmp(lower_ext, ".jpeg") == 0) {
                mime_type = "image/jpeg";
            } else if (strcmp(lower_ext, ".gif") == 0) {
                mime_type = "image/gif";
            } else if (strcmp(lower_ext, ".webp") == 0) {
                mime_type = "image/webp";
            } else if (strcmp(lower_ext, ".bmp") == 0) {
                mime_type = "image/bmp";
            } else if (strcmp(lower_ext, ".tiff") == 0 || strcmp(lower_ext, ".tif") == 0) {
                mime_type = "image/tiff";
            } else if (strcmp(lower_ext, ".svg") == 0) {
                mime_type = "image/svg+xml";
            }
        }
    }

    /* Try to detect image type from magic numbers */
    if (file_size >= 8) {
        unsigned char magic[8];
        memcpy(magic, image_data, 8);

        /* PNG */
        if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G' &&
            magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A) {
            mime_type = "image/png";
        }
        /* JPEG */
        else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
            mime_type = "image/jpeg";
        }
        /* GIF */
        else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8' &&
                (magic[4] == '7' || magic[4] == '9') && magic[5] == 'a') {
            mime_type = "image/gif";
        }
        /* WebP */
        else if (file_size >= 12) {
            if (magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F') {
                if (image_data[8] == 'W' && image_data[9] == 'E' &&
                    image_data[10] == 'B' && image_data[11] == 'P') {
                    mime_type = "image/webp";
                }
            }
        }
        /* BMP */
        else if (magic[0] == 'B' && magic[1] == 'M') {
            mime_type = "image/bmp";
        }
        /* TIFF */
        else if ((magic[0] == 'I' && magic[1] == 'I') || (magic[0] == 'M' && magic[1] == 'M')) {
            mime_type = "image/tiff";
        }
    }

    /* Base64 encode the image */
    size_t encoded_size = 0;
    char *base64_data = base64_encode(image_data, (size_t)file_size, &encoded_size);
    free(image_data);

    if (!base64_data) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to encode image as base64");
        return error;
    }

    /* Build data URL */
    size_t url_size = strlen("data:") + strlen(mime_type) + strlen(";base64,") + encoded_size + 1;
    char *image_url = malloc(url_size);
    if (!image_url) {
        free(base64_data);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Out of memory");
        return error;
    }

    snprintf(image_url, url_size, "data:%s;base64,%s", mime_type, base64_data);

    /* Build result */
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "image_url", image_url);

    free(base64_data);
    free(image_url);

    return result;
}

/* ============================================================================
 * spawn_agent Tool Implementation
 * ============================================================================ */

cJSON* codex_tool_spawn_agent(cJSON *args) {
    if (!args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing arguments");
        return error;
    }

    /* Get task_name (required) */
    cJSON *task_name_json = cJSON_GetObjectItem(args, "task_name");
    if (!task_name_json || !cJSON_IsString(task_name_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'task_name' parameter");
        return error;
    }
    const char *task_name = task_name_json->valuestring;

    /* Validate task_name format (lowercase letters, digits, underscores) */
    for (size_t i = 0; i < strlen(task_name); i++) {
        char c = task_name[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "task_name must contain only lowercase letters, digits, and underscores");
            return error;
        }
    }

    /* Get message (required) */
    cJSON *message_json = cJSON_GetObjectItem(args, "message");
    if (!message_json || !cJSON_IsString(message_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'message' parameter");
        return error;
    }
    const char *message = message_json->valuestring;

    /* Get model (optional) */
    const char *model = NULL;
    cJSON *model_json = cJSON_GetObjectItem(args, "model");
    if (model_json && cJSON_IsString(model_json)) {
        model = model_json->valuestring;
    }

    /* Find executable path */
    char exe_path[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to determine executable path");
        return error;
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to determine executable path");
        return error;
    }
    exe_path[len] = '\0';
#endif

    /* Create log directory */
    const char *log_dir = ".klawed/subagent";
    mkdir_p(log_dir);

    /* Generate log file path */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    char log_file[PATH_MAX];
    int ret = snprintf(log_file, sizeof(log_file), "%s/subagent_%s_%d.log",
                       log_dir, timestamp, getpid());
    if (ret < 0 || (size_t)ret >= sizeof(log_file)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Log file path too long");
        return error;
    }

    /* Escape message for shell */
    size_t escaped_size = strlen(message) * 2 + 1;
    char *escaped_message = malloc(escaped_size);
    if (!escaped_message) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Out of memory");
        return error;
    }

    size_t j = 0;
    for (size_t i = 0; message[i] && j < escaped_size - 2; i++) {
        if (message[i] == '"' || message[i] == '\\' || message[i] == '$' || message[i] == '`') {
            escaped_message[j++] = '\\';
        }
        escaped_message[j++] = message[i];
    }
    escaped_message[j] = '\0';

    /* Build command */
    char command[BUFFER_SIZE * 2];
    snprintf(command, sizeof(command),
             "\"%s\" \"%s\" > \"%s\" 2>&1 </dev/null",
             exe_path, escaped_message, log_file);

    free(escaped_message);

#ifdef __APPLE__
    /* Use posix_spawn on macOS */
    pid_t pid;
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t spawnattr;

    int rc = posix_spawn_file_actions_init(&file_actions);
    if (rc != 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to init posix_spawn");
        return error;
    }

    rc = posix_spawnattr_init(&spawnattr);
    if (rc != 0) {
        posix_spawn_file_actions_destroy(&file_actions);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to init spawn attributes");
        return error;
    }

    posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, log_file,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    short flags = POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&spawnattr, flags);
    posix_spawnattr_setpgroup(&spawnattr, 0);

    /* Build environment */
    int env_count = 0;
    for (char **e = environ; *e; e++) {
        env_count++;
    }

    char **new_environ = malloc((size_t)(env_count + 5) * sizeof(char *));
    if (!new_environ) {
        posix_spawn_file_actions_destroy(&file_actions);
        posix_spawnattr_destroy(&spawnattr);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Out of memory");
        return error;
    }

    int new_env_idx = 0;
    for (int i = 0; i < env_count; i++) {
        if (strncmp(environ[i], "KLAWED_SQLITE_DB_PATH=", 22) == 0) continue;
        if (strncmp(environ[i], "KLAWED_SQLITE_SENDER=", 21) == 0) continue;
        if (strncmp(environ[i], "KLAWED_IS_SUBAGENT=", 19) == 0) continue;
        if (strncmp(environ[i], "KLAWED_LLM_PROVIDER=", 20) == 0) continue;
        new_environ[new_env_idx++] = environ[i];
    }

    char *is_subagent = strdup("KLAWED_IS_SUBAGENT=1");
    if (is_subagent) new_environ[new_env_idx++] = is_subagent;

    char *provider_env = NULL;
    if (model && model[0] != '\0') {
        size_t prov_len = strlen(model) + 22;
        provider_env = malloc(prov_len);
        if (provider_env) {
            snprintf(provider_env, prov_len, "KLAWED_LLM_PROVIDER=%s", model);
            new_environ[new_env_idx++] = provider_env;
        }
    }

    new_environ[new_env_idx] = NULL;

    char shell_name[] = "sh";
    char dash_c[] = "-c";
    char *argv[] = {shell_name, dash_c, command, NULL};

    rc = posix_spawn(&pid, "/bin/sh", &file_actions, &spawnattr, argv, new_environ);

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&spawnattr);
    free(is_subagent);
    free(provider_env);
    free(new_environ);

    if (rc != 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to spawn subagent");
        return error;
    }
#else
    /* Use fork/exec on Linux */
    pid_t pid = fork();
    if (pid < 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to fork");
        return error;
    }

    if (pid == 0) {
        /* Child process */
        if (setsid() == -1) {
            setpgid(0, 0);
        }

        setenv("KLAWED_IS_SUBAGENT", "1", 1);
        unsetenv("KLAWED_SQLITE_DB_PATH");
        unsetenv("KLAWED_SQLITE_SENDER");

        if (model && model[0] != '\0') {
            setenv("KLAWED_LLM_PROVIDER", model, 1);
        }

        /* Redirect output */
        int fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        /* Redirect stdin */
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        exit(127);
    }
#endif

    /* Build result */
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "task_name", task_name);
    cJSON_AddNumberToObject(result, "agent_id", pid);
    cJSON_AddNumberToObject(result, "pid", pid);
    cJSON_AddStringToObject(result, "log_file", log_file);

    return result;
}

/* ============================================================================
 * send_message Tool Implementation
 * ============================================================================ */

cJSON* codex_tool_send_message(cJSON *args) {
    if (!args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing arguments");
        return error;
    }

    /* Get target (required) */
    cJSON *target_json = cJSON_GetObjectItem(args, "target");
    if (!target_json || !cJSON_IsString(target_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'target' parameter");
        return error;
    }
    const char *target = target_json->valuestring;

    /* Get message (required) */
    cJSON *message_json = cJSON_GetObjectItem(args, "message");
    if (!message_json || !cJSON_IsString(message_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'message' parameter");
        return error;
    }
    const char *message = message_json->valuestring;

    /* Try to parse target as PID */
    char *endptr;
    long pid = strtol(target, &endptr, 10);
    if (*endptr == '\0' && pid > 0) {
        /* Target is a PID - check if process exists */
        if (kill((pid_t)pid, 0) != 0) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Target agent not found");
            return error;
        }
    }

    /* For now, we just return success - actual message delivery would require
     * a more complex inter-process communication mechanism */
    (void)message; /* Unused for now */

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "success", 1);
    cJSON_AddStringToObject(result, "target", target);

    return result;
}
