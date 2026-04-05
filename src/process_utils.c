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

#ifdef __APPLE__
#include <spawn.h>
/* For posix_spawn file actions */
extern char **environ;
#endif

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

#ifdef __APPLE__
    /*
     * macOS: Use posix_spawn() instead of fork() for thread safety.
     *
     * When fork() is called in a multi-threaded program on macOS, only the
     * calling thread survives in the child. If other threads held locks
     * (malloc, SQLite, log mutex, etc.), those locks remain locked forever
     * in the child, causing deadlocks. This manifests as TUI freezes when
     * multiple Bash tools run in parallel.
     *
     * posix_spawn() avoids this by creating a new process without copying
     * the parent's memory space and mutex states.
     */
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t spawnattr;
    pid_t pid;

    int rc = posix_spawn_file_actions_init(&file_actions);
    if (rc != 0) {
        LOG_ERROR("Failed to init file actions: %s", strerror(rc));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    rc = posix_spawnattr_init(&spawnattr);
    if (rc != 0) {
        LOG_ERROR("Failed to init spawn attributes: %s", strerror(rc));
        posix_spawn_file_actions_destroy(&file_actions);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    // Set up file actions: redirect stdout/stderr to pipes, stdin from /dev/null
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[0]);  // Close read end
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe[0]);  // Close read end
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[1]);  // Close original after dup
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe[1]);  // Close original after dup
    posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    // Set spawn attributes: create new process group for kill(-pid, sig)
    short flags = POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&spawnattr, flags);
    posix_spawnattr_setpgroup(&spawnattr, 0);  // New process group

    // Build argv for shell execution (posix_spawn requires non-const char*)
    char shell_name[] = "sh";
    char dash_c[] = "-c";
    // Need a mutable copy of command since posix_spawn may modify argv
    char *command_copy = strdup(command);
    if (!command_copy) {
        LOG_ERROR("Failed to allocate memory for command copy");
        posix_spawn_file_actions_destroy(&file_actions);
        posix_spawnattr_destroy(&spawnattr);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }
    char *argv[] = {shell_name, dash_c, command_copy, NULL};

    rc = posix_spawn(&pid, "/bin/sh", &file_actions, &spawnattr, argv, environ);
    free(command_copy);  // Safe to free after posix_spawn returns

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&spawnattr);

    if (rc != 0) {
        LOG_ERROR("Failed to spawn process: %s", strerror(rc));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }
#else
    /*
     * Linux: Use the traditional fork()/exec() approach.
     *
     * Linux has been working fine with fork() in multi-threaded programs,
     * so we keep the original implementation to avoid any regressions.
     */
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
#endif

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

    // Track whether both pipes have been closed (EOF reached)
    int stdout_eof = 0;
    int stderr_eof = 0;

    while (!process_exited || !stdout_eof || !stderr_eof) {
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

        // Check if process has exited (only if not already known)
        if (!process_exited) {
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
        }

        // Read available output from pipes
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;
        if (stdout_pipe[0] != -1) {
            FD_SET(stdout_pipe[0], &readfds);
            if (stdout_pipe[0] > max_fd) max_fd = stdout_pipe[0];
        }
        if (stderr_pipe[0] != -1) {
            FD_SET(stderr_pipe[0], &readfds);
            if (stderr_pipe[0] > max_fd) max_fd = stderr_pipe[0];
        }

        // If both pipes are closed, we're done reading
        if (max_fd == -1) {
            stdout_eof = 1;
            stderr_eof = 1;
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms

        int select_result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (select_result > 0) {
            // Read from stdout
            if (stdout_pipe[0] != -1 && FD_ISSET(stdout_pipe[0], &readfds)) {
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
                    stdout_eof = 1;
                }
            }

            // Read from stderr (append to same output)
            if (stderr_pipe[0] != -1 && FD_ISSET(stderr_pipe[0], &readfds)) {
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
                    stderr_eof = 1;
                }
            }
        } else if (select_result == -1 && errno != EINTR) {
            LOG_ERROR("select failed: %s", strerror(errno));
            break;
        } else if (select_result == 0) {
            // Timeout on select - if process exited and both pipes are at EOF, we're done
            // Otherwise, keep waiting for more data
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
