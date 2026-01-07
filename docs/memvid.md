# Memvid Integration

Memvid provides persistent memory for klawed, enabling the agent to remember facts, preferences, and context across sessions.

## Overview

- Memory is stored in `.klawed/memory.mv2` (project-local)
- Memories are structured as entity:slot = value pairs
- Supports different memory types: facts, preferences, events, profiles, relationships, goals
- Memories persist across sessions and can be searched

## Building with Memvid Support

```bash
# Build the FFI library first (in memvid repo)
cd /path/to/memvid/memvid-ffi
cargo build --release

# Build klawed with memvid support
make MEMVID=1

# Or let it auto-detect if libmemvid_ffi is available
make
```

**Build options:**
- `make MEMVID=1` - Explicitly enable memvid (fails if library not found)
- `make MEMVID=0` - Explicitly disable memvid
- `make` - Auto-detect if libmemvid_ffi is available

## Memory Tools

### MemoryStore

Store a memory about the user or project. Memories persist across sessions.

**Parameters:**
- `entity` (required): Who/what this is about (e.g., "user", "project.klawed", "user.team")
- `slot` (required): The attribute (e.g., "employer", "preferred_language", "coding_style")
- `value` (required): The value to store
- `kind` (required): Type of memory (see Memory Types below)
- `relation` (optional): How this relates to existing values (default: "sets")
  - `sets` - Replace the current value
  - `updates` - Update with new information
  - `extends` - Add to existing value
  - `retracts` - Remove/negate the value

### MemoryRecall

Recall the current value for an entity's attribute from persistent memory.

**Parameters:**
- `entity` (required): Who/what to look up
- `slot` (required): The attribute to recall

### MemorySearch

Search all memories by text query.

**Parameters:**
- `query` (required): Search query string
- `top_k` (optional): Number of results (default: 10)

## Memory Types

| Kind | Description | Example |
|------|-------------|---------|
| `fact` | Factual information | "User works at Anthropic" |
| `preference` | User preferences | "Prefers explicit error handling" |
| `event` | Discrete events | "User moved to SF on 2024-03-15" |
| `profile` | Background information | "User is a software engineer" |
| `relationship` | Relationships between entities | "User's manager is Alice" |
| `goal` | Goals and intents | "User wants to learn Rust" |

## Example Usage

### Storing Memories

```
# Store a user preference
MemoryStore(
  entity="user",
  slot="coding_style",
  value="prefers explicit error handling over exceptions",
  kind="preference"
)

# Store a fact about the project
MemoryStore(
  entity="project.klawed",
  slot="language",
  value="C11",
  kind="fact"
)

# Store a user goal
MemoryStore(
  entity="user",
  slot="learning",
  value="wants to understand async patterns better",
  kind="goal"
)
```

### Recalling Memories

```
# Recall a specific memory
MemoryRecall(entity="user", slot="coding_style")
→ "prefers explicit error handling over exceptions"

# Check if a memory exists
MemoryRecall(entity="user", slot="employer")
→ null (if not stored)
```

### Searching Memories

```
# Search for related memories
MemorySearch(query="coding", top_k=5)
→ Returns array of matching memories

# Search with more results
MemorySearch(query="user preferences")
```

## Best Practices

### When to Store Memories

✅ **Good use cases:**
- User explicitly states a preference or fact about themselves
- Important project constraints or decisions
- Recurring patterns you notice (e.g., "user always uses tabs")
- User's goals for the current project

❌ **Avoid storing:**
- Temporary or session-specific information
- Sensitive data (API keys, passwords)
- Transient state that changes frequently

### Entity Naming Conventions

- `user` - Information about the user
- `user.team` - Information about the user's team
- `project.<name>` - Project-specific information
- `<domain>` - Domain-specific knowledge

### Slot Naming Conventions

Use descriptive, lowercase slot names with underscores:
- `coding_style`, `preferred_language`, `employer`
- `architecture_preference`, `testing_approach`

## Architecture

```
┌─────────────────────────────────────────────────┐
│                   klawed.c                       │
│    (Memory tools: MemoryStore/Recall/Search)    │
└──────────────────────┬──────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────┐
│               src/memvid.c                       │
│         (C wrapper, global instance)            │
└──────────────────────┬──────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────┐
│             memvid-ffi (Rust)                   │
│        (FFI bindings to memvid-core)            │
└──────────────────────┬──────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────┐
│              memvid-core (Rust)                 │
│      (Vector storage, search, persistence)      │
└─────────────────────────────────────────────────┘
```

**Source files:**
- `src/memvid.h` - C header with function declarations and constants
- `src/memvid.c` - C wrapper with global instance management
- Memory tools in `src/klawed.c` - `tool_memory_store()`, `tool_memory_recall()`, `tool_memory_search()`

## Configuration

**Environment variables:**
- `KLAWED_MEMORY_PATH` - Custom path for memory file (default: `.klawed/memory.mv2`)

## Limitations

1. **Build dependency** - Requires the memvid-ffi Rust library to be built
2. **No encryption** - Memory file is stored in plaintext
3. **Local only** - Memories are stored per-project directory
4. **No sync** - No built-in mechanism to sync across machines

## Troubleshooting

**Problem**: "Memvid: Not available (built without HAVE_MEMVID)"
- Cause: klawed was built without memvid support
- Solution: Rebuild with `make MEMVID=1` or ensure libmemvid_ffi.a exists

**Problem**: "Failed to open database"
- Cause: Cannot create or access `.klawed/memory.mv2`
- Solution: Check directory permissions and disk space

**Problem**: Memory tools not appearing
- Cause: klawed built without memvid support
- Solution: Verify with `memvid_is_available()` or rebuild with MEMVID=1
