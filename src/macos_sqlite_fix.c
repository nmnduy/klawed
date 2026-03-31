/*
 * macos_sqlite_fix.c - macOS-specific SQLite fixes for TUI hangs
 */

#include "macos_sqlite_fix.h"
#include "logger.h"
#include <string.h>

#ifdef KLAWED_MACOS

/*
 * On macOS, SQLite can hang due to fcntl file locking issues with WAL mode.
 * The workaround is to:
 * 1. Use a shorter busy timeout
 * 2. Disable memory-mapped I/O which can cause issues on APFS
 * 3. Use DELETE journal mode instead of WAL if WAL causes issues
 */
int macos_sqlite_apply_fixes(sqlite3 *db) {
    if (!db) return -1;

    char *err_msg = NULL;
    int rc;

    /*
     * Disable memory-mapped I/O on macOS.
     * This can cause hangs on APFS with certain SQLite versions.
     */
    rc = sqlite3_exec(db, "PRAGMA mmap_size = 0;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("[macOS] Failed to disable mmap: %s", err_msg);
        sqlite3_free(err_msg);
        err_msg = NULL;
        /* Non-fatal, continue */
    } else {
        LOG_DEBUG("[macOS] Disabled memory-mapped I/O");
    }

    /*
     * Set a shorter busy timeout on macOS to prevent indefinite hangs.
     * The default 5000ms can feel like a freeze; 1000ms is more responsive.
     */
    sqlite3_busy_timeout(db, 1000);
    LOG_DEBUG("[macOS] Set busy timeout to 1000ms");

    return 0;
}

int macos_sqlite_needs_fixes(void) {
    return 1;  /* Always apply fixes on macOS */
}

int macos_sqlite_busy_timeout_ms(void) {
    return 1000;  /* Shorter timeout on macOS */
}

int macos_sqlite_open_flags(void) {
    /*
     * On macOS, use FULLMUTEX to ensure thread safety.
     * This is more reliable than the default threading mode.
     */
    return SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
}

#else  /* Not macOS */

int macos_sqlite_apply_fixes(sqlite3 *db) {
    (void)db;
    return 0;  /* No fixes needed on non-macOS platforms */
}

int macos_sqlite_needs_fixes(void) {
    return 0;
}

int macos_sqlite_busy_timeout_ms(void) {
    return 5000;  /* Default timeout on other platforms */
}

int macos_sqlite_open_flags(void) {
    return SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
}

#endif /* KLAWED_MACOS */
