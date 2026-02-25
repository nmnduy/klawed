# Memory System (SQLite + FTS5)

Klawed uses an SQLite-based memory system for persistent storage of user preferences, project context, and other long-term memory.

## Overview

The memory system provides:

- **Entity:Slot:Value storage** - Store facts about users, projects, and other entities
- **Full-text search (FTS5)** - Fast text-based memory retrieval
- **Vector search support** - Optional sqlite-vector extension for similarity-based search
- **Thread-safe access** - Global singleton pattern with mutex protection

## Architecture

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

## Database Schema

### Core Memories Table

```sql
CREATE TABLE memories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    entity TEXT NOT NULL,           -- e.g., "user", "project.klawed"
    slot TEXT NOT NULL,             -- e.g., "preferred_language", "coding_style"
    value TEXT NOT NULL,            -- The actual memory content
    kind INTEGER NOT NULL DEFAULT 0,     -- fact, preference, event, etc.
    relation INTEGER NOT NULL DEFAULT 0, -- sets, updates, extends, retracts
    timestamp TEXT NOT NULL,        -- ISO 8601 timestamp
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for efficient lookups
CREATE INDEX idx_memories_entity ON memories(entity);
CREATE INDEX idx_memories_entity_slot ON memories(entity, slot);
CREATE INDEX idx_memories_timestamp ON memories(timestamp);
```

### FTS5 Virtual Table

```sql
CREATE VIRTUAL TABLE memories_fts USING fts5(
    content,
    entity,
    slot,
    content_rowid=rowid
);

-- Triggers keep FTS index synchronized
CREATE TRIGGER memories_fts_insert AFTER INSERT ON memories ...
CREATE TRIGGER memories_fts_delete AFTER DELETE ON memories ...
```

## API Reference

### Initialization

```c
// Initialize global memory database
int memory_db_init_global(const char *path);

// Clean up global instance
void memory_db_cleanup_global(void);

// Get global handle
MemoryDB* memory_db_get_global(void);
```

### Storage Operations

```c
// Store a memory card
int64_t memory_db_store(MemoryDB *db, const char *entity,
                        const char *slot, const char *value,
                        MemoryKind kind, MemoryRelation relation);

// Get current value for entity:slot
MemoryCard* memory_db_get_current(MemoryDB *db, const char *entity, const char *slot);
```

### Search Operations

```c
// Full-text search using FTS5
MemorySearchResult* memory_db_search(MemoryDB *db, const char *query, uint32_t top_k);

// Get all memories for an entity
MemorySearchResult* memory_db_get_entity_memories(MemoryDB *db, const char *entity);
```

### Cleanup

```c
// Free a single card
void memory_db_free_card(MemoryCard *card);

// Free search results
void memory_db_free_result(MemorySearchResult *result);
```

## Memory Kinds

| Kind | Description |
|------|-------------|
| `MEMORY_KIND_FACT` | Objective facts |
| `MEMORY_KIND_PREFERENCE` | User preferences |
| `MEMORY_KIND_EVENT` | Events that occurred |
| `MEMORY_KIND_PROFILE` | Profile information |
| `MEMORY_KIND_RELATIONSHIP` | Relationship data |
| `MEMORY_KIND_GOAL` | Goals and objectives |

## Memory Relations

| Relation | Description |
|----------|-------------|
| `MEMORY_RELATION_SETS` | Sets a new value |
| `MEMORY_RELATION_UPDATES` | Updates existing value |
| `MEMORY_RELATION_EXTENDS` | Extends existing value |
| `MEMORY_RELATION_RETRACTS` | Retracts/clears value |

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KLAWED_MEMORY_PATH` | `.klawed/memory.db` | Path to memory database |

### Example Usage

```bash
# Use custom memory location
export KLAWED_MEMORY_PATH="/path/to/my/memory.db"
./build/klawed "remember that I prefer Python"
```

## Tools Integration

The memory system is exposed through three tools:

### MemoryStore

Store a new memory:
```json
{
  "entity": "user",
  "slot": "preferred_language",
  "value": "Python",
  "kind": "preference"
}
```

### MemoryRecall

Recall a specific memory:
```json
{
  "entity": "user",
  "slot": "preferred_language"
}
```

### MemorySearch

Search memories by text:
```json
{
  "query": "coding preferences",
  "top_k": 10
}
```

## Implementation Details

### Thread Safety

- Global instance uses `pthread_once` for one-time initialization
- Mutex protection for global handle access
- SQLite handles are not shared across threads

### FTS5 Fallback

If FTS5 is not available in the SQLite build, the system gracefully falls back to `LIKE` queries:

```sql
-- Fallback when FTS5 unavailable
SELECT * FROM memories
WHERE value LIKE '%query%' OR entity LIKE '%query%' OR slot LIKE '%query%'
ORDER BY id DESC
LIMIT ?;
```

### Vector Search (Future)

The schema includes placeholders for vector embeddings:

```sql
CREATE TABLE memory_embeddings (
    memory_id INTEGER PRIMARY KEY,
    embedding BLOB NOT NULL,
    FOREIGN KEY (memory_id) REFERENCES memories(id) ON DELETE CASCADE
);
```

Vector search requires the sqlite-vector extension.

## Debugging

### Inspect Memory Database

Use standard SQLite tools:

```bash
# List all memories
sqlite3 .klawed/memory.db "SELECT * FROM memories;"

# Search using FTS5
sqlite3 .klawed/memory.db "SELECT * FROM memories_fts WHERE memories_fts MATCH 'python';"

# Check database integrity
sqlite3 .klawed/memory.db "PRAGMA integrity_check;"
```

### Schema Version

The database tracks its schema version:

```sql
SELECT value FROM memory_metadata WHERE key = 'schema_version';
```

## Migration from Memvid

If you have existing `.mv2` files from the old memvid system:

1. The memory tools will create a new `.db` file automatically
2. Old `.mv2` files are not automatically converted (manual migration required)
3. The file extension has changed from `.mv2` to `.db`

## Dependencies

- **Required**: SQLite3 with FTS5 support
- **Optional**: sqlite-vector extension (for vector search)

### Check FTS5 Availability

```bash
sqlite3 :memory: "CREATE VIRTUAL TABLE test USING fts5(content);"
# If no error, FTS5 is available
```
