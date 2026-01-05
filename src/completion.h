/*
 * completion.h - Path and File Completion Utilities
 *
 * Provides filesystem-based completion for paths and directories
 */

#ifndef COMPLETION_H
#define COMPLETION_H

#include "ncurses_input.h"  // For CompletionResult and CompletionFn types

/**
 * Complete file paths (files and directories)
 *
 * @param partial  Partial path to complete
 * @param ctx      Optional context (unused for now)
 * @return         CompletionResult with matches, or NULL on error
 */
CompletionResult* complete_filepath(const char *partial, void *ctx);

/**
 * Complete directory paths only
 *
 * @param partial  Partial path to complete
 * @param ctx      Optional context (unused for now)
 * @return         CompletionResult with matches, or NULL on error
 */
CompletionResult* complete_dirpath(const char *partial, void *ctx);

/**
 * Free a CompletionResult (handles both arena and regular allocations)
 * This should be used instead of ncurses_completion_free for completion results
 * created by completion.c functions.
 *
 * @param result  CompletionResult to free
 */
void completion_free_result(CompletionResult *result);

#endif // COMPLETION_H
