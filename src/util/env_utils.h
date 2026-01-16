#ifndef ENV_UTILS_H
#define ENV_UTILS_H

/**
 * Environment Utilities
 * 
 * Helper functions for environment variables and system information.
 */

/**
 * Get integer value from environment variable with retry and validation
 * @param name Environment variable name
 * @param default_value Default value if not set or invalid
 * @return Parsed integer value, or default_value if invalid
 */
int get_env_int_retry(const char *name, int default_value);

/**
 * Get platform identifier
 * @return Static string with platform name (darwin, linux, win32, freebsd, openbsd, unknown)
 *         Do not free() the returned string
 */
const char* get_platform(void);

/**
 * Get OS version string
 * @return Dynamically allocated OS version string (from uname -sr), or NULL on error
 *         Caller must free() the returned string
 */
char* get_os_version(void);

/**
 * Execute shell command and return output
 * @param command Shell command to execute
 * @return Dynamically allocated output string, or NULL on error
 *         Caller must free() the returned string
 */
char* exec_shell_command(const char *command);

#endif // ENV_UTILS_H
