/*
 * tool_utils.h - Helper utilities for tool argument summarization
 */

#ifndef TOOL_UTILS_H
#define TOOL_UTILS_H

#include <stddef.h>

// Summarize a bash command for display purposes.
// - Writes a concise preview into `out` (NUL-terminated).
// - If the command begins with "cd <dir> &&" or "cd <dir>;" and <dir> is the
//   current working directory, the leading cd segment is stripped.
// - The output is truncated to fit `outsz` (including NUL). If truncation
//   occurs and there is room (outsz > 4), an ellipsis "..." is appended.
// Returns 0 on success.
int summarize_bash_command(const char *cmd, char *out, size_t outsz);

#endif // TOOL_UTILS_H

