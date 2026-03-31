/*
 * environment.c - Platform and environment detection for system prompt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <sys/stat.h>
#include <limits.h>

#include "environment.h"

/**
 * Check if the working directory is a git repository.
 */
int is_git_repo(const char *working_dir) {
    if (!working_dir) {
        return 0;
    }

    char git_path[PATH_MAX];
    snprintf(git_path, sizeof(git_path), "%s/.git", working_dir);

    struct stat st;
    return (stat(git_path, &st) == 0);
}

/**
 * Execute a git command and return its output.
 * Uses popen() to run the command and captures output.
 * Trims trailing newline from output.
 *
 * Note: On macOS, git commands may hang due to credential prompts or network issues.
 * We use a timeout wrapper to prevent indefinite hangs.
 */
char* exec_git_command(const char *command) {
    if (!command) {
        return NULL;
    }

    /*
     * On macOS, wrap git commands with timeout to prevent hangs from
     * credential prompts or network issues. The timeout command sends
     * SIGTERM after the specified duration.
     */
#ifdef __APPLE__
    #define GIT_TIMEOUT_SEC 5
    char timeout_cmd[4096];
    /* Use -k to ensure the command is killed even if it ignores SIGTERM */
    snprintf(timeout_cmd, sizeof(timeout_cmd), "timeout -k 1 %d %s", GIT_TIMEOUT_SEC, command);
    const char *effective_command = timeout_cmd;
#else
    const char *effective_command = command;
#endif

    FILE *fp = popen(effective_command, "r");
    if (!fp) {
        return NULL;
    }

    char *output = NULL;
    size_t output_size = 0;
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        char *new_output = realloc(output, output_size + len + 1);
        if (!new_output) {
            free(output);
            pclose(fp);
            return NULL;
        }
        output = new_output;
        memcpy(output + output_size, buffer, len);
        output_size += len;
        output[output_size] = '\0';
    }

    pclose(fp);

    // Trim trailing newline
    if (output && output_size > 0 && output[output_size-1] == '\n') {
        output[output_size-1] = '\0';
    }

    return output;
}

/**
 * Get git status information for the working directory.
 * Includes current branch, status (clean/modified), and recent commits.
 */
char* get_git_status(const char *working_dir) {
    if (!is_git_repo(working_dir)) {
        return NULL;
    }

    // Get current branch
    char *branch = exec_git_command("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    if (!branch) {
        branch = strdup("unknown");
        if (!branch) {
            return NULL;
        }
    }

    // Get git status (clean or modified)
    char *status_output = exec_git_command("git status --porcelain 2>/dev/null");
    const char *status = (status_output && strlen(status_output) > 0) ? "modified" : "clean";

    // Get recent commits (last 5)
    char *commits = exec_git_command("git log --oneline -5 2>/dev/null");
    if (!commits) {
        commits = strdup("(no commits)");
        if (!commits) {
            free(branch);
            free(status_output);
            return NULL;
        }
    }

    // Build the gitStatus block
    size_t total_size = 1024 + strlen(branch) + strlen(status) + strlen(commits);
    char *git_status = malloc(total_size);
    if (!git_status) {
        free(branch);
        free(status_output);
        free(commits);
        return NULL;
    }

    snprintf(git_status, total_size,
        "gitStatus: This is the git status at the start of the conversation. "
        "Note that this status is a snapshot in time, and will not update during the conversation.\n"
        "Current branch: %s\n\n"
        "Main branch (you will usually use this for PRs): \n\n"
        "Status:\n(%s)\n\n"
        "Recent commits:\n%s",
        branch, status, commits);

    free(branch);
    free(status_output);
    free(commits);

    return git_status;
}
