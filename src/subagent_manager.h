/*
 * Subagent Manager - Track and monitor running subagent processes
 *
 * This module provides tracking for active subagent processes spawned by
 * the Subagent tool, allowing the TUI to display their status and log output
 * in real-time during terminal redraws.
 *
 * Process Group Management:
 * Subagents are spawned in their own process group (via setsid() or setpgid()).
 * When terminating subagents, signals are sent to the entire process group
 * using kill(-pid, sig), ensuring that all child processes spawned by the
 * subagent (e.g., Bash commands, nested processes) are also terminated.
 * This prevents orphan processes when klawed is killed.
 */

#ifndef SUBAGENT_MANAGER_H
#define SUBAGENT_MANAGER_H

#include <sys/types.h>
#include <pthread.h>
#include <time.h>

typedef struct SubagentProcess {
    pid_t pid;                     // Process ID
    char *log_file;                // Log file path (owned, must be freed)
    char *prompt;                  // Original prompt (owned, must be freed)
    time_t start_time;             // When the subagent was started
    int timeout_seconds;           // Timeout in seconds (0 = no timeout)
    int completed;                 // Whether the process has completed
    int exit_code;                 // Exit code if completed
    char *last_log_tail;           // Last N lines of log (owned, must be freed)
    int tail_lines;                // Number of lines in last_log_tail
} SubagentProcess;

typedef struct SubagentManager {
    SubagentProcess **processes;   // Array of tracked subagent processes (owned)
    int process_count;             // Number of tracked processes
    int process_capacity;          // Capacity of processes array
    pthread_mutex_t mutex;         // Synchronize access to process list
    int mutex_initialized;         // Tracks mutex initialization
} SubagentManager;

// Initialize a SubagentManager
// Returns: 0 on success, -1 on failure
int subagent_manager_init(SubagentManager *manager);

// Cleanup and free a SubagentManager
void subagent_manager_free(SubagentManager *manager);

// Add a new subagent process to track
// manager: SubagentManager instance
// pid: Process ID of the subagent
// log_file: Path to the log file (will be copied)
// prompt: Original prompt (will be copied)
// timeout_seconds: Timeout in seconds (0 = no timeout)
// Returns: 0 on success, -1 on failure
int subagent_manager_add(SubagentManager *manager, pid_t pid, const char *log_file,
                         const char *prompt, int timeout_seconds);

// Update status of all tracked subagents (check if still running, read log tails)
// manager: SubagentManager instance
// tail_lines: Number of lines to read from end of each log (default: 10)
// Returns: 0 on success, -1 on failure
int subagent_manager_update_all(SubagentManager *manager, int tail_lines);

// Remove completed subagents from tracking
// manager: SubagentManager instance
// keep_recent: Number of recently completed subagents to keep (0 = remove all completed)
// Returns: Number of processes removed
int subagent_manager_cleanup_completed(SubagentManager *manager, int keep_recent);

// Get count of running subagents
// Returns: Number of running (non-completed) subagents
int subagent_manager_get_running_count(SubagentManager *manager);

// Get a specific subagent process by index (thread-safe, returns a copy)
// manager: SubagentManager instance
// index: Index of the process (0 to process_count-1)
// out_process: Output parameter for process data (caller must free strings)
// Returns: 0 on success, -1 if index out of bounds
int subagent_manager_get_process(SubagentManager *manager, int index, SubagentProcess *out_process);

// Terminate all running subagents (sends SIGTERM, then SIGKILL if needed)
// Signals are sent to the entire process group (-pid) to ensure child processes
// spawned by the subagent are also terminated.
// manager: SubagentManager instance
// grace_period_ms: Milliseconds to wait after SIGTERM before sending SIGKILL (default: 2000ms)
// Returns: Number of processes terminated
int subagent_manager_terminate_all(SubagentManager *manager, int grace_period_ms);

#endif // SUBAGENT_MANAGER_H
