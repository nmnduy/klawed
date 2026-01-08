# Memvid Integration

Memvid provides persistent memory for klawed, enabling the agent to remember facts, preferences, and context across sessions.

## Overview

- Memory is stored in `.klawed/memory.mv2` (project-local)
- Memories are structured as entity:slot = value pairs
- Supports different memory types: facts, preferences, events, profiles, relationships, goals
- Memories persist across sessions and can be searched

## Building with Memvid Support

```bash
# Initialize the memvid submodule (first time only)
git submodule update --init vendor/memvid

# Build the FFI library
make memvid-ffi

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

**When to use - BE PROACTIVE:**

Klawed should actively identify and store important information during conversations:

1. **User Preferences** - When user says "I prefer...", "I don't like...", "always use...", "never use..."
2. **User Facts** - When user shares "I work at...", "I'm learning...", "my team uses..."
3. **Project Constraints** - When you learn "we use tabs", "we follow style X", "this project requires Y"
4. **Recurring Patterns** - When you notice consistent patterns in their coding style or requests
5. **Explicit Requests** - When user says "remember that...", "keep in mind..."

**Do NOT store:**
- Temporary context, sensitive data (API keys/passwords), transient state
- Information already in KLAWED.md or project documentation (avoid duplication)

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

**When to use:**
- Need to check what you already know about user preferences or project details
- Starting a new conversation and want to recall context from previous sessions
- User mentions something you may have stored before (check if you have it)
- Verifying if a preference/fact exists before storing a new one

**Parameters:**
- `entity` (required): Who/what to look up
- `slot` (required): The attribute to recall

### MemorySearch

Search all memories by text query.

**When to use:**
- Need to find related past context but don't know the specific entity/slot
- User asks about something that may have been discussed before
- After seeing an auto-compaction notice - search for relevant past conversation context
- Starting a complex task and want to check for relevant project knowledge
- Exploring what you already know about a topic

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

**BE PROACTIVE**: Klawed should actively identify and store important information during conversations, not just when explicitly asked.

✅ **Always store when you notice:**

1. **User Preferences** (kind: "preference")
   - "I prefer explicit error handling"
   - "I don't like using exceptions"
   - "Always use tabs, not spaces"
   - "Never use abbreviations in variable names"
   - Preferred languages, frameworks, or tools

2. **User Facts** (kind: "fact" or "profile")
   - "I work at Anthropic"
   - "I'm learning Rust"
   - "My team uses Python 3.11"
   - "We deploy to AWS"
   - Background, expertise level, team context

3. **Project Constraints** (kind: "fact")
   - "This project uses C11 standard"
   - "We follow NASA C coding standards"
   - "Maximum line length is 80 characters"
   - "All functions must have tests"
   - Architecture decisions, style guides, requirements

4. **Recurring Patterns** (kind: "preference")
   - User consistently requests a certain code style
   - User always asks for tests with new functions
   - User prefers detailed explanations vs brief answers
   - User likes/dislikes certain approaches

5. **Goals and Intent** (kind: "goal")
   - "I want to learn async patterns"
   - "We're refactoring to use arena allocators"
   - "Goal: reduce memory allocations"
   - Long-term objectives or learning goals

6. **Explicit Requests** (any kind)
   - "Remember that I prefer..."
   - "Keep in mind that..."
   - "For future reference..."

❌ **NEVER store:**
- Temporary or session-specific information (current file being edited, temporary variables)
- Sensitive data (API keys, passwords, tokens, credentials)
- Transient state that changes frequently (current line number, loop counter)
- Information already documented in KLAWED.md or project files (avoid duplication)
- Conversation-specific context (use normal conversation flow for this)

### Entity Naming Conventions

- `user` - Information about the user
- `user.team` - Information about the user's team
- `project.<name>` - Project-specific information
- `<domain>` - Domain-specific knowledge

### Slot Naming Conventions

Use descriptive, lowercase slot names with underscores:
- `coding_style`, `preferred_language`, `employer`
- `architecture_preference`, `testing_approach`

## Workflow Guide for Klawed

### Starting a New Conversation

When beginning a conversation (especially in a new session), Klawed should:

1. **Check for existing context** using `MemorySearch` or `MemoryRecall`:
   ```
   MemorySearch(query="user preferences", top_k=5)
   MemoryRecall(entity="user", slot="coding_style")
   MemoryRecall(entity="project.{current_project}", slot="architecture")
   ```

2. **Use retrieved memories** to inform your approach and responses

### During a Conversation

As the conversation progresses, Klawed should:

1. **Listen for signals** indicating something should be stored:
   - User expressing preferences ("I prefer...", "I like...", "I don't like...")
   - User sharing facts ("I work at...", "We use...", "My team...")
   - User setting constraints ("Always...", "Never...", "Must...")
   - Recurring patterns in requests or code style

2. **Store proactively** without waiting to be asked:
   ```
   MemoryStore(
     entity="user",
     slot="error_handling_preference",
     value="prefers explicit error handling over exceptions",
     kind="preference"
   )
   ```

3. **Avoid duplicates** - Check if something similar is already stored:
   ```
   MemoryRecall(entity="user", slot="error_handling_preference")
   # If returns null or outdated value, then store/update
   ```

### After Auto-Compaction

When you see a compaction notice in the conversation:

1. **Use MemorySearch** to retrieve relevant past context:
   ```
   MemorySearch(query="file structure refactoring", top_k=5)
   ```

2. **Check for session-specific memories**:
   ```
   MemorySearch(query="session.{session_id}", top_k=10)
   ```

3. **Continue the conversation** naturally with recovered context

### Example Workflow

```
User: "I prefer using tabs over spaces, and I always want explicit error handling."

Klawed: [Identifies two preferences and stores them]
  MemoryStore(entity="user", slot="indentation", value="tabs", kind="preference")
  MemoryStore(entity="user", slot="error_handling", 
              value="explicit error handling", kind="preference")

User: "Let's write a new function for parsing JSON."

Klawed: [Recalls preferences before generating code]
  MemoryRecall(entity="user", slot="indentation")
  MemoryRecall(entity="user", slot="error_handling")
  
  [Generates function using tabs and explicit error handling]
```

## Slot Naming Conventions

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

1. **Build dependency** - Requires Rust toolchain to build memvid-ffi (fetches memvid-core from crates.io)
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
