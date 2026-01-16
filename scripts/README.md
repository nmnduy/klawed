# FileSurf v2 Scripts

This directory contains utility scripts for managing and testing FileSurf v2.

## User Management

### invite-user.sh
Invite and manage users in the FileSurf system.

```bash
# Invite a new user
./scripts/invite-user.sh user@example.com

# Activate a user
./scripts/invite-user.sh --activate user@example.com
./scripts/invite-user.sh -a user@example.com

# List all invited users
./scripts/invite-user.sh --list

# Deactivate a user
./scripts/invite-user.sh --deactivate user@example.com
```

**Note:** This script is production-aware and automatically detects whether to use:
- Development DB: `data/filesurf.db`
- Production DB: `/var/lib/filesurf/data/filesurf.db`

## Testing

### test-deployment.sh
Run sanity tests after deployment to verify the application is working correctly.

```bash
./scripts/test-deployment.sh
```

**Tests:**
- Public endpoints (login, privacy pages)
- Protected endpoints (auth redirects)
- Static assets (CSS/JS)
- Security (metrics endpoint)
- Service health (active, database, logs)

**Expected output:** 10 tests passed

### test-api.sh
Comprehensive HTTP API test covering all major endpoints.

```bash
# Test with default user (nmnduy@gmail.com)
./scripts/test-api.sh

# Test with specific user
./scripts/test-api.sh user@example.com
```

**Tests 15 endpoints:**
- Authentication (login, status, logout)
- Session management (generate, count, update, conclude)
- File operations (list, upload, metadata, preview)
- Messaging (send, get messages, poll)

**Note:** This script runs tests on the production server via SSH because the `Secure` cookie flag requires HTTPS for external access but works with HTTP on localhost.

## Post-Deployment Workflow

After deploying with `deployment/deploy-rsync.sh`, run:

```bash
# 1. Quick sanity check
./scripts/test-deployment.sh

# 2. Full API test
./scripts/test-api.sh

# 3. Check service status
ssh filesurf-0 'systemctl status filesurf-v2'

# 4. View logs
ssh filesurf-0 'journalctl -u filesurf-v2 -f'
```

## Script Requirements

- **SSH access:** Scripts use `ssh filesurf-0` to connect to production server
- **curl:** Required for API testing
- **sqlite3:** Required for user management (on production server)

## Adding New Scripts

When adding new scripts to this directory:
1. Make them executable: `chmod +x scripts/your-script.sh`
2. Add documentation to this README
3. Follow the existing naming conventions
4. Include help text in the script (`-h` or `--help` flag)
