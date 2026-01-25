# FileSurf v2 - Klawed Agent Project

## Project Overview
FileSurf v2 is a Quarkus-based application for file management and chat functionality with AI integration.

## Quick Start
```bash
# 1. Start Quarkus in development mode (in a separate terminal)
mvn quarkus:dev

# 2. Access the application
# Default: http://localhost:8080/file-chat
# Or use custom port: http://localhost:8082/file-chat

# 3. View logs
tail -f logs/application.log
```

## Development

**DO NOT COMMIT EXTRA MARKDOWN FILES UNLESS SPECIFICALLY ASKED TO DO SO**

### Using Git

You're encoureaged to create commits when finish a task, fix a bug or adding a new feature. Unless specified otherwise, do not stage markdown files for commit. I will review them and commit myself.

### Running Quarkus in Development Mode
1. **Start Quarkus in a separate shell**:
   ```bash
   # In the project root directory
   mvn quarkus:dev
   ```
   This will start the application on port 8080 (or another port if specified).

2. **Hot Reload Behavior**:
   - Changes to Java code, templates, and resources will be automatically picked up by Quarkus
   - The application will restart automatically when files are modified
   - This works seamlessly when the AI agent is **not** modifying code in a worktree

3. **Important Note on Worktrees**:
   - If the AI agent is working in a **git worktree**, hot reload may not work correctly
   - Quarkus dev mode monitors the main project directory, not worktree directories
   - For worktree development, you may need to:
     - Copy changes back to the main directory, OR
     - Restart Quarkus manually after changes

### Port Configuration
- Default port: 8080
- Alternative port (if 8080 is busy): 8082
- To specify a custom port:
  ```bash
  mvn quarkus:dev -Dquarkus.http.port=8081
  ```

### Logging
- Application logs: `logs/application.log` (rotates at 10MB)
- Klawed agent logs: `logs/klawed-agents.log` (rotates at 50MB)
- Console output also available in dev mode

### JavaScript Conventions
See `docs/JS_CONVENTIONS.md` for full details. Key points:
- Classic scripts use `<script defer>` with **NO** `export`/`import`
- ES modules use `<script type="module">` and MAY use `export`/`import`
- All JavaScript files are automatically validated for syntax errors during build
- Run `npm run check:js` to validate JavaScript files manually
- Run `npm run build` to validate conventions automatically

### Klawed Communication Mode
FileSurf v2 uses SQLite queue mode for communication with klawed agents:

- **SQLite Queue Mode**: Uses SQLite database for message passing (production standard)
- Provides reliable message delivery and persistence
- Each session has its own SQLite database file for klawed communication

#### Configuration
```properties
# SQLite queue configuration
klawed.sqlite-queue.db-path=klawed_messages.db
klawed.sqlite-queue.sender-name=client
klawed.sqlite-queue.receiver-name=klawed

# Session tracking database
klawed.sessions.db.path=data/sessions.db
```

Note: Unix socket mode has been removed in favor of the more reliable SQLite queue approach.

## SKILLS Organization Guidance

**Format:** All SKILLS should be **flat markdown files** in `src/main/resources/SKILLS/`.
- ✅ Good: `SKILLS/ocr.md` - Flat markdown file
- ❌ Bad: `SKILLS/ocr/README.md` - Nested folder structure

**Organization Principle:** Organize by **goal/outcome**, not by tool name.
- The markdown filename should reflect the user goal or capability (e.g., `latex.md`, `typst.md`, `ocr.md`)
- Document the tools and techniques inside the markdown file
- Keep each skill self-contained with installation instructions, usage examples, and troubleshooting tips

**Current Skills:**
- `github.md` - Browse and analyze GitHub repositories (n8n connectors) without full clone
- `latex.md` - PDF generation using LaTeX with comprehensive recipes
- `ocr.md` - Optical Character Recognition for extracting text
- `search_engines.md` - Search engine integration capabilities
- `typst.md` - PDF generation using Typst (modern LaTeX alternative)
- `web_browse_agent.md` - Web browsing and content extraction

**When Adding New Skills:**
1. Create a single markdown file named after the goal (e.g., `video_editing.md`, `data_visualization.md`)
2. Include comprehensive documentation: what it does, installation, usage, examples, troubleshooting
3. Update this list with a brief description of the new skill

## Project Structure
```
src/main/java/com/filesurf/     # Java source code
src/main/resources/templates/   # Qute HTML templates
src/main/resources/css/         # CSS styles
data/                          # Local development data (not synced to production)
data/demos/                    # Demo MP4 videos (dev only)
logs/                          # Application logs (dev only)
```

### Production Paths (/var/lib/filesurf/)
```
/var/lib/filesurf/data/        # SQLite database
/var/lib/filesurf/persistent/  # Persistent user data
/var/lib/filesurf/demos/       # Demo MP4 videos
/var/log/filesurf/             # Application logs
```

## Key Endpoints
- `GET /file-chat` - Main chat interface
- WebSocket: `/file-chat/ws/{sessionId}`
- REST API: Various endpoints under `/file-chat/http/`
- **Demo Videos**: `GET /demo/{name}` - Stream demo MP4 videos
- **Monitoring**: `GET /metrics` - Prometheus metrics endpoint (Tailscale-only access on port 9090)
- **Sanity Tests**: Automated production health checks run every 8 hours (see `docs/PRODUCTION_SANITY_TEST.md`)

## REST API Endpoints
### Authentication
- `GET /auth/login` - Login page
- `POST /auth/login` - Login with email
- `GET /auth/status` - Check auth status
- `POST /auth/logout` - Logout

### Session Management
- `GET /session/generate` - Generate new session (requires auth)
- `GET /session/count` - Get session count

### File Upload
- `POST /file-chat/upload` - Upload files (standard, up to 100 MB)
- `GET /file-chat/upload/list` - List uploaded files
- `POST /file-chat/upload/chunked/init` - Initialize chunked upload (for large files up to 1 GB)
- `POST /file-chat/upload/chunked/chunk` - Upload file chunk
- `GET /file-chat/upload/chunked/status/{uploadId}` - Get chunked upload status
- `DELETE /file-chat/upload/chunked/{uploadId}` - Cancel chunked upload

#### File Type Validation (Security)
**All upload endpoints enforce strict file type validation:**
- ✅ **Whitelist approach**: Only approved file types are allowed
- ❌ **Blocked**: Executables (exe, sh, bat, cmd, msi, app, dmg, deb, rpm, dll, so)
- ❌ **Blocked**: Scripts (vbs, ps1, py, rb, pl), Java archives (jar), installers
- ✅ **Allowed**: Documents (pdf, doc, docx, txt, md), Images (png, jpg, gif), Spreadsheets (csv, xlsx), Archives (zip, tar, gz), Code/Text (json, xml, yaml, tex, log, html, css, js), Subtitles (srt, vtt, ass, sub)
- See `docs/FILE_TYPE_VALIDATION.md` for complete details

### File Explorer

- `GET /file-chat/explorer/list` - List files in session
- `GET /file-chat/explorer/metadata` - Get file metadata
- `GET /file-chat/explorer/open` - Open/download file
- `GET /file-chat/explorer/preview` - Preview text file
- `POST /file-chat/explorer/compile-latex` - Compile LaTeX
- `DELETE /file-chat/explorer/delete` - Delete a file from session
- `POST /file-chat/http/session/{sessionId}` - Create/update session
- `DELETE /file-chat/http/session/{sessionId}` - Delete session
- `POST /file-chat/http/session/{sessionId}/conclude` - Conclude session
- `POST /file-chat/http/message/{sessionId}` - Send message
- `GET /file-chat/http/messages/{sessionId}` - Get messages
- `GET /file-chat/http/poll/{sessionId}` - Poll for messages

### Demo Videos
- `GET /demo` - Demo videos page (HTML)
- `GET /demo/list` - List available demo videos (JSON)
- `GET /demo/{name}` - Stream demo video (supports HTTP Range requests for seeking)

## Database
- SQLite database: `data/filesurf.db`
- Schema is automatically initialized on first run
- Uses Write-Ahead Logging (WAL) mode for better concurrency

## Cookie Configuration
- Cookie name: `filesurf_userId` (application-specific to avoid conflicts with other apps)
- Cookie path: `/` (root path)
- HttpOnly: `true` (secure, not accessible from JavaScript)
- Secure: Configurable via `cookie.secure` property (defaults to `false` for development)
- Cookie duration: 365 days
- The cookie name is specific to FileSurf to prevent interference with other applications running on the same host

## Authentication

### Email-Based Authentication (Invite-Only)
Users must be invited before they can access the application. The authentication flow:

1. **Admin Invites User**: Admin runs `./scripts/invite-user.sh user@example.com`
2. **First Visit**: Users without a valid `filesurf_userId` cookie are redirected to `/auth/login`
3. **Login**: User enters their invited email address on the login page
4. **User Lookup**: System verifies email exists in database (no auto-registration)
5. **Cookie Set**: `filesurf_userId` cookie is set with 365-day expiration
6. **Access Granted**: User can now access all protected endpoints

### Managing Users (Admin Scripts)
```bash
# Invite a new user (creates if doesn't exist)
./scripts/invite-user.sh user@example.com

# Activate a user (creates if doesn't exist)
./scripts/invite-user.sh --activate user@example.com
# or: ./scripts/invite-user.sh -a user@example.com

# List all invited users
./scripts/invite-user.sh --list

# Deactivate a user (they can no longer log in)
./scripts/invite-user.sh --deactivate user@example.com
```

### Auth Endpoints
- `GET /auth/login` - Login page (HTML form)
- `POST /auth/login` - Submit login (form or JSON)
- `GET /auth/status` - Check authentication status (JSON)
- `POST /auth/logout` - Clear authentication cookie

### Protected vs Public Endpoints
**Public (no auth required):**
- `/auth/*` - Authentication endpoints
- `/assets/*`, `/js/*`, `/css/*` - Static assets
- `/health/*`, `/q/*` - Health and monitoring

**Network-Restricted (Tailscale only):**
- `/metrics` - Prometheus metrics endpoint (only accessible from 100.x.x.x network)

**Protected (auth required):**
- `/file-chat` - Main chat interface
- `/session/*` - Session management
- `/file-chat/upload/*` - File uploads
- `/file-chat/explorer/*` - File explorer
- All WebSocket connections

### Database Schema
The `users` table links emails to userIds:
```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL UNIQUE,      -- Cookie value (e.g., "user-uuid")
    email TEXT NOT NULL UNIQUE,         -- User's email address
    created_at TIMESTAMP NOT NULL,
    last_login_at TIMESTAMP,
    is_active BOOLEAN NOT NULL DEFAULT TRUE
);
```

## Dependencies
- Quarkus 3.16.4
- Qute templates
- WebSockets
- SQLite JDBC
- Tailwind CSS (via npm)
- **Monitoring**: Micrometer + Prometheus registry
- **Dark Mode**: Class-based theming with localStorage persistence (see `docs/DARK_MODE.md`)

## Testing Tools

### WebSocket Bridge Server (Python)
A persistent WebSocket connection testing tool for AI agents using a Python-based bridge server:

**Start the bridge server:**
```bash
# Start on port 5000, connecting to FileSurf on port 9090
python3 scripts/ws_bridge_server.py --port 5000 --file-surf-port 9090

# Or with custom email
python3 scripts/ws_bridge_server.py --email user@example.com
```

**Send a message:**
```bash
# Simple message
curl -X POST http://localhost:5000/message -d "Generate a Solana wallet"

# Or with JSON
curl -X POST http://localhost:5000/message \
  -H "Content-Type: application/json" \
  -d '{"content": "List files in current directory"}'
```

**Poll for responses:**
```bash
# Basic poll (waits up to 2 seconds for responses)
curl http://localhost:5000/poll

# Poll with longer timeout (5 seconds)
curl "http://localhost:5000/poll?timeout=5&clear=false"

# Stream responses (Server-Sent Events)
curl -N http://localhost:5000/poll/stream
```

**Check status:**
```bash
curl http://localhost:5000/status
# Returns: {"connected": true, "session_id": "...", "pending_responses": 3}
```

**Session management:**
```bash
# Get current session ID
curl http://localhost:5000/session

# Close current session and create new one
curl -X DELETE http://localhost:5000/session

# Close WebSocket connection
curl -X POST http://localhost:5000/close
```

**Python example:**
```python
import requests

# Send message
requests.post("http://localhost:5000/message", data="Generate a Solana wallet")

# Poll for response
response = requests.get("http://localhost:5000/poll")
data = response.json()
print(f"Received {data['count']} messages:")
for msg in data['messages']:
    print(f"  - {msg['content'][:100]}...")
```

**HTTP Endpoints:**
- `POST /message` - Send a message to the WebSocket
- `GET /poll` - Poll for responses (default 2s timeout)
- `GET /poll/stream` - Stream responses (Server-Sent Events)
- `GET /status` - Check connection status
- `GET /session` - Get current session ID
- `DELETE /session` - Close and create new session
- `POST /close` - Close WebSocket connection

Dependencies: `pip install flask requests websockets`

## Building for Development
```bash
# Build CSS and JS without cache busting (faster for development)
bun run build:dev      # Creates main.css and *.js (no hashes)
# OR
make css-dev           # Same as above

# Build CSS and JS with cache busting (production mode)
bun run build          # Creates main.[hash].css and *.[hash].js
# OR
make css               # Same as above
```

### CSS & JS Cache Busting
Both CSS and JS files are automatically hashed during **production** builds to prevent browser caching issues:
- **Production** (`npm run build`):
  - CSS: Generates `main.[hash].css` (e.g., `main.d44edce3.css`)
  - JS: Generates `[name].[hash].js` (e.g., `fileChat.9bc4af5d.js`)
  - All hashed files are output to `dist/` directory
- **Development** (`npm run build:dev`):
  - CSS: Generates `main.css` (no hash)
  - JS: Generates standard filenames (no hashes, faster iteration)
- Templates use `{cssPath}` and `{inject:jsHelper.path('name')}` which automatically resolve to correct filenames
- Hashes change whenever content changes, forcing browser cache invalidation
- See `docs/CSS_CACHE_BUSTING.md` and `docs/JS_CACHE_BUSTING.md` for full details

## Troubleshooting

### Port Already in Use
```bash
# Check what's using the port
lsof -i :8080

# Kill the process
kill <pid>

# Or use a different port
mvn quarkus:dev -Dquarkus.http.port=8082
```

### Database Issues
- Ensure `data/` directory exists
- Check file permissions for SQLite database

### Template Changes Not Reflecting
- Verify you're not working in a git worktree
- Check that template files are in `src/main/resources/templates/`
- Restart Quarkus if changes aren't picked up automatically

### Security Considerations for Production
- **Invite-only authentication**: Only pre-invited email addresses can access the app. No self-registration.
- **Session isolation**: Each user session is isolated based on their userId
- **Exposed endpoints**: Protected endpoints require valid authentication cookie
- **File type validation**: Strict whitelist prevents malware uploads (exe, sh, bat, cmd, etc. are blocked)
- **Monitoring**: `/metrics` endpoint is restricted to Tailscale network (100.x.x.x) only

## Deployment

`deployment/deploy-rsync.sh`

## Background Services

docs/background_services.md
