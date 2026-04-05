/*
 * crash_handler.h - Enhanced crash diagnostics for klawed
 *
 * Provides detailed crash reporting including:
 * - Stack backtraces
 * - Signal information (address, code)
 * - Thread information
 * - Register dump (platform-specific)
 */

#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include <signal.h>

/*
 * Install enhanced crash handlers for signals:
 * - SIGSEGV (segmentation fault)
 * - SIGBUS (bus error)
 * - SIGILL (illegal instruction)
 * - SIGABRT (abort)
 * - SIGFPE (floating point exception)
 *
 * These handlers will log detailed diagnostics before calling the cleanup handler.
 */
void crash_handler_install(void);

/*
 * Get a string description of the crash for logging.
 * Returns a static buffer - do not free.
 */
const char *crash_handler_get_last_info(void);

#endif /* CRASH_HANDLER_H */
