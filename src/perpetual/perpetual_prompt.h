#ifndef PERPETUAL_PROMPT_H
#define PERPETUAL_PROMPT_H

// Build the system prompt for perpetual mode.
// log_path: absolute or relative path to perpetual.md
// log_size_bytes: current size of the log (0 if new/empty)
// Returns a malloc'd string the caller must free. Returns NULL on OOM.
char *perpetual_prompt_build(const char *log_path, long log_size_bytes);

#endif // PERPETUAL_PROMPT_H
