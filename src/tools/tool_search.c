/*
 * tool_search.c - Grep search tool implementation
 */

#include "tool_search.h"
#include "../klawed_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Helper function to check if a command exists
static int command_exists(const char *cmd) {
    char test_cmd[256];
    snprintf(test_cmd, sizeof(test_cmd), "command -v %s >/dev/null 2>&1", cmd);
    return system(test_cmd) == 0;
}

cJSON* tool_grep(cJSON *params, ConversationState *state) {
    const cJSON *pattern_json = cJSON_GetObjectItem(params, "pattern");
    const cJSON *path_json = cJSON_GetObjectItem(params, "path");
    const cJSON *max_results_json = cJSON_GetObjectItem(params, "max_results");

    if (!pattern_json || !cJSON_IsString(pattern_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'pattern' parameter");
        return error;
    }

    const char *pattern = pattern_json->valuestring;
    const char *path = path_json && cJSON_IsString(path_json) ?
                       path_json->valuestring : ".";

    // Get max results: parameter > environment > default
    int max_results = 100;  // Default limit
    int warn_large_request = 0;

    // First check if AI provided a value
    if (max_results_json && cJSON_IsNumber(max_results_json)) {
        int requested = max_results_json->valueint;
        if (requested > 0) {
            max_results = requested;
            if (requested > 300) {
                warn_large_request = 1;
            }
        }
    } else {
        // Fall back to environment variable
        const char *max_env = getenv("KLAWED_GREP_MAX_RESULTS");
        if (max_env) {
            int max_val = atoi(max_env);
            if (max_val > 0) {
                max_results = max_val;
            }
        }
    }

    // Print warning for large requests
    if (warn_large_request) {
        printf("\n⚠️  Warning: Grep requested %d matches (>300). This may produce a lot of output.\n\n", max_results);
        fflush(stdout);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON *matches = cJSON_CreateArray();
    int match_count = 0;
    int total_matches = 0;
    int truncated = 0;

    // Determine which grep tool to use (prefer rg > ag > grep)
    const char *grep_tool = "grep";
    const char *exclusions;

    if (command_exists("rg")) {
        grep_tool = "rg";
        // rg uses -g '!pattern' for exclusions
        exclusions =
            "-g '!.git' "
            "-g '!.svn' "
            "-g '!.hg' "
            "-g '!node_modules' "
            "-g '!bower_components' "
            "-g '!vendor' "
            "-g '!build' "
            "-g '!dist' "
            "-g '!target' "
            "-g '!.cache' "
            "-g '!.venv' "
            "-g '!venv' "
            "-g '!__pycache__' "
            "-g '!*.min.js' "
            "-g '!*.min.css' "
            "-g '!*.pyc' "
            "-g '!*.o' "
            "-g '!*.a' "
            "-g '!*.so' "
            "-g '!*.dylib' "
            "-g '!*.exe' "
            "-g '!*.dll' "
            "-g '!*.class' "
            "-g '!*.jar' "
            "-g '!*.war' "
            "-g '!*.zip' "
            "-g '!*.tar' "
            "-g '!*.gz' "
            "-g '!*.log' "
            "-g '!.DS_Store' ";
    } else if (command_exists("ag")) {
        grep_tool = "ag";
        // ag uses --ignore=pattern options
        exclusions =
            "--ignore=.git "
            "--ignore=.svn "
            "--ignore=.hg "
            "--ignore=node_modules "
            "--ignore=bower_components "
            "--ignore=vendor "
            "--ignore=build "
            "--ignore=dist "
            "--ignore=target "
            "--ignore=.cache "
            "--ignore=.venv "
            "--ignore=venv "
            "--ignore=__pycache__ "
            "--ignore='*.min.js' "
            "--ignore='*.min.css' "
            "--ignore='*.pyc' "
            "--ignore='*.o' "
            "--ignore='*.a' "
            "--ignore='*.so' "
            "--ignore='*.dylib' "
            "--ignore='*.exe' "
            "--ignore='*.dll' "
            "--ignore='*.class' "
            "--ignore='*.jar' "
            "--ignore='*.war' "
            "--ignore='*.zip' "
            "--ignore='*.tar' "
            "--ignore='*.gz' "
            "--ignore='*.log' "
            "--ignore=.DS_Store ";
    } else {
        // Standard grep exclusions
        exclusions =
            "--exclude-dir=.git "
            "--exclude-dir=.svn "
            "--exclude-dir=.hg "
            "--exclude-dir=node_modules "
            "--exclude-dir=bower_components "
            "--exclude-dir=vendor "
            "--exclude-dir=build "
            "--exclude-dir=dist "
            "--exclude-dir=target "
            "--exclude-dir=.cache "
            "--exclude-dir=.venv "
            "--exclude-dir=venv "
            "--exclude-dir=__pycache__ "
            "--exclude='*.min.js' "
            "--exclude='*.min.css' "
            "--exclude='*.pyc' "
            "--exclude='*.o' "
            "--exclude='*.a' "
            "--exclude='*.so' "
            "--exclude='*.dylib' "
            "--exclude='*.exe' "
            "--exclude='*.dll' "
            "--exclude='*.class' "
            "--exclude='*.jar' "
            "--exclude='*.war' "
            "--exclude='*.zip' "
            "--exclude='*.tar' "
            "--exclude='*.gz' "
            "--exclude='*.log' "
            "--exclude='.DS_Store' ";
    }

    // First pass: count total matches across all directories
    char count_command[BUFFER_SIZE * 2];

    if (strcmp(grep_tool, "rg") == 0) {
        snprintf(count_command, sizeof(count_command),
                 "cd %s && rg -n %s '%s' %s 2>/dev/null | wc -l || echo 0",
                 state->working_dir, exclusions, pattern, path);
    } else if (strcmp(grep_tool, "ag") == 0) {
        snprintf(count_command, sizeof(count_command),
                 "cd %s && ag -n %s '%s' %s 2>/dev/null | wc -l || echo 0",
                 state->working_dir, exclusions, pattern, path);
    } else {
        snprintf(count_command, sizeof(count_command),
                 "cd %s && grep -r -n %s '%s' %s 2>/dev/null | wc -l || echo 0",
                 state->working_dir, exclusions, pattern, path);
    }

    FILE *count_pipe = popen(count_command, "r");
    if (count_pipe) {
        char count_buffer[32];
        if (fgets(count_buffer, sizeof(count_buffer), count_pipe)) {
            total_matches += atoi(count_buffer);
        }
        pclose(count_pipe);
    }

    // Count matches in additional working directories
    for (int dir_idx = 0; dir_idx < state->additional_dirs_count; dir_idx++) {
        if (strcmp(grep_tool, "rg") == 0) {
            snprintf(count_command, sizeof(count_command),
                     "cd %s && rg -n %s '%s' %s 2>/dev/null | wc -l || echo 0",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        } else if (strcmp(grep_tool, "ag") == 0) {
            snprintf(count_command, sizeof(count_command),
                     "cd %s && ag -n %s '%s' %s 2>/dev/null | wc -l || echo 0",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        } else {
            snprintf(count_command, sizeof(count_command),
                     "cd %s && grep -r -n %s '%s' %s 2>/dev/null | wc -l || echo 0",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        }

        count_pipe = popen(count_command, "r");
        if (count_pipe) {
            char count_buffer[32];
            if (fgets(count_buffer, sizeof(count_buffer), count_pipe)) {
                total_matches += atoi(count_buffer);
            }
            pclose(count_pipe);
        }
    }

    // Set truncation flag if total exceeds limit
    if (total_matches > max_results) {
        truncated = 1;
    }

    // Second pass: collect matches up to the limit
    // Search in main working directory
    char command[BUFFER_SIZE * 2];
    if (strcmp(grep_tool, "rg") == 0) {
        // rg: recursive by default, shows line numbers by default when output is to terminal
        // but we need -n for consistency since we're piping
        snprintf(command, sizeof(command),
                 "cd %s && rg -n %s '%s' %s 2>/dev/null || true",
                 state->working_dir, exclusions, pattern, path);
    } else if (strcmp(grep_tool, "ag") == 0) {
        // ag: recursive by default, shows line numbers with -n
        snprintf(command, sizeof(command),
                 "cd %s && ag -n %s '%s' %s 2>/dev/null || true",
                 state->working_dir, exclusions, pattern, path);
    } else {
        // Standard grep
        snprintf(command, sizeof(command),
                 "cd %s && grep -r -n %s '%s' %s 2>/dev/null || true",
                 state->working_dir, exclusions, pattern, path);
    }

    FILE *pipe = popen(command, "r");
    if (!pipe) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to execute grep");
        return error;
    }

    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        // CRITICAL: Add cancellation point for long grep operations
        pthread_testcancel();

        if (match_count >= max_results) {
            truncated = 1;
            break;
        }
        buffer[strcspn(buffer, "\n")] = 0;  // Remove newline
        cJSON_AddItemToArray(matches, cJSON_CreateString(buffer));
        match_count++;
    }
    pclose(pipe);

    // Search in additional working directories (if not already truncated)
    for (int dir_idx = 0; dir_idx < state->additional_dirs_count && !truncated; dir_idx++) {
        if (strcmp(grep_tool, "rg") == 0) {
            snprintf(command, sizeof(command),
                     "cd %s && rg -n %s '%s' %s 2>/dev/null || true",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        } else if (strcmp(grep_tool, "ag") == 0) {
            snprintf(command, sizeof(command),
                     "cd %s && ag -n %s '%s' %s 2>/dev/null || true",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        } else {
            snprintf(command, sizeof(command),
                     "cd %s && grep -r -n %s '%s' %s 2>/dev/null || true",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        }

        pipe = popen(command, "r");
        if (!pipe) continue;  // Skip this directory on error

        while (fgets(buffer, sizeof(buffer), pipe)) {
            // CRITICAL: Add cancellation point for long grep operations
            pthread_testcancel();

            if (match_count >= max_results) {
                truncated = 1;
                break;
            }
            buffer[strcspn(buffer, "\n")] = 0;  // Remove newline
            cJSON_AddItemToArray(matches, cJSON_CreateString(buffer));
            match_count++;
        }
        pclose(pipe);
    }

    cJSON_AddItemToObject(result, "matches", matches);

    // Add metadata about the search
    if (truncated) {
        char warning[256];
        snprintf(warning, sizeof(warning),
                "Results truncated: showing %d/%d matches. Use KLAWED_GREP_MAX_RESULTS to adjust limit, or refine your search pattern.",
                match_count, total_matches);
        cJSON_AddStringToObject(result, "warning", warning);
    }

    cJSON_AddNumberToObject(result, "match_count", match_count);
    cJSON_AddNumberToObject(result, "total_matches", total_matches);

    return result;
}
