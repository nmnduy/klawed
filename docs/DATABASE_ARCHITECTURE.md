# FileSurf v2 - Database Architecture

## Overview
FileSurf v2 uses multiple SQLite databases for separation of concerns and independent lifecycle management.

## Database Files

### 1. Main Application Database (`filesurf.db`)
**Location:**
- Dev: `data/filesurf.db`
- Prod: `/var/lib/filesurf/data/filesurf.db`

**Purpose:** Core application data
- Users table (authentication, user profiles)
- Main application state

**Managed by:** `SQLiteManager` (singleton)

---

### 2. Sessions Database (`sessions.db`)
**Location:**
- Dev: `data/sessions.db`
- Prod: `/var/lib/filesurf/data/sessions.db`

**Purpose:** Session tracking and WebSocket connection management
- Active sessions
- Connection status (connected/disconnected timestamps)
- Session activity tracking

**Managed by:** `KlawedSandboxService` (part of session lifecycle)

---

### 3. Container Tracking Database (`containers.db`)
**Location:**
- Dev: `data/containers.db`
- Prod: `/var/lib/filesurf/data/containers.db`

**Purpose:** Container lifecycle and monitoring
- Container state tracking
- Container creation/termination history
- Restart attempts and health status

**Retention:** 14 days (configurable via `container.tracking.retention.days`)

---

### 4. Feedback Database (`feedback.db`) ✨ NEW
**Location:**
- Dev: `data/feedback.db`
- Prod: `/var/lib/filesurf/data/feedback.db`

**Purpose:** User feedback storage
- User feedback submissions
- Bug reports
- Feature requests
- Error details and environment information

**Managed by:** `FeedbackDatabaseManager`

**Schema:**
```sql
CREATE TABLE feedback (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    feedback_id TEXT NOT NULL UNIQUE,
    type TEXT NOT NULL,
    description TEXT NOT NULL,
    user_id TEXT NOT NULL,
    user_email TEXT NOT NULL,
    error_details TEXT,
    environment TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Indexes
CREATE INDEX idx_feedback_feedback_id ON feedback(feedback_id);
CREATE INDEX idx_feedback_user_id ON feedback(user_id);
CREATE INDEX idx_feedback_type ON feedback(type);
CREATE INDEX idx_feedback_created_at ON feedback(created_at);
```

---

### 5. Klawed Message Queues (per-session)
**Location:**
- Dev: `data/klawed-messages/klawed_messages_{sessionId}.db`
- Prod: `/var/lib/filesurf/data/klawed-messages/klawed_messages_{sessionId}.db`

**Purpose:** SQLite queue for klawed agent communication
- Message passing between client and klawed agent
- One database file per active session

**Lifecycle:** 
- Created when session starts
- Cleaned up 60 days after last modification (daily cleanup at 3 AM)

---

## Benefits of Separation

1. **Independent Backups**: Each database can be backed up separately
2. **Performance**: No lock contention between different subsystems
3. **Lifecycle Management**: Each database has its own retention policy
4. **Easier Debugging**: Issues can be isolated to specific databases
5. **Clean Migration**: Databases can be moved or migrated independently

## Configuration Reference

All database paths are configured in `application.properties`:

```properties
# Main database (managed by SQLiteManager)
quarkus.datasource.jdbc.url=jdbc:sqlite:data/filesurf.db

# Sessions database
klawed.sessions.db.path=data/sessions.db

# Container tracking database
container.tracking.db.path=data/containers.db

# Feedback database
feedback.db.path=data/feedback.db

# Klawed message queues directory
klawed.sqlite-queue.db-dir=./data/klawed-messages
```

Production overrides use `%prod.` prefix for all paths.
