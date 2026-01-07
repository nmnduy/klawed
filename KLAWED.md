# FileSurf v2 - Klawed Agent Project

## Project Overview
FileSurf v2 is a Quarkus-based application for file management and chat functionality with AI integration.

## Current Status (2026-01-06)
- ✅ Application codebase is clean and ready for development
- ✅ All previous Quarkus processes have been cleaned up
- ✅ Ports 8080, 8081, and 8082 are now available
- ✅ Database directory (`data/`) exists and is ready
- ✅ Logging configuration is properly set up
- ✅ **Email-based authentication** added (2026-01-07)
- ✅ **Dark mode support** with system preference detection (2026-01-07)

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

## Project Structure
```
src/main/java/com/filesurf/     # Java source code
src/main/resources/templates/   # Qute HTML templates
src/main/resources/css/         # CSS styles
data/                          # SQLite database directory
logs/                          # Application logs
```

## Key Endpoints
- `GET /file-chat` - Main chat interface
- WebSocket: `/file-chat/ws/{sessionId}`
- REST API: Various endpoints under `/file-chat/http/`
- **Monitoring**: `GET /metrics` - Prometheus metrics endpoint (standard format)

## REST API Endpoints
### Authentication
- `GET /auth/login` - Login page
- `POST /auth/login` - Login with email
- `GET /auth/status` - Check auth status
- `POST /auth/logout` - Logout

### Session Management
- `GET /session/generate` - Generate new session (requires auth)
- `GET /session/count` - Get session count
- `POST /file-chat/upload` - Upload files
- `GET /file-chat/upload/list` - List uploaded files
- `GET /file-chat/explorer/list` - List files in session
- `GET /file-chat/explorer/metadata` - Get file metadata
- `GET /file-chat/explorer/open` - Open/download file
- `GET /file-chat/explorer/preview` - Preview text file
- `POST /file-chat/explorer/compile-latex` - Compile LaTeX
- `POST /file-chat/http/session/{sessionId}` - Create/update session
- `DELETE /file-chat/http/session/{sessionId}` - Delete session
- `POST /file-chat/http/session/{sessionId}/conclude` - Conclude session
- `POST /file-chat/http/message/{sessionId}` - Send message
- `GET /file-chat/http/messages/{sessionId}` - Get messages
- `GET /file-chat/http/poll/{sessionId}` - Poll for messages

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
# Invite a new user
./scripts/invite-user.sh user@example.com

# List all invited users
./scripts/invite-user.sh --list

# Deactivate a user (they can no longer log in)
./scripts/invite-user.sh --deactivate user@example.com

# Re-activate a user
./scripts/invite-user.sh --activate user@example.com
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
- `/health/*`, `/metrics`, `/q/*` - Health and monitoring

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

## Building for Production
```bash
# Build CSS and Quarkus JAR
make build-dist

# Or manually:
npm run build  # Build Tailwind CSS
mvn clean package -DskipTests
```

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
- **File uploads**: Users can upload any files to their session directories
- **Monitoring**: `/metrics` endpoint is publicly accessible. Consider IP whitelisting or basic auth
