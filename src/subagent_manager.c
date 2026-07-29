/*
 * Subagent Manager Implementation
 */

#include "subagent_manager.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <bsd/string.h>

#define INITIAL_CAPACITY 4

int subagent_manager_init(SubagentManager *manager) {
    if (!manager) {
        return -1;
    }

    memset(manager, 0, sizeof(SubagentManager));

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize subagent manager mutex");
        return -1;
    }

    manager->mutex_initialized = 1;
    manager->process_capacity = INITIAL_CAPACITY;
    manager->processes = calloc((size_t)manager->process_capacity, sizeof(SubagentProcess*));

    if (!manager->processes) {
        pthread_mutex_destroy(&manager->mutex);
        manager->mutex_initialized = 0;
        return -1;
    }

    return 0;
}

void subagent_manager_free(SubagentManager *manager) {
    if (!manager) {
        return;
    }

    // First, terminate all running subagents
    // Note: This temporarily releases the lock internally
    int terminated = subagent_manager_terminate_all(manager, 2000);
    if (terminated > 0) {
        LOG_INFO("SubagentManager: Terminated %d running subagent(s) during cleanup", terminated);
    }

    if (manager->mutex_initialized) {
        pthread_mutex_lock(&manager->mutex);
    }

    // Free all tracked processes
    for (int i = 0; i < manager->process_count; i++) {
        if (manager->processes[i]) {
            free(manager->processes[i]->log_file);
            free(manager->processes[i]->prompt);
            free(manager->processes[i]->last_log_tail);

            free(manager->processes[i]);
        }
    }

    free(manager->processes);
    manager->processes = NULL;
    manager->process_count = 0;
    manager->process_capacity = 0;

    if (manager->mutex_initialized) {
        pthread_mutex_unlock(&manager->mutex);
        pthread_mutex_destroy(&manager->mutex);
        manager->mutex_initialized = 0;
    }
}

int subagent_manager_add(SubagentManager *manager, pid_t pid, const char *log_file,
                         const char *prompt, int timeout_seconds) {
    if (!manager || !log_file || !prompt || pid <= 0) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    // Expand capacity if needed
    if (manager->process_count >= manager->process_capacity) {
        int new_capacity = manager->process_capacity * 2;
        SubagentProcess **new_processes = realloc(manager->processes,
                                                   (size_t)new_capacity * sizeof(SubagentProcess*));
        if (!new_processes) {
            pthread_mutex_unlock(&manager->mutex);
            return -1;
        }
        manager->processes = new_processes;
        manager->process_capacity = new_capacity;
    }

    // Allocate new process entry
    SubagentProcess *proc = calloc(1, sizeof(SubagentProcess));
    if (!proc) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    proc->pid = pid;
    proc->log_file = strdup(log_file);
    proc->prompt = strdup(prompt);
    proc->start_time = time(NULL);
    proc->timeout_seconds = timeout_seconds;
    proc->completed = 0;
    proc->exit_code = -1;
    proc->last_log_tail = NULL;
    proc->tail_lines = 0;

    if (!proc->log_file || !proc->prompt) {
        free(proc->log_file);
        free(proc->prompt);
        free(proc);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    manager->processes[manager->process_count++] = proc;

    pthread_mutex_unlock(&manager->mutex);

    LOG_INFO("SubagentManager: Added subagent PID %d, log: %s", pid, log_file);
    return 0;
}

// Helper: Read last N lines from a file
// Uses fseek from end to avoid reading the entire file when logs are large.
static char* read_file_tail(const char *log_file, int tail_lines, int *out_lines_read) {
    if (!log_file || tail_lines <= 0) {
        if (out_lines_read) *out_lines_read = 0;
        return NULL;
    }

    FILE *fp = fopen(log_file, "r");
    if (!fp) {
        if (out_lines_read) *out_lines_read = 0;
        return NULL;
    }

    /* Seek to end and read backwards to find the last N lines.
     * This avoids the O(file_size) cost of reading the entire file
     * line-by-line, which caused 200%+ CPU when subagent logs were large. */

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        if (out_lines_read) *out_lines_read = 0;
        return NULL;
    }

    /* Read backwards in chunks to find the start of the last N lines.
     * We read a chunk from the end, count newlines, and expand backward
     * until we have enough lines or reach the beginning. */
    const long chunk_size = 4096;
    int newlines_found = 0;
    long read_offset = file_size;
    long search_pos = file_size;

    /* We need tail_lines newlines. The last line may not end with \n,
     * so we count it as a line if there's content. */
    char *chunk_buf = malloc((size_t)chunk_size + 1);
    if (!chunk_buf) {
        fclose(fp);
        if (out_lines_read) *out_lines_read = 0;
        return NULL;
    }

    while (search_pos > 0 && newlines_found <= tail_lines) {
        long this_chunk = (search_pos < chunk_size) ? search_pos : chunk_size;
        search_pos -= this_chunk;
        fseek(fp, search_pos, SEEK_SET);
        size_t bytes_read = fread(chunk_buf, 1, (size_t)this_chunk, fp);
        chunk_buf[bytes_read] = '\0';

        for (long i = (long)bytes_read - 1; i >= 0; i--) {
            if (chunk_buf[i] == '\n') {
                newlines_found++;
                if (newlines_found > tail_lines) {
                    /* The line starting position is just after this newline */
                    read_offset = search_pos + i + 1;
                    break;
                }
            }
        }
        if (newlines_found > tail_lines) {
            break;
        }
        read_offset = search_pos;
    }

    /* Edge case: if file doesn't end with newline, count the last partial line */
    if (newlines_found <= tail_lines) {
        read_offset = 0;
    }

    free(chunk_buf);

    /* Now read from read_offset to end of file */
    fseek(fp, read_offset, SEEK_SET);

    /* Estimate buffer size: remaining bytes + some headroom */
    long remaining = file_size - read_offset;
    if (remaining < 0) remaining = 0;
    size_t buffer_size = (size_t)remaining + 1;
    if (buffer_size < 64) buffer_size = 64;

    char *tail_output = malloc(buffer_size);
    if (!tail_output) {
        fclose(fp);
        if (out_lines_read) *out_lines_read = 0;
        return NULL;
    }

    size_t tail_size = fread(tail_output, 1, (size_t)remaining, fp);
    tail_output[tail_size] = '\0';

    fclose(fp);

    /* Count lines read for the out parameter */
    int lines_read = 0;
    for (size_t i = 0; i < tail_size; i++) {
        if (tail_output[i] == '\n') lines_read++;
    }
    /* If there's content after the last newline, count that as a line too */
    if (tail_size > 0 && tail_output[tail_size - 1] != '\n') {
        lines_read++;
    }

    if (out_lines_read) {
        *out_lines_read = lines_read;
    }

    return tail_output;
}

int subagent_manager_update_all(SubagentManager *manager, int tail_lines) {
    if (!manager) {
        return -1;
    }

    if (tail_lines <= 0) {
        tail_lines = 10;  // Default: 10 lines
    }

    pthread_mutex_lock(&manager->mutex);

    for (int i = 0; i < manager->process_count; i++) {
        SubagentProcess *proc = manager->processes[i];
        if (!proc || proc->completed) {
            continue;
        }

        // Check if process is still running
        int status;
        pid_t result = waitpid(proc->pid, &status, WNOHANG);

        if (result == 0) {
            // Still running
            proc->completed = 0;
        } else if (result == proc->pid) {
            // Process has terminated
            proc->completed = 1;
            if (WIFEXITED(status)) {
                proc->exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                proc->exit_code = -WTERMSIG(status);
            }
            LOG_INFO("SubagentManager: PID %d completed with exit code %d", proc->pid, proc->exit_code);
        } else if (result == -1 && errno == ECHILD) {
            // Process doesn't exist
            proc->completed = 1;
            proc->exit_code = -999;
        }

        // Read log tail (for both running and recently completed processes)
        free(proc->last_log_tail);
        proc->last_log_tail = read_file_tail(proc->log_file, tail_lines, &proc->tail_lines);
    }

    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int subagent_manager_cleanup_completed(SubagentManager *manager, int keep_recent) {
    if (!manager) {
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    int removed = 0;
    int completed_count = 0;

    // Count completed processes
    for (int i = 0; i < manager->process_count; i++) {
        if (manager->processes[i] && manager->processes[i]->completed) {
            completed_count++;
        }
    }

    // Remove old completed processes (keep the most recent ones)
    for (int i = manager->process_count - 1; i >= 0; i--) {
        SubagentProcess *proc = manager->processes[i];
        if (proc && proc->completed) {
            // Keep the N most recent completed processes
            if (completed_count > keep_recent) {
                // Free this process
                free(proc->log_file);
                free(proc->prompt);
                free(proc->last_log_tail);

                free(proc);

                // Shift remaining processes down
                for (int j = i; j < manager->process_count - 1; j++) {
                    manager->processes[j] = manager->processes[j + 1];
                }
                manager->process_count--;
                removed++;
                completed_count--;
            }
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return removed;
}

int subagent_manager_get_running_count(SubagentManager *manager) {
    if (!manager) {
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    int running_count = 0;
    for (int i = 0; i < manager->process_count; i++) {
        if (manager->processes[i] && !manager->processes[i]->completed) {
            running_count++;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return running_count;
}

int subagent_manager_get_process(SubagentManager *manager, int index, SubagentProcess *out_process) {
    if (!manager || !out_process || index < 0) {
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    if (index >= manager->process_count) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    SubagentProcess *proc = manager->processes[index];
    if (!proc) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    // Copy process data (caller must free allocated strings)
    memset(out_process, 0, sizeof(SubagentProcess));
    out_process->pid = proc->pid;
    out_process->log_file = proc->log_file ? strdup(proc->log_file) : NULL;
    out_process->prompt = proc->prompt ? strdup(proc->prompt) : NULL;
    out_process->start_time = proc->start_time;
    out_process->timeout_seconds = proc->timeout_seconds;
    out_process->completed = proc->completed;
    out_process->exit_code = proc->exit_code;
    out_process->last_log_tail = proc->last_log_tail ? strdup(proc->last_log_tail) : NULL;
    out_process->tail_lines = proc->tail_lines;

    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int subagent_manager_terminate_all(SubagentManager *manager, int grace_period_ms) {
    if (!manager) {
        return 0;
    }

    if (grace_period_ms <= 0) {
        grace_period_ms = 2000;  // Default: 2 seconds
    }

    pthread_mutex_lock(&manager->mutex);

    int terminated_count = 0;
    int still_running_count = 0;
    pid_t *running_pids = NULL;

    // First pass: Send SIGTERM to all running subagents
    // Use negative PID to send signal to entire process group (subagent + its children)
    for (int i = 0; i < manager->process_count; i++) {
        SubagentProcess *proc = manager->processes[i];
        if (!proc || proc->completed) {
            continue;
        }

        // Check if still running
        int status;
        pid_t result = waitpid(proc->pid, &status, WNOHANG);
        if (result == 0) {
            // Still running - send SIGTERM to the entire process group
            // Using -pid sends to all processes in the group led by pid
            if (kill(-proc->pid, SIGTERM) == 0) {
                LOG_INFO("SubagentManager: Sent SIGTERM to subagent process group %d", proc->pid);
                still_running_count++;
            } else if (errno == ESRCH) {
                // Process group doesn't exist - try direct process kill as fallback
                if (kill(proc->pid, SIGTERM) == 0) {
                    LOG_INFO("SubagentManager: Sent SIGTERM to subagent PID %d (no process group)", proc->pid);
                    still_running_count++;
                } else if (errno == ESRCH) {
                    // Process doesn't exist
                    proc->completed = 1;
                    proc->exit_code = -999;
                } else {
                    LOG_WARN("SubagentManager: Failed to send SIGTERM to PID %d: %s",
                             proc->pid, strerror(errno));
                }
            } else {
                LOG_WARN("SubagentManager: Failed to send SIGTERM to process group %d: %s",
                         proc->pid, strerror(errno));
            }
        } else {
            // Already terminated
            proc->completed = 1;
            if (WIFEXITED(status)) {
                proc->exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                proc->exit_code = -WTERMSIG(status);
            }
        }
    }

    // If we sent any SIGTERM signals, wait for grace period
    if (still_running_count > 0) {
        LOG_INFO("SubagentManager: Waiting %d ms for %d subagent(s) to terminate gracefully",
                 grace_period_ms, still_running_count);

        pthread_mutex_unlock(&manager->mutex);

        // Convert milliseconds to microseconds for usleep
        usleep((useconds_t)(grace_period_ms * 1000));

        pthread_mutex_lock(&manager->mutex);

        // Allocate array to track PIDs that are still running
        running_pids = calloc((size_t)still_running_count, sizeof(pid_t));
        if (!running_pids) {
            LOG_ERROR("SubagentManager: Failed to allocate memory for PID tracking");
            pthread_mutex_unlock(&manager->mutex);
            return terminated_count;
        }

        int running_idx = 0;

        // Second pass: Check status and send SIGKILL if needed
        // Use negative PID to send to entire process group
        for (int i = 0; i < manager->process_count; i++) {
            SubagentProcess *proc = manager->processes[i];
            if (!proc || proc->completed) {
                continue;
            }

            int status;
            pid_t result = waitpid(proc->pid, &status, WNOHANG);

            if (result == 0) {
                // Still running after grace period - force kill entire process group
                running_pids[running_idx++] = proc->pid;
                if (kill(-proc->pid, SIGKILL) == 0) {
                    LOG_WARN("SubagentManager: Sent SIGKILL to stubborn subagent process group %d", proc->pid);
                    terminated_count++;
                } else if (errno == ESRCH) {
                    // Process group doesn't exist - try direct kill as fallback
                    if (kill(proc->pid, SIGKILL) == 0) {
                        LOG_WARN("SubagentManager: Sent SIGKILL to stubborn subagent PID %d (no process group)", proc->pid);
                        terminated_count++;
                    } else if (errno == ESRCH) {
                        // Race condition: process terminated between checks
                        proc->completed = 1;
                        proc->exit_code = -999;
                        terminated_count++;
                    } else {
                        LOG_ERROR("SubagentManager: Failed to send SIGKILL to PID %d: %s",
                                 proc->pid, strerror(errno));
                    }
                } else {
                    LOG_ERROR("SubagentManager: Failed to send SIGKILL to process group %d: %s",
                             proc->pid, strerror(errno));
                }
            } else if (result == proc->pid) {
                // Terminated gracefully
                proc->completed = 1;
                if (WIFEXITED(status)) {
                    proc->exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    proc->exit_code = -WTERMSIG(status);
                }
                terminated_count++;
                LOG_INFO("SubagentManager: Subagent PID %d terminated gracefully", proc->pid);
            } else if (result == -1 && errno == ECHILD) {
                // Process doesn't exist
                proc->completed = 1;
                proc->exit_code = -999;
                terminated_count++;
            }
        }

        // Wait a bit for SIGKILL'd processes to fully terminate
        if (running_idx > 0) {
            pthread_mutex_unlock(&manager->mutex);
            usleep(100000);  // 100ms
            pthread_mutex_lock(&manager->mutex);

            // Final reap
            for (int i = 0; i < running_idx; i++) {
                waitpid(running_pids[i], NULL, WNOHANG);
            }
        }

        free(running_pids);
    }

    pthread_mutex_unlock(&manager->mutex);

    if (terminated_count > 0) {
        LOG_INFO("SubagentManager: Terminated %d subagent process(es)", terminated_count);
    }

    return terminated_count;
}
