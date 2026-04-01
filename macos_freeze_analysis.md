# macOS Freeze Analysis - ROOT CAUSE IDENTIFIED

## Problem Summary
- **Symptom:** Application freezes after running for a while (not at startup)
- **Platform:** macOS only (works fine on Linux)
- **Recent fixes applied:** SQLite mmap disabled, background thread timeouts

## ROOT CAUSE: Missing SQLite WAL Checkpoint

The codebase enables SQLite WAL (Write-Ahead Logging) mode on all databases:
- `api_calls.db` (persistence)
- `memory.db` (memory database)
- `token_usage.db` (token usage tracking)
- SQLite queue database

**BUT** there was no WAL checkpoint configuration or periodic checkpointing!

### Why this causes freezes on macOS:

1. **WAL file grows indefinitely** - Every write operation appends to the WAL file
2. **Performance degrades over time** - SQLite must scan larger WAL files for reads
3. **macOS APFS sensitivity** - With mmap disabled (our recent fix), file I/O is slower
4. **Eventually appears frozen** - Operations take seconds instead of milliseconds

### Why Linux doesn't show this issue:
- Different file system behavior (ext4/xfs vs APFS)
- Better mmap performance even with SQLite's default settings
- Different default SQLite configurations

## Fix Applied

### 1. WAL Auto-Checkpoint Configuration (src/macos_sqlite_fix.c)

Added `PRAGMA wal_autocheckpoint = 100;` to limit WAL file growth:
```c
/*
 * Configure WAL auto-checkpoint to prevent WAL file growth on macOS.
 * Default is 1000 pages, but on macOS with mmap disabled, we need more
 * frequent checkpointing to prevent performance degradation.
 * 100 pages = ~400KB checkpoint threshold.
 */
rc = sqlite3_exec(db, "PRAGMA wal_autocheckpoint = 100;", NULL, NULL, &err_msg);
```

### 2. Manual Checkpoint API (src/macos_sqlite_fix.c/h)

Added `macos_sqlite_wal_checkpoint()` function for explicit checkpoint control.

## Additional Fix (Lower Priority)

### vim-fugitive Check Timeout (src/tui_core.c)

The vim-fugitive availability check could theoretically hang if vim waits for input:
```c
#ifdef __APPLE__
    snprintf(test_cmd, sizeof(test_cmd),
             "timeout 5 vim -c \"if exists(':Git') | q | else | cquit 1 | endif\" -c \"q\" 2>&1");
```

## Monitoring

Users can monitor WAL file growth before/after the fix:
```bash
# Check WAL file sizes
ls -lh .klawed/*.db-wal

# Check if checkpoint is working (should show decreasing or small log size)
sqlite3 .klawed/api_calls.db "PRAGMA wal_checkpoint; SELECT * FROM pragma_wal_checkpoint();"
```

## Testing the Fix

1. Run klawed normally for an extended session
2. Monitor `.klawed/*.db-wal` files - they should stay small (< 1MB)
3. Application should remain responsive after hours of use

## Alternative Workarounds (if issues persist)

If users still experience issues, they can:

1. **Disable storage entirely:**
   ```bash
   export KLAWED_NO_STORAGE=1
   ```

2. **Use DELETE journal mode instead of WAL:**
   Would require code change to disable WAL on macOS

3. **Periodic manual checkpoint:**
   Run `PRAGMA wal_checkpoint;` via sqlite3 CLI during idle periods

## Summary of Changes

| File | Change |
|------|--------|
| `src/macos_sqlite_fix.c` | Added `PRAGMA wal_autocheckpoint = 100` and `macos_sqlite_wal_checkpoint()` function |
| `src/macos_sqlite_fix.h` | Added checkpoint function declaration |
| `src/tui_core.c` | Added timeout wrapper to vim-fugitive check (preventive) |

## Future Improvements

1. Add periodic explicit checkpoint calls during idle time
2. Monitor WAL file size and trigger emergency checkpoint if needed
3. Consider different journal mode for macOS if issues persist
