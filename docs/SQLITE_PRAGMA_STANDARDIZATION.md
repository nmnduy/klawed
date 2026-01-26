# SQLite PRAGMA Standardization - All Database Managers

**Date**: 2026-01-26  
**Objective**: Ensure all SQLite database managers use identical PRAGMA settings

---

## Summary of Changes

### ✅ Standardized PRAGMA Settings Across All Databases

All database managers now set **8 identical PRAGMAs** on every connection:

```java
stmt.execute("PRAGMA journal_mode = WAL");           // Write-Ahead Log
stmt.execute("PRAGMA synchronous = NORMAL");         // Balanced durability
stmt.execute("PRAGMA busy_timeout = 5000");          // 5s wait on locks
stmt.execute("PRAGMA foreign_keys = ON");            // Referential integrity
stmt.execute("PRAGMA cache_size = -2000");           // 2 MB page cache
stmt.execute("PRAGMA mmap_size = 268435456");        // 256 MB memory mapping
stmt.execute("PRAGMA temp_store = MEMORY");          // Temp tables in RAM
stmt.execute("PRAGMA encoding = 'UTF-8'");           // Character encoding
```

---

## Database Managers Updated

### ✅ BlogDatabaseManager
**Status**: Already had all 8 PRAGMAs  
**Location**: `src/main/java/com/filesurf/db/BlogDatabaseManager.java`  
**No changes needed**

### ✅ SQLiteManager (Main Database)
**Status**: Already had all 8 PRAGMAs  
**Location**: `src/main/java/com/filesurf/db/SQLiteManager.java`  
**No changes needed**

### ✅ SessionSQLiteManager
**Status**: Already had all 8 PRAGMAs  
**Location**: `src/main/java/com/filesurf/db/SessionSQLiteManager.java`  
**No changes needed**

### ✅ FeedbackDatabaseManager
**Status**: Missing 2 PRAGMAs, now added  
**Location**: `src/main/java/com/filesurf/db/FeedbackDatabaseManager.java`  
**Changes**:
- ✅ Added `PRAGMA mmap_size = 268435456`
- ✅ Added `PRAGMA encoding = 'UTF-8'`

---

## What Each PRAGMA Does

| PRAGMA | Value | Persistent? | Purpose |
|--------|-------|-------------|---------|
| `journal_mode` | `WAL` | ✅ Yes | Write-Ahead Logging (readers don't block writers) |
| `synchronous` | `NORMAL` | ✅ Yes | Balanced durability vs performance (2-3x faster than FULL) |
| `busy_timeout` | `5000` | ❌ No | Wait 5 seconds if database is locked (prevents errors) |
| `foreign_keys` | `ON` | ❌ No | Enforce referential integrity constraints |
| `cache_size` | `-2000` | ❌ No | Use 2 MB of RAM for page cache (faster queries) |
| `mmap_size` | `268435456` | ❌ No | Memory-map 256 MB of DB file (faster reads) |
| `temp_store` | `MEMORY` | ❌ No | Store temporary tables in RAM (faster sorts/joins) |
| `encoding` | `UTF-8` | ✅ Yes | Character encoding (required for i18n) |

**Persistent PRAGMAs**: Saved to database file, apply to all connections  
**Connection PRAGMAs**: Must be set on every new connection

---

## Benefits of Standardization

### 1. **Consistency**
All databases behave identically with respect to:
- Concurrency (WAL mode)
- Performance (cache, memory mapping)
- Data integrity (foreign keys)
- Character encoding (UTF-8)

### 2. **Performance**
- **WAL mode**: Readers and writers don't block each other
- **NORMAL sync**: 2-3x faster writes than FULL
- **mmap**: Faster reads by memory-mapping DB file
- **Cache**: Frequently accessed pages stay in RAM

### 3. **Reliability**
- **busy_timeout**: Automatic retry prevents "database is locked" errors
- **foreign_keys**: Prevents orphaned records (blog posts without authors)
- **WAL checkpoint**: Gradual flushing reduces write spikes

### 4. **Maintainability**
- Same PRAGMA block in all managers
- Easy to update/modify across project
- Documented in `docs/BLOG_SQLITE_PRAGMAS.md`

---

## Database Sizes and Impact

| Database | File Size | Purpose | PRAGMAs Applied |
|----------|-----------|---------|-----------------|
| `data/filesurf.db` | ~5 MB | Main user/session data | ✅ All 8 |
| `data/blog.db` | 188 KB | Blog posts/categories/tags | ✅ All 8 |
| `data/feedback.db` | ~50 KB | User feedback | ✅ All 8 (2 added) |
| `data/sessions.db` | Variable | Session tracking | ✅ All 8 |
| Session-specific DBs | Variable | Per-session data | ✅ All 8 |

**Total impact**: Minimal memory overhead (~4-8 MB per database), significant performance gain.

---

## Testing Recommendations

### Before Restarting Quarkus

1. **Verify all managers are updated**:
```bash
grep -A10 "Set PRAGMAs" src/main/java/com/filesurf/db/*.java
```

2. **Check for compilation errors**:
```bash
mvn compile
```

### After Restarting Quarkus

1. **Check logs for initialization messages**:
```bash
grep "initialized with WAL" logs/application.log
```

2. **Test each database**:
```bash
# Blog
curl http://localhost:9090/blog

# Feedback
curl -X POST http://localhost:9090/feedback/submit \
  -H "Content-Type: application/json" \
  -d '{"type":"bug","description":"test"}'

# Sessions (automatic)
# Visit any page - sessions are created automatically
```

3. **Monitor for database lock errors**:
```bash
grep "database is locked" logs/application.log
# Should be: no results (busy_timeout prevents this)
```

---

## Rollback Plan (If Issues Occur)

If any database has issues after these changes:

### Quick Fix: Revert FeedbackDatabaseManager
```bash
git checkout src/main/java/com/filesurf/db/FeedbackDatabaseManager.java
mvn quarkus:dev
```

### Why This Should Be Safe
- We only **added** PRAGMAs, didn't remove or change existing ones
- mmap_size and encoding are optional optimizations
- All other databases already use these settings successfully

---

## Files Modified

1. **src/main/java/com/filesurf/db/FeedbackDatabaseManager.java**
   - Added `PRAGMA mmap_size = 268435456`
   - Added `PRAGMA encoding = 'UTF-8'`

2. **docs/BLOG_SQLITE_PRAGMAS.md** (created)
   - Comprehensive PRAGMA documentation
   - Explains each setting and why we use it

3. **docs/SQLITE_PRAGMA_STANDARDIZATION.md** (this file)
   - Summary of standardization effort
   - Testing and verification instructions

---

## Performance Expectations

### Before (FeedbackDatabaseManager missing 2 PRAGMAs)
- ❌ No memory mapping (slower reads on large DBs)
- ⚠️ Encoding not explicitly set (could default to something else)

### After (All PRAGMAs standardized)
- ✅ Memory-mapped reads (up to 256 MB of DB file in RAM)
- ✅ UTF-8 encoding guaranteed (consistent i18n support)
- ✅ All databases behave identically

**Expected impact on feedback.db**: Negligible (it's only ~50 KB), but ensures consistency with other databases.

---

## Long-Term Maintenance

### When Adding New Database Managers

Always include this PRAGMA block in the initialization method:

```java
// Set PRAGMAs for optimal SQLite performance
try (Statement stmt = connection.createStatement()) {
    stmt.execute("PRAGMA journal_mode = WAL");
    stmt.execute("PRAGMA synchronous = NORMAL");
    stmt.execute("PRAGMA busy_timeout = 5000");
    stmt.execute("PRAGMA foreign_keys = ON");
    stmt.execute("PRAGMA cache_size = -2000");
    stmt.execute("PRAGMA mmap_size = 268435456");
    stmt.execute("PRAGMA temp_store = MEMORY");
    stmt.execute("PRAGMA encoding = 'UTF-8'");
}
```

### Updating PRAGMA Values

If we need to change a PRAGMA value (e.g., increase cache_size):
1. Update **all** database managers in one commit
2. Update documentation (`docs/BLOG_SQLITE_PRAGMAS.md`)
3. Test with each database type

---

## References

- SQLite PRAGMA documentation: https://www.sqlite.org/pragma.html
- WAL mode: https://www.sqlite.org/wal.html
- Memory-mapped I/O: https://www.sqlite.org/mmap.html
- Busy timeout: https://www.sqlite.org/c3ref/busy_timeout.html

---

**Status**: ✅ Complete - All SQLite databases use identical PRAGMA settings  
**Date**: 2026-01-26  
**Next Step**: Restart Quarkus to apply changes
