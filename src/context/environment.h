/*
 * environment.h - Platform and environment detection for system prompt
 *
 * Functions to detect git repository status, execute git commands,
 * and gather environment information for the system prompt.
 */

#ifndef CONTEXT_ENVIRONMENT_H
#define CONTEXT_ENVIRONMENT_H

/**
 * Check if the working directory is a git repository.
 *
 * Returns: 1 if git repo, 0 otherwise
 */
int is_git_repo(const char *working_dir);

/**
 * Execute a git command and return its output.
 * Caller must free the returned string.
 *
 * Returns: Newly allocated string with command output, or NULL on failure
 */
char* exec_git_command(const char *command);

/**
 * Get git status information for the working directory.
 * Includes current branch, status (clean/modified), and recent commits.
 * Caller must free the returned string.
 *
 * Returns: Newly allocated string with formatted git status, or NULL if not a git repo
 */
char* get_git_status(const char *working_dir);

#endif /* CONTEXT_ENVIRONMENT_H */
