/*
 * history_file.h - Persistent input history using a flat file (one entry per line)
 */

#ifndef HISTORY_FILE_H
#define HISTORY_FILE_H

#include <stddef.h>

typedef struct HistoryFile HistoryFile;

// Open flat history file at path (or default when NULL/empty)
HistoryFile* history_file_open(const char *path);

// Close file and free resources
void history_file_close(HistoryFile *hf);

// Default history path resolution
// Priority: $CLAUDE_C_HISTORY_FILE_PATH > ./.claude-c/input_history.txt > $XDG_DATA_HOME/claude-c/input_history.txt > ~/.local/share/claude-c/input_history.txt
char* history_file_default_path(void);

// Load most recent N lines; returns array oldest -> newest
// Caller frees each string and the array
char** history_file_load_recent(HistoryFile *hf, int limit, int *out_count);

// Append a line (adds trailing '\n'). Skips empty strings.
int history_file_append(HistoryFile *hf, const char *text);

#ifdef TEST_BUILD
// Internal functions exposed for testing
char* escape_newlines(const char *text);
char* unescape_newlines(const char *escaped_text);
#endif

#endif // HISTORY_FILE_H

