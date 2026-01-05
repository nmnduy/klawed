/*
 * process_utils.c - Process management utilities for safe command execution
 */

#include "process_utils.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <bsd/string.h>
#include <time.h>

// Execute a command with timeout and interrupt support
// Returns: exit code, output in *output (caller must free), *timed_out flag
int execute_command_with_timeout(
    const char *command,
    int timeout_seconds,
    int *timed_out,
    char **output,
    size_t *output_size,
    volatile int *interrupt_requested
) {
    if (!command || !timed_out || !output || !output_size || !interrupt_requested) {
        return -1;
    }

    *timed_out = 0;
    *output = NULL;
    *output_size = 0;

    // Create pipes for stdout/stderr
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        LOG_ERROR("Failed to create pipes: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOG_ERROR("Failed to fork: %s", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        // Close read ends
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        // Redirect stdout and stderr to pipes
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        // Close write ends (now duplicated)
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Redirect stdin from /dev/null
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        // Create a new process group so we can kill all children
        setpgid(0, 0);

        // Execute the command
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);

        // If we get here, exec failed
        _exit(127);  // Command not found
    }

    // Parent process
    // Close write ends
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Set pipes to non-blocking
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    // Read output from pipes
    char buffer[4096];
    size_t total_size = 0;
    char *result = NULL;
    time_t start_time = time(NULL);
    int process_exited = 0;
    int exit_status = -1;

    while (!process_exited) {
        // Check for interrupt
        if (*interrupt_requested) {
            LOG_DEBUG("Command interrupted by user");
            kill(-pid, SIGTERM);  // Kill entire process group
            usleep(100000);  // 100ms
            kill(-pid, SIGKILL);
            break;
        }

        // Check for timeout
        time_t current_time = time(NULL);
        if (timeout_seconds > 0 && (current_time - start_time) >= timeout_seconds) {
            LOG_DEBUG("Command timed out after %d seconds", timeout_seconds);
            *timed_out = 1;
            kill(-pid, SIGTERM);  // Kill entire process group
            usleep(100000);  // 100ms
            kill(-pid, SIGKILL);
            break;
        }

        // Check if process has exited
        int status;
        pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            process_exited = 1;
            if (WIFEXITED(status)) {
                exit_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_status = 128 + WTERMSIG(status);  // Standard shell convention
            }
        } else if (wait_result == -1) {
            LOG_ERROR("waitpid failed: %s", strerror(errno));
            break;
        }

        // Read available output from stdout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(stdout_pipe[0], &readfds);
        FD_SET(stderr_pipe[0], &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms

        int max_fd = (stdout_pipe[0] > stderr_pipe[0]) ? stdout_pipe[0] : stderr_pipe[0];
        int select_result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (select_result > 0) {
            // Read from stdout
            if (FD_ISSET(stdout_pipe[0], &readfds)) {
                ssize_t bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';

                    // Append to result
                    char *new_result = realloc(result, total_size + (size_t)bytes_read + 1);
                    if (!new_result) {
                        LOG_ERROR("Failed to allocate memory for command output");
                        free(result);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        kill(-pid, SIGKILL);
                        waitpid(pid, NULL, 0);
                        return -1;
                    }
                    result = new_result;
                    memcpy(result + total_size, buffer, (size_t)bytes_read);
                    total_size += (size_t)bytes_read;
                    result[total_size] = '\0';
                } else if (bytes_read == 0) {
                    // EOF on stdout
                    close(stdout_pipe[0]);
                    stdout_pipe[0] = -1;
                }
            }

            // Read from stderr (append to same output)
            if (FD_ISSET(stderr_pipe[0], &readfds)) {
                ssize_t bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';

                    // Append to result
                    char *new_result = realloc(result, total_size + (size_t)bytes_read + 1);
                    if (!new_result) {
                        LOG_ERROR("Failed to allocate memory for command output");
                        free(result);
                        close(stdout_pipe[0]);
                        close(stderr_pipe[0]);
                        kill(-pid, SIGKILL);
                        waitpid(pid, NULL, 0);
                        return -1;
                    }
                    result = new_result;
                    memcpy(result + total_size, buffer, (size_t)bytes_read);
                    total_size += (size_t)bytes_read;
                    result[total_size] = '\0';
                } else if (bytes_read == 0) {
                    // EOF on stderr
                    close(stderr_pipe[0]);
                    stderr_pipe[0] = -1;
                }
            }
        } else if (select_result == -1 && errno != EINTR) {
            LOG_ERROR("select failed: %s", strerror(errno));
            break;
        }

        // If both pipes are closed and process hasn't exited yet, wait a bit
        if ((stdout_pipe[0] == -1 || !FD_ISSET(stdout_pipe[0], &readfds)) &&
            (stderr_pipe[0] == -1 || !FD_ISSET(stderr_pipe[0], &readfds)) &&
            !process_exited) {
            usleep(100000);  // 100ms
        }
    }

    // Clean up
    if (stdout_pipe[0] != -1) close(stdout_pipe[0]);
    if (stderr_pipe[0] != -1) close(stderr_pipe[0]);

    // Wait for process to exit if it hasn't already
    if (!process_exited) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_status = 128 + WTERMSIG(status);
        }
    }

    *output = result;
    *output_size = total_size;

    if (*timed_out) {
        return -2;  // Special timeout code
    }

    return exit_status;
}
