#!/bin/bash
# Diagnostic script for Podman klawed container issues
# Run this on the production server to diagnose communication problems

set -e

echo "=== Klawed Container Diagnostics ==="
echo ""

# 1. Check running containers
echo "1. Running klawed containers:"
podman ps --filter "name=klawed-" || echo "   No klawed containers found"
echo ""

# 2. Check for any crashed containers
echo "2. All klawed containers (including stopped):"
podman ps -a --filter "name=klawed-" || echo "   None found"
echo ""

# 3. Check last container's logs
echo "3. Last container logs (if any):"
LAST_CONTAINER=$(podman ps -a --filter "name=klawed-" --format "{{.Names}}" | head -1)
if [ -n "$LAST_CONTAINER" ]; then
    echo "   Container: $LAST_CONTAINER"
    podman logs --tail 50 "$LAST_CONTAINER" || echo "   Could not get logs"
else
    echo "   No containers found"
fi
echo ""

# 4. Check file permissions in a test container
echo "4. Testing container filesystem access:"
TEST_DIR="/tmp/klawed-test-$$"
mkdir -p "$TEST_DIR"
echo "   Test directory: $TEST_DIR"
podman run --rm \
    --user 0:0 \
    -v "$TEST_DIR:/workspace:Z" \
    -w /workspace \
    klawed-sandbox:latest \
    /bin/sh -c "mkdir -p /workspace/.klawed/logs && touch /workspace/.klawed/logs/test.log && ls -la /workspace/.klawed/logs && echo 'SUCCESS: Can write to mounted volume'"
rm -rf "$TEST_DIR"
echo ""

# 5. Check user namespace configuration
echo "5. Podman user namespace configuration:"
podman info 2>/dev/null | grep -i "userns\|uid\|gid" || echo "   Could not get podman info"
echo ""

# 6. Check filesurf service status
echo "6. FileSurf service status:"
systemctl status filesurf-v2 --no-pager | head -20 || echo "   Service not found"
echo ""

# 7. Check recent filesurf logs for errors
echo "7. Recent filesurf errors:"
journalctl -u filesurf-v2 --since "10 minutes ago" --no-pager | grep -i "error\|failed\|exception" | tail -20 || echo "   No recent errors"
echo ""

# 8. Check SQLite database files
echo "8. SQLite database files in persistent storage:"
find /root/filesurf_v2/data/persistent -name "klawed_messages_*.db" -type f 2>/dev/null | head -5 | while read f; do
    echo "   $f"
    ls -la "$f" 2>/dev/null || true
done
echo ""

# 9. Test SQLite queue communication manually
echo "9. Testing SQLite queue (if DB exists):"
DB_FILE=$(find /root/filesurf_v2/data/persistent -name "klawed_messages_*.db" -type f 2>/dev/null | head -1)
if [ -n "$DB_FILE" ]; then
    echo "   Database: $DB_FILE"
    echo "   Tables:"
    sqlite3 "$DB_FILE" ".tables" 2>/dev/null || echo "   Could not query database"
    echo "   Message count:"
    sqlite3 "$DB_FILE" "SELECT COUNT(*) FROM messages;" 2>/dev/null || echo "   Could not count messages"
    echo "   Pending messages:"
    sqlite3 "$DB_FILE" "SELECT COUNT(*) FROM messages WHERE sent = 0;" 2>/dev/null || echo "   Could not count pending"
else
    echo "   No SQLite database found"
fi
echo ""

echo "=== End of Diagnostics ==="
