/*
 * perpetual_log.h - Append-only markdown log for perpetual mode sessions
 *
 * Manages .klawed/perpetual.md, recording each completed session turn
 * with timestamp, session ID, request, summary, files touched, and commit.
 */

#ifndef PERPETUAL_LOG_H
#define PERPETUAL_LOG_H

/* Resolve log path from KLAWED_PERPETUAL_FILE env var, or fall back to
 * data_dir + "/perpetual.md". Returns malloc'd string, caller must free.
 * data_dir may be NULL (falls back to ".klawed/perpetual.md"). */
char *perpetual_log_get_path(const char *data_dir);

/* Append a completed turn to the log file.
 * log_path:      path to perpetual.md
 * session_id:    short unique id for this session
 * request:       the user's original query (single line, truncated if needed)
 * summary:       what the agent did (may be multi-line)
 * files_touched: comma-separated list of files, or NULL
 * commit_hash:   git commit if applicable, or NULL
 * Returns 0 on success, -1 on error. */
int perpetual_log_append(const char *log_path,
                         const char *session_id,
                         const char *request,
                         const char *summary,
                         const char *files_touched,
                         const char *commit_hash);

/* Return file size in bytes. Returns 0 if file does not exist, -1 on error. */
long perpetual_log_size(const char *log_path);

#endif /* PERPETUAL_LOG_H */
