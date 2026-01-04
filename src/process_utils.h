/*
 * process_utils.h - Process management utilities for safe command execution
 */

#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <stddef.h>

/**
 * Execute a command with timeout and interrupt support
 * 
 * @param command The command to execute
 * @param timeout_seconds Timeout in seconds (0 for no timeout)
 * @param timed_out Output: set to 1 if command timed out
 * @param output Output: command output (caller must free)
 * @param output_size Output: size of output
 * @param interrupt_requested Pointer to interrupt flag (volatile int*)
 * @return Exit code of command, -1 on error, -2 on timeout
 */
int execute_command_with_timeout(
    const char *command,
    int timeout_seconds,
    int *timed_out,
    char **output,
    size_t *output_size,
    volatile int *interrupt_requested
);

#endif /* PROCESS_UTILS_H */
