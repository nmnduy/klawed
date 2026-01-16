#ifndef DIFF_UTILS_H
#define DIFF_UTILS_H

/**
 * Diff Utilities
 * 
 * Helper functions for generating and displaying diffs.
 */

/**
 * Show unified diff between original content and current file
 * Uses external diff command and emits colorized output
 * @param file_path Path to current file
 * @param original_content Original content to compare against
 * @return 0 on success, -1 on error
 */
int show_diff(const char *file_path, const char *original_content);

#endif // DIFF_UTILS_H
