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

// Forward declaration for internal use
static int read_output_from_pipes(int stdout_pipe[2], int stderr_pipe[2],
                                  char **output, size_t *output_size,
                                  pid_t pid, int timeout_seconds, int *timed_out,
                                  volatile int *interrupt_requested, int *exit_status);

// Internal helper: Spawn a child process with stdout/stderr pipes
// Uses posix_spawn on macOS, fork/exec on Linux
// Returns 0 on success, -1 on error. Outputs the pid via out_pid.
static int spawn_child_with_pipes(const char *path, char **argv, const char *workdir,
                                  int stdout_pipe[2], int stderr_pipe[2],
                                  pid_t *out_pid)
{
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

    int rc = posix_spawn_file_actions_init(&file_actions);
    if (rc != 0) {
        LOG_ERROR("Failed to init file actions: %s", strerror(rc));
        return -1;
    }

    rc = posix_spawnattr_init(&spawnattr);
    if (rc != 0) {
        LOG_ERROR("Failed to init spawn attributes: %s", strerror(rc));
        posix_spawn_file_actions_destroy(&file_actions);
        return -1;
    }

    // Set up file actions: redirect stdout/stderr to pipes, stdin from /dev/null
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe[0]);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe[1]);
    posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    // Change working directory if specified
    // macOS 26.0+ has posix_spawn_file_actions_addchdir; older macOS only has _np
    if (workdir && workdir[0] != '\0') {
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && (__MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
        posix_spawn_file_actions_addchdir(&file_actions, workdir);
#else
        posix_spawn_file_actions_addchdir_np(&file_actions, workdir);
#endif
    }

    // Set spawn attributes: create new process group for kill(-pid, sig)
    short flags = POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&spawnattr, flags);
    posix_spawnattr_setpgroup(&spawnattr, 0);

    // Use posix_spawnp() to get PATH search behavior like execvp()
    rc = posix_spawnp(out_pid, path, &file_actions, &spawnattr, argv, environ);

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&spawnattr);

    if (rc != 0) {
        LOG_ERROR("Failed to spawn process '%s': %s", path, strerror(rc));
        return -1;
    }

    return 0;
#else
    /*
     * Linux: Use the traditional fork()/exec() approach.
     */
    pid_t pid = fork();
    if (pid == -1) {
        LOG_ERROR("Failed to fork: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        // Create a new process group
        setpgid(0, 0);

        // Change working directory if specified
        if (workdir && workdir[0] != '\0') {
            if (chdir(workdir) != 0) {
                fprintf(stderr, "Failed to change to working directory '%s': %s\n",
                        workdir, strerror(errno));
                _exit(127);
            }
        }

        execvp(path, argv);

        // If we get here, exec failed
        _exit(127);
    }

    *out_pid = pid;
    return 0;
#endif
}

// Execute a command with explicit argv array and optional workdir
int execute_command_with_timeout_argv(
    const char *path,
    char **argv,
    const char *workdir,
    int timeout_seconds,
    int *timed_out,
    char **output,
    size_t *output_size,
    volatile int *interrupt_requested
) {
    if (!path || !argv || !timed_out || !output || !output_size || !interrupt_requested) {
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

    pid_t pid;
    if (spawn_child_with_pipes(path, argv, workdir, stdout_pipe, stderr_pipe, &pid) != 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    // Parent process: close write ends
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    int exit_status = -1;
    int result = read_output_from_pipes(stdout_pipe, stderr_pipe, output, output_size,
                                        pid, timeout_seconds, timed_out,
                                        interrupt_requested, &exit_status);

    return result == 0 ? exit_status : result;
}

// Execute a command with timeout using sh -c (wrapper around execute_command_with_timeout_argv)
int execute_command_with_timeout(
    const char *command,
    const char *workdir,
    int timeout_seconds,
    int *timed_out,
    char **output,
    size_t *output_size,
    volatile int *interrupt_requested
) {
    if (!command) {
        return -1;
    }

    // Build argv for shell execution
    // Note: For macOS posix_spawn, argv needs to be mutable
    char shell_name[] = "sh";
    char dash_c[] = "-c";
    char *command_copy = strdup(command);
    if (!command_copy) {
        LOG_ERROR("Failed to allocate memory for command copy");
        return -1;
    }
    char *argv[] = {shell_name, dash_c, command_copy, NULL};

    int result = execute_command_with_timeout_argv(
        "/bin/sh", argv, workdir, timeout_seconds,
        timed_out, output, output_size, interrupt_requested
    );

    free(command_copy);
    return result;
}

// Internal helper: Read output from pipes with timeout and interrupt support
static int read_output_from_pipes(int stdout_pipe[2], int stderr_pipe[2],
                                  char **output, size_t *output_size,
                                  pid_t pid, int timeout_seconds, int *timed_out,
                                  volatile int *interrupt_requested, int *exit_status)
{
    // Set pipes to non-blocking
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    char buffer[4096];
    size_t total_size = 0;
    char *result = NULL;
    time_t start_time = time(NULL);
    int process_exited = 0;
    *exit_status = -1;

    int stdout_eof = 0;
    int stderr_eof = 0;

    while (!process_exited || !stdout_eof || !stderr_eof) {
        if (*interrupt_requested) {
            LOG_DEBUG("Command interrupted by user");
            kill(-pid, SIGTERM);
            usleep(100000);
            kill(-pid, SIGKILL);
            break;
        }

        time_t current_time = time(NULL);
        if (timeout_seconds > 0 && (current_time - start_time) >= timeout_seconds) {
            LOG_DEBUG("Command timed out after %d seconds", timeout_seconds);
            *timed_out = 1;
            kill(-pid, SIGTERM);
            usleep(100000);
            kill(-pid, SIGKILL);
            break;
        }

        if (!process_exited) {
            int status;
            pid_t wait_result = waitpid(pid, &status, WNOHANG);
            if (wait_result == pid) {
                process_exited = 1;
                if (WIFEXITED(status)) {
                    *exit_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    *exit_status = 128 + WTERMSIG(status);
                }
            } else if (wait_result == -1) {
                LOG_ERROR("waitpid failed: %s", strerror(errno));
                break;
            }
        }

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

        if (max_fd == -1) {
            stdout_eof = 1;
            stderr_eof = 1;
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int select_result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (select_result > 0) {
            if (stdout_pipe[0] != -1 && FD_ISSET(stdout_pipe[0], &readfds)) {
                ssize_t bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
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
                    close(stdout_pipe[0]);
                    stdout_pipe[0] = -1;
                    stdout_eof = 1;
                }
            }

            if (stderr_pipe[0] != -1 && FD_ISSET(stderr_pipe[0], &readfds)) {
                ssize_t bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
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
                    close(stderr_pipe[0]);
                    stderr_pipe[0] = -1;
                    stderr_eof = 1;
                }
            }
        } else if (select_result == -1 && errno != EINTR) {
            LOG_ERROR("select failed: %s", strerror(errno));
            break;
        }
    }

    if (stdout_pipe[0] != -1) close(stdout_pipe[0]);
    if (stderr_pipe[0] != -1) close(stderr_pipe[0]);

    if (!process_exited) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            *exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            *exit_status = 128 + WTERMSIG(status);
        }
    }

    *output = result;
    *output_size = total_size;

    return *timed_out ? -2 : 0;
}
