# macOS TUI Freeze Fix

## Problem
The application would freeze on macOS (especially ARM64/Apple Silicon) during startup. This was a known issue documented in KLAWED.md with the workaround of setting `KLAWED_NO_STORAGE=1` to disable SQLite database operations.

## Root Cause
The freeze was caused by SQLite database operations that could hang indefinitely on macOS due to:

1. **Memory-mapped I/O issues**: SQLite's mmap feature can cause hangs on APFS (Apple's file system)
2. **File locking differences**: macOS uses different file locking mechanisms than Linux
3. **Threading mode**: SQLite's default threading mode on macOS may not be optimal
4. **No timeout**: The database initialization thread could block indefinitely with no way to recover

## Solution
The fix involves several changes:

### 1. macOS-Specific SQLite Configuration (`src/macos_sqlite_fix.c`)
- Disables memory-mapped I/O (`PRAGMA mmap_size = 0`) on macOS
- Uses `SQLITE_OPEN_FULLMUTEX` flag for thread-safe operation
- Reduces busy timeout from 5000ms to 1000ms on macOS for faster failure recovery

### 2. Database Initialization Timeout (`src/background_init.c`)
- Added a 5-second timeout for database initialization on macOS
- Falls back to running without database if initialization times out
- Uses polling with 100ms intervals to avoid indefinite blocking

### 3. Consistent Database Configuration
All three database files (`persistence.c`, `token_usage_db.c`, `memory_db.c`) now:
- Use the macOS-specific fixes
- Use `sqlite3_open_v2()` instead of `sqlite3_open()` for better control
- Have consistent WAL mode and busy timeout settings

## Files Changed
- `src/macos_sqlite_fix.h` - New header with macOS SQLite fix declarations
- `src/macos_sqlite_fix.c` - New implementation with macOS-specific SQLite configuration
- `src/persistence.c` - Updated to use macOS fixes
- `src/token_usage_db.c` - Updated to use macOS fixes
- `src/memory_db.c` - Updated to use macOS fixes and added missing WAL mode/busy timeout
- `src/background_init.c` - Added timeout mechanism for database initialization
- `Makefile` - Added new source file to build

## Testing
After these changes:
1. The application should start without freezing on macOS
2. Database functionality works normally when initialization succeeds
3. If database initialization fails or times out, the app continues without persistence (graceful degradation)
4. The `KLAWED_NO_STORAGE=1` workaround is still available as a fallback

## Environment Variables
- `KLAWED_NO_STORAGE=1` - Still works to disable all storage operations (diagnostic mode)
- No new environment variables are required
