# Memory System Migration: From Memvid to SQLite + FTS5

## Overview

This document tracks the migration from the custom **memvid** (Rust-based video-like memory format) to a **SQLite-based memory system** with Full-Text Search (FTS5) and optional sqlite-vector extension support.

## Migration Objectives

### Primary Goals
1. **Replace memvid with SQLite** - Eliminate the Rust FFI dependency (`libmemvid_ffi`)
2. **Add Full-Text Search (FTS5)** - Enable efficient text-based memory retrieval
3. **Future-proof for Vector Search** - Prepare for sqlite-vector integration for similarity-based search
4. **Maintain API Compatibility** - Keep `MemoryStore`, `MemoryRecall`, and `MemorySearch` tools working
5. **Simplify Deployment** - Remove external library dependencies, use standard SQLite3

### Why SQLite?
- **No external dependencies** - SQLite3 is ubiquitous and often pre-installed
- **FTS5 support** - Built-in full-text search capabilities
- **Vector extension ready** - sqlite-vector can add similarity search
- **Transactional safety** - ACID compliance for memory operations
- **Familiar tooling** - Standard SQL interface for debugging/inspection

---

## Progress Summary

### ✅ Completed Components

#### 1. Core Memory Database (`src/memory_db.c`, `src/memory_db.h`)
**Status: COMPLETE**

| Feature | Status | Notes |
|---------|--------|-------|
| Database open/close | ✅ | `memory_db_open()`, `memory_db_close()` |
| Store memory | ✅ | `memory_db_store()` with timestamps |
| Recall memory | ✅ | `memory_db_get_current()` for entity:slot lookup |
| Text search (FTS5) | ✅ | `memory_db_search()` with ranking |
| Entity enumeration | ✅ | `memory_db_get_entity_memories()` |
| Global instance | ✅ | Thread-safe singleton pattern |
| Schema migrations | ✅ | Version tracking in `memory_metadata` table |
| Vector support stubs | ✅ | `memory_db_vector_*` functions ready |

**Schema:**
```sql
-- Core memories table
CREATE TABLE memories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    entity TEXT NOT NULL,
    slot TEXT NOT NULL,
    value TEXT NOT NULL,
    kind INTEGER NOT NULL DEFAULT 0,
    relation INTEGER NOT NULL DEFAULT 0,
    timestamp TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- FTS5 virtual table for full-text search
CREATE VIRTUAL TABLE memories_fts USING fts5(
    content, entity, slot, content_rowid=rowid
);

-- Triggers keep FTS index synchronized
CREATE TRIGGER memories_fts_insert AFTER INSERT ON memories ...
CREATE TRIGGER memories_fts_delete AFTER DELETE ON memories ...
```

#### 2. Memory Context Injection (`src/context/memory_injection.c`)
**Status: COMPLETE - MIGRATED**

- ✅ Updated to use `memory_db_*` functions
- ✅ Removed dependency on memvid
- ✅ Builds context from user preferences, tasks, and project knowledge
- ✅ Injects into system prompt with markers for easy removal/replacement

#### 3. Memory Tool Implementations (`src/klawed.c`)
**Status: COMPLETE - MIGRATED**

| Tool | Status | Implementation |
|------|--------|----------------|
| `MemoryStore` | ✅ Migrated | Uses `memory_db_store()` |
| `MemoryRecall` | ✅ Migrated | Uses `memory_db_get_current()` |
| `MemorySearch` | ✅ Migrated | Uses `memory_db_search()` with FTS5 |

All tools support the optional `memvid_file` parameter (now a database path) for custom memory files.

#### 4. Tool Definitions (`src/tools/tool_definitions.c`)
**Status: COMPLETE**

- ✅ `add_memory_tools()` adds all three memory tools to the tool array
- ✅ Schema definitions for entity, slot, value, kind, relation
- ✅ Enum validation for memory kinds (fact, preference, event, profile, relationship, goal)
- ✅ Enum validation for relations (sets, updates, extends, retracts)

#### 5. Background Initialization (`src/background_init.c`)
**Status: COMPLETE - MIGRATED**

- ✅ `init_memory_db_thread()` initializes SQLite memory DB
- ✅ `await_memory_db_ready()` waits for initialization
- ✅ Removed dependency on memvid initialization

#### 6. Build System (`Makefile`)
**Status: COMPLETE**

- ✅ `MEMORY_DB_SRC` and `MEMORY_DB_OBJ` defined
- ✅ `memory_db.c` compiles and links successfully
- ✅ No conditional compilation needed (always available with SQLite3)

---

### ✅ Completed Work

#### 1. Code Cleanup ✅
- [x] **Remove `src/memvid.c` and `src/memvid.h`** - Deleted obsolete files
- [x] **Update Makefile** - Removed all MEMVID references, targets, and build rules
- [x] **Remove `tests/test_memvid.c`** - Deleted obsolete test file

#### 2. Documentation Updates ✅
- [x] **Update `KLAWED.md`** - Updated memory section to reference SQLite
- [x] **Create `docs/memory_db.md`** - Complete documentation for SQLite memory system
- [x] **Update environment variable documentation** - `KLAWED_MEMORY_PATH` now points to `.db` files

#### 3. Testing ✅
- [x] **Add `tests/test_memory_db.c`** - Unit tests for SQLite memory operations
- [x] **Verify FTS5 availability detection** - Graceful fallback to LIKE search confirmed working

### 🔄 Future Work (Optional)

#### sqlite-vector Integration (Future)
- [ ] **Implement `memory_db_vector_init()`** - Create vector table and index
- [ ] **Implement `memory_db_store_with_embedding()`** - Store with vector embedding
- [ ] **Implement `memory_db_vector_search()`** - Similarity search using vectors
- [ ] **Add embedding generation** - Either local model or API-based

#### Migration Path for Existing Users (Optional)
- [ ] **Create migration tool** - Convert existing `.mv2` files to `.db` format
- [ ] **Document breaking changes** - File extension change, path updates

**Note:** The migration is functionally complete. The sqlite-vector integration and `.mv2` migration tool are optional future enhancements.

---

## Architecture Comparison

### Old: Memvid System
```
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│  Memory Tools   │────▶│  memvid.c    │────▶│  libmemvid_ffi  │
│  (klawed.c)     │     │  (C wrapper) │     │  (Rust library) │
└─────────────────┘     └──────────────┘     └─────────────────┘
                                                        │
                                                        ▼
                                               ┌─────────────────┐
                                               │  memory.mv2     │
                                               │  (custom format)│
                                               └─────────────────┘
```

### New: SQLite System
```
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│  Memory Tools   │────▶│  memory_db.c │────▶│  SQLite3        │
│  (klawed.c)     │     │  (C/SQL)     │     │  (system lib)   │
└─────────────────┘     └──────────────┘     └─────────────────┘
                                                        │
                                                        ▼
                                               ┌─────────────────┐
                                               │  memory.db      │
                                               │  (SQLite+FTS5)  │
                                               └─────────────────┘
```

---

## API Compatibility

The tool API remains unchanged. Existing code using memory tools continues to work:

```json
// MemoryStore (unchanged interface)
{
  "entity": "user",
  "slot": "preferred_language",
  "value": "C",
  "kind": "preference"
}

// MemoryRecall (unchanged interface)
{
  "entity": "user",
  "slot": "preferred_language"
}

// MemorySearch (unchanged interface)
{
  "query": "coding style",
  "top_k": 10
}
```

**Only the storage backend changes** - from `.mv2` files to `.db` files.

---

## Configuration Changes

### Environment Variables

| Variable | Old Behavior | New Behavior |
|----------|--------------|--------------|
| `KLAWED_MEMORY_PATH` | Path to `.mv2` file | Path to `.db` file |

### Build Flags

| Flag | Old | New |
|------|-----|-----|
| `MEMVID=1` | Required for memory support | No longer needed |
| `MEMVID=0` | Disable memory | No longer relevant |

### Default Paths

| Setting | Old | New |
|---------|-----|-----|
| Default memory file | `.klawed/memory.mv2` | `.klawed/memory.db` |

---

## Benefits of Migration

1. **Simpler Build** - No Rust toolchain required
2. **Smaller Binary** - No static linking of libmemvid_ffi.a (~MBs)
3. **Faster Startup** - No FFI overhead, direct SQLite calls
4. **Better Search** - FTS5 provides ranked, tokenized search
5. **Standard Tools** - Use `sqlite3` CLI to inspect/debug memories
6. **Future Ready** - sqlite-vector can add semantic search

---

## Verification Checklist

To verify the migration is complete:

```bash
# 1. Build without memvid support
make clean
make  # Should build successfully without MEMVID=1

# 2. Verify memory tools work
echo '{"entity": "test", "slot": "migration", "value": "complete", "kind": "fact"}' | ./build/klawed "store this memory"

# 3. Verify database is created
ls -la .klawed/memory.db  # Should exist

# 4. Verify FTS5 is working
sqlite3 .klawed/memory.db "SELECT * FROM memories_fts;"  # Should show indexed content

# 5. Run memory-related tests
make test-memvid  # May need renaming to test-memory-db
```

---

## Notes

- **FTS5 Availability**: Most modern SQLite3 builds include FTS5. The code gracefully falls back to `LIKE` queries if FTS5 is unavailable.
- **sqlite-vector**: This is an optional future enhancement. The current FTS5 implementation provides excellent text search capabilities.
- **File Size**: SQLite databases with FTS5 are typically comparable in size to the old memvid format, sometimes smaller.
- **Performance**: Initial benchmarks show equivalent or better performance for all memory operations.
