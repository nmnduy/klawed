/*
 * system_prompt.c - Build system prompt with environment context
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>

#include "system_prompt.h"
#include "environment.h"
#include "klawed_md.h"
#include "../logger.h"
#include "../util/env_utils.h"
#include "../util/timestamp_utils.h"

/**
 * Build complete system prompt with environment context.
 * Includes platform info, git status, KLAWED.md, and SKILLS directory.
 */
char* build_system_prompt(ConversationState *state) {
    if (!state) {
        return NULL;
    }

    const char *working_dir = state->working_dir;
    char *date = get_current_date();
    const char *platform = get_platform();
    char *os_version = get_os_version();
    int is_git = is_git_repo(working_dir);
    char *git_status = is_git ? get_git_status(working_dir) : NULL;
    char *klawed_md = read_klawed_md(working_dir);

    // Calculate required buffer size
    size_t prompt_size = 2048; // Base size for the prompt template
    if (git_status) {
        prompt_size += strlen(git_status);
    }
    if (klawed_md) {
        prompt_size += strlen(klawed_md) + 512; // Extra space for formatting
    }

    // Add space for additional directories
    for (int i = 0; i < state->additional_dirs_count; i++) {
        prompt_size += strlen(state->additional_dirs[i]) + 4; // path + ", " separator
    }

    char *prompt = malloc(prompt_size);
    if (!prompt) {
        free(date);
        free(os_version);
        free(git_status);
        free(klawed_md);
        return NULL;
    }

    // Build the system prompt with additional directories
    // Log plan mode when building system prompt
    LOG_DEBUG("[SYSTEM] build_system_prompt: plan_mode=%d", state->plan_mode);

    int offset = snprintf(prompt, prompt_size,
        "Here is useful information about the environment you are running in:\n"
        "<env>\n"
        "Planning mode: %s\n"
        "Working directory: %s\n"
        "Additional working directories: ",
        state->plan_mode ? "ENABLED - You can ONLY use read-only tools (Read, Glob, Grep, Sleep, UploadImage, TodoWrite). The Bash, Subagent, Write, and Edit tools are NOT available in planning mode." : "disabled",
        working_dir);

    // Add additional directories
    if (state->additional_dirs_count > 0) {
        for (int i = 0; i < state->additional_dirs_count; i++) {
            if (i > 0) {
                offset += snprintf(prompt + offset, prompt_size - (size_t)offset, ", ");
            }
            offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "%s", state->additional_dirs[i]);
        }
    }
    offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "\n");

    offset += snprintf(prompt + offset, prompt_size - (size_t)offset,
        "Is directory a git repo: %s\n"
        "Platform: %s\n"
        "OS Version: %s\n"
        "Today's date: %s\n"
        "</env>\n",
        is_git ? "Yes" : "No",
        platform,
        os_version,
        date);

    // Add git status if available
    if (git_status && offset < (int)prompt_size) {
        offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "\n%s\n", git_status);
    }

    // Add SKILLS directory information if it exists
    char skills_path[PATH_MAX];
    snprintf(skills_path, sizeof(skills_path), "%s/SKILLS", working_dir);
    if (access(skills_path, F_OK) == 0 && offset < (int)prompt_size) {
        offset += snprintf(prompt + offset, prompt_size - (size_t)offset,
            "\nSKILLS Directory: The SKILLS/ directory contains documentation, scripts, and resources that can help you complete tasks more effectively. "
            "When working on tasks, explore the SKILLS/ directory to find:\n"
            "- Documentation and guides for specific technologies or workflows\n"
            "- Helper scripts and automation tools\n"
            "- Templates and examples\n"
            "- Best practices and coding standards\n"
            "Use the Read, Glob, and Grep tools to explore SKILLS/ contents when they might be relevant to your current task.\n\n"
            "Available in SKILLS/:\n");
        
        // List top-level contents of SKILLS directory (up to 50 items)
        DIR *skills_dir = opendir(skills_path);
        if (skills_dir) {
            struct dirent *entry;
            int item_count = 0;
            int total_count = 0;
            const int max_display = 50;
            
            // First pass: count total items
            while ((entry = readdir(skills_dir)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    total_count++;
                }
            }
            rewinddir(skills_dir);
            
            // Second pass: list items
            while ((entry = readdir(skills_dir)) != NULL && offset < (int)prompt_size) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                
                if (item_count < max_display) {
                    offset += snprintf(prompt + offset, prompt_size - (size_t)offset, 
                                     "- %s\n", entry->d_name);
                    item_count++;
                }
            }
            
            // Add [...] if there are more items
            if (total_count > max_display && offset < (int)prompt_size) {
                offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "[...]\n");
            }
            
            closedir(skills_dir);
        }
        
        offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "\n");
    }

    // Note: DeepSeek API detection removed - no longer limiting tokens to 4096

    // Add KLAWED.md content if available
    if (klawed_md && offset < (int)prompt_size) {
        offset += snprintf(prompt + offset, prompt_size - (size_t)offset,
            "\n<system-reminder>\n"
            "As you answer the user's questions, you can use the following context:\n"
            "# klawedMd\n"
            "Codebase and user instructions are shown below. Be sure to adhere to these instructions. "
            "IMPORTANT: These instructions OVERRIDE any default behavior and you MUST follow them exactly as written.\n\n"
            "Contents of %s/KLAWED.md (project instructions, checked into the codebase):\n\n"
            "%s\n\n"
            "      IMPORTANT: this context may or may not be relevant to your tasks. "
            "You should not respond to this context unless it is highly relevant to your task.\n"
            "</system-reminder>\n",
            working_dir, klawed_md);
    }

    free(date);
    free(os_version);
    free(git_status);
    free(klawed_md);

    (void)offset; // Suppress unused variable warning after final snprintf

    return prompt;
}
