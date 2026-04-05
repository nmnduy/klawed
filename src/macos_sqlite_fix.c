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

    /*
     * Configure WAL auto-checkpoint to prevent WAL file growth on macOS.
     * Default is 1000 pages, but on macOS with mmap disabled, we need more
     * frequent checkpointing to prevent performance degradation.
     * 100 pages = ~400KB checkpoint threshold.
     */
    rc = sqlite3_exec(db, "PRAGMA wal_autocheckpoint = 100;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("[macOS] Failed to set WAL auto-checkpoint: %s", err_msg);
        sqlite3_free(err_msg);
        err_msg = NULL;
    } else {
        LOG_DEBUG("[macOS] Set WAL auto-checkpoint to 100 pages");
    }

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

int macos_sqlite_wal_checkpoint(sqlite3 *db, int mode) {
    if (!db) return -1;

#ifdef KLAWED_MACOS
    int log_size = 0;
    int checkpointed = 0;

    int rc = sqlite3_wal_checkpoint_v2(db, NULL, mode, &log_size, &checkpointed);
    if (rc != SQLITE_OK) {
        LOG_WARN("[macOS] WAL checkpoint failed: %s", sqlite3_errmsg(db));
        return -1;
    }

    if (log_size > 0) {
        LOG_DEBUG("[macOS] WAL checkpoint: %d frames in log, %d checkpointed", log_size, checkpointed);
    }
    return 0;
#else
    (void)mode;
    return 0;  /* No special handling needed on non-macOS */
#endif
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

int macos_sqlite_wal_checkpoint(sqlite3 *db, int mode) {
    (void)db;
    (void)mode;
    return 0;  /* No special handling needed on non-macOS */
}

#endif /* KLAWED_MACOS */
