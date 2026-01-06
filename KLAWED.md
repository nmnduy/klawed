# FileSurf v2 - Klawed Agent Project

## Project Overview
FileSurf v2 is a Quarkus-based application for file management and chat functionality with AI integration.

## Current Status (2026-01-06)
- ✅ Application codebase is clean and ready for development
- ✅ All previous Quarkus processes have been cleaned up
- ✅ Ports 8080, 8081, and 8082 are now available
- ✅ Database directory (`data/`) exists and is ready
- ✅ Logging configuration is properly set up

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

## Database
- SQLite database: `data/filesurf.db`
- Schema is automatically initialized on first run
- Uses Write-Ahead Logging (WAL) mode for better concurrency

## Cookie Configuration
- Cookie name: `filesurf_userId` (application-specific to avoid conflicts with other apps)
- Cookie path: `/` (root path)
- HttpOnly: `true` (secure, not accessible from JavaScript)
- Secure: Configurable via `cookie.secure` property (defaults to `false` for development)
- The cookie name is specific to FileSurf to prevent interference with other applications running on the same host

## Dependencies
- Quarkus 3.16.4
- Qute templates
- WebSockets
- SQLite JDBC
- Tailwind CSS (via npm)

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

### Known Issues & Fixes
1. **Template Name Mismatch (Fixed)**: 
   - Was: Java code expected `invoiceChat.html`, file was `fileChat.html`
   - Fix: Updated Java code to use `fileChat.html` (correct for FileSurf)
   - Location: `src/main/resources/templates/fileChat.html`