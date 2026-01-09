/*
 * Subagent Manager Implementation
 */

#include "subagent_manager.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
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

            // Free environment variables
            if (manager->processes[i]->env_vars) {
                for (int j = 0; j < manager->processes[i]->env_var_count; j++) {
                    free(manager->processes[i]->env_vars[j]);
                }
                free(manager->processes[i]->env_vars);
            }

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
                         const char *prompt, int timeout_seconds,
                         const char **env_vars, int env_var_count) {
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
    proc->env_vars = NULL;
    proc->env_var_count = 0;

    if (!proc->log_file || !proc->prompt) {
        free(proc->log_file);
        free(proc->prompt);
        free(proc);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    // Copy environment variables if provided
    if (env_vars && env_var_count > 0) {
        proc->env_vars = calloc((size_t)env_var_count, sizeof(char*));
        if (!proc->env_vars) {
            free(proc->log_file);
            free(proc->prompt);
            free(proc);
            pthread_mutex_unlock(&manager->mutex);
            return -1;
        }

        proc->env_var_count = env_var_count;
        for (int i = 0; i < env_var_count; i++) {
            if (env_vars[i]) {
                proc->env_vars[i] = strdup(env_vars[i]);
                if (!proc->env_vars[i]) {
                    // Cleanup on failure
                    for (int j = 0; j < i; j++) {
                        free(proc->env_vars[j]);
                    }
                    free(proc->env_vars);
                    free(proc->log_file);
                    free(proc->prompt);
                    free(proc);
                    pthread_mutex_unlock(&manager->mutex);
                    return -1;
                }
            }
        }
    }

    manager->processes[manager->process_count++] = proc;

    pthread_mutex_unlock(&manager->mutex);

    LOG_INFO("SubagentManager: Added subagent PID %d, log: %s", pid, log_file);
    return 0;
}

// Helper: Read last N lines from a file
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

    // Count total lines
    int total_lines = 0;
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        total_lines++;
    }

    // Calculate start line
    int start_line = (total_lines > tail_lines) ? (total_lines - tail_lines) : 0;
    int lines_to_read = (total_lines > tail_lines) ? tail_lines : total_lines;

    // Allocate buffer for tail output (conservative estimate: 256 bytes per line)
    size_t buffer_size = (size_t)(lines_to_read * 256 + 1);
    char *tail_output = malloc(buffer_size);
    if (!tail_output) {
        fclose(fp);
        if (out_lines_read) *out_lines_read = 0;
        return NULL;
    }
    tail_output[0] = '\0';

    // Read the tail content
    rewind(fp);
    int current_line = 0;
    size_t tail_size = 0;

    while (fgets(line, sizeof(line), fp) && current_line < total_lines) {
        if (current_line >= start_line) {
            size_t line_len = strlen(line);

            // Ensure we have space (double buffer if needed)
            if (tail_size + line_len + 1 >= buffer_size) {
                buffer_size *= 2;
                char *new_output = realloc(tail_output, buffer_size);
                if (!new_output) {
                    free(tail_output);
                    fclose(fp);
                    if (out_lines_read) *out_lines_read = 0;
                    return NULL;
                }
                tail_output = new_output;
            }

            // Append line using safe string functions
            strlcpy(tail_output + tail_size, line, buffer_size - tail_size);
            tail_size += line_len;
        }
        current_line++;
    }

    fclose(fp);

    if (out_lines_read) {
        *out_lines_read = lines_to_read;
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

                // Free environment variables
                if (proc->env_vars) {
                    for (int k = 0; k < proc->env_var_count; k++) {
                        free(proc->env_vars[k]);
                    }
                    free(proc->env_vars);
                }

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

    // Copy environment variables
    out_process->env_var_count = proc->env_var_count;
    if (proc->env_vars && proc->env_var_count > 0) {
        out_process->env_vars = calloc((size_t)proc->env_var_count, sizeof(char*));
        if (out_process->env_vars) {
            for (int i = 0; i < proc->env_var_count; i++) {
                if (proc->env_vars[i]) {
                    out_process->env_vars[i] = strdup(proc->env_vars[i]);
                }
            }
        }
    } else {
        out_process->env_vars = NULL;
    }

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
