/*
 * macos_sqlite_fix.h - macOS-specific SQLite fixes for TUI hangs
 *
 * On macOS (especially ARM64/Apple Silicon), SQLite operations can hang due to:
 * 1. File locking differences on APFS
 * 2. Threading mode incompatibilities
 * 3. fcntl-based locking issues
 *
 * This header provides workarounds and fixes.
 */

#ifndef MACOS_SQLITE_FIX_H
#define MACOS_SQLITE_FIX_H

#include <sqlite3.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_MAC
#define KLAWED_MACOS 1
#endif
#endif

/*
 * Apply macOS-specific SQLite pragmas to avoid hangs.
 * Call this immediately after sqlite3_open() on macOS.
 *
 * Returns 0 on success, -1 on error.
 */
int macos_sqlite_apply_fixes(sqlite3 *db);

/*
 * Check if we're running on macOS and might need special handling.
 * Returns 1 if macOS fixes should be applied, 0 otherwise.
 */
int macos_sqlite_needs_fixes(void);

/*
 * Get recommended busy timeout for macOS (in milliseconds).
 * Returns a shorter timeout on macOS to prevent indefinite hangs.
 */
int macos_sqlite_busy_timeout_ms(void);

/*
 * Get recommended SQLite open flags for macOS.
 * These flags ensure proper threading mode on macOS.
 */
int macos_sqlite_open_flags(void);

#endif /* MACOS_SQLITE_FIX_H */
