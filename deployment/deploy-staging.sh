#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 STAGING Deployment"
echo "Build Local + Deploy to pie-01"
echo "================================"
echo ""

# Configuration
REMOTE_HOST="pie-01"
REMOTE_PATH="/root/filesurf_v2_staging"
LOCAL_PATH="$(cd "$(dirname "$0")/..") && pwd)"
LOCAL_TEST_PORT="${LOCAL_TEST_PORT:-9095}"
LOCAL_TEST_DURATION="${LOCAL_TEST_DURATION:-10}"

echo "Local path:  $LOCAL_PATH"
echo "Remote host: $REMOTE_HOST"
echo "Remote path: $REMOTE_PATH"
echo ""

# Step 0: Tag current commit as staging
echo "Step 0: Tagging current commit as staging..."
cd "$LOCAL_PATH"

CURRENT_COMMIT=$(git rev-parse HEAD)
git tag -f staging HEAD
git push -f origin staging 2>/dev/null || echo '   (Could not push tag to origin)'
echo "   ✓ Tagged $(git rev-parse --short HEAD) as 'staging'"
echo ""

# Step 1: Build assets locally
echo "Step 1: Building CSS assets locally..."
cd "$LOCAL_PATH"
npm run build
echo "   ✓ CSS build complete"
echo ""

# Step 2: Build application
echo "Step 2: Building application..."
mvn clean package -DskipTests -Dquarkus.profile=staging
echo "   ✓ Build complete"
echo ""

# Step 3: Verify build artifacts
echo "Step 3: Verifying build artifacts..."
if [ ! -f "$LOCAL_PATH/target/quarkus-app/quarkus-run.jar" ]; then
    echo "   ✗ ERROR: JVM build artifact not found!"
    exit 1
fi
if [ ! -d "$LOCAL_PATH/src/main/resources/META-INF/resources/dist" ]; then
    echo "   ✗ ERROR: dist directory not found!"
    exit 1
fi
echo "   ✓ All artifacts present"
ls -lh "$LOCAL_PATH/target/quarkus-app/quarkus-run.jar"
echo ""

# Step 4: Run locally to verify build works
echo "Step 4: Running build locally to verify..."
echo "   Starting on port $LOCAL_TEST_PORT for $LOCAL_TEST_DURATION seconds..."
echo ""

# Create temporary data directory for local test
LOCAL_TEST_DIR=$(mktemp -d)
mkdir -p "$LOCAL_TEST_DIR/data"
mkdir -p "$LOCAL_TEST_DIR/persistent"
mkdir -p "$LOCAL_TEST_DIR/demos"
mkdir -p "$LOCAL_TEST_DIR/klawed-messages"
mkdir -p "$LOCAL_TEST_DIR/chat-messages"

# Start the application in background
cd "$LOCAL_PATH/target/quarkus-app"
java \
    -Dquarkus.http.port="$LOCAL_TEST_PORT" \
    -Dquarkus.profile=staging \
    -Dquarkus.datasource.jdbc.url="jdbc:sqlite:$LOCAL_TEST_DIR/data/test.db?journal_mode=WAL" \
    -Dfilesurf.persist.root="$LOCAL_TEST_DIR/persistent" \
    -Ddemo.videos.directory="$LOCAL_TEST_DIR/demos" \
    -Dcontainer.tracking.db.path="$LOCAL_TEST_DIR/data/containers.db" \
    -Dfeedback.db.path="$LOCAL_TEST_DIR/data/feedback.db" \
    -Dklawed.sqlite-queue.db-dir="$LOCAL_TEST_DIR/klawed-messages" \
    -Dchat.messages.db-dir="$LOCAL_TEST_DIR/chat-messages" \
    -Dklawed.sessions.db.path="$LOCAL_TEST_DIR/data/sessions.db" \
    -Dblog.db.path="$LOCAL_TEST_DIR/data/blog.db" \
    -jar quarkus-run.jar &
LOCAL_PID=$!

# Give it time to start
sleep 5

# Check if process is still running
if ! kill -0 "$LOCAL_PID" 2>/dev/null; then
    echo "   ✗ ERROR: Application failed to start!"
    echo "   Check the output above for errors."
    rm -rf "$LOCAL_TEST_DIR"
    exit 1
fi

# Try to hit the health endpoint
echo "   Testing health endpoint..."
HEALTH_RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$LOCAL_TEST_PORT/q/health/ready" 2>/dev/null || echo "000")

if [ "$HEALTH_RESPONSE" = "200" ]; then
    echo "   ✓ Health check passed (HTTP $HEALTH_RESPONSE)"
else
    echo "   ⚠ Health check returned HTTP $HEALTH_RESPONSE (may still be starting)"
fi

# Let it run for a bit more to verify stability
echo "   Running for $LOCAL_TEST_DURATION seconds to verify stability..."
sleep "$LOCAL_TEST_DURATION"

# Check if still running
if kill -0 "$LOCAL_PID" 2>/dev/null; then
    echo "   ✓ Application ran successfully for $LOCAL_TEST_DURATION seconds"
    echo "   Stopping local test instance..."
    kill "$LOCAL_PID" 2>/dev/null || true
    wait "$LOCAL_PID" 2>/dev/null || true
else
    echo "   ✗ ERROR: Application crashed during test run!"
    rm -rf "$LOCAL_TEST_DIR"
    exit 1
fi

# Cleanup
rm -rf "$LOCAL_TEST_DIR"
echo "   ✓ Local verification complete"
echo ""

# Step 5: Sync to remote server
echo "Step 5: Syncing to $REMOTE_HOST..."
echo ""

# Create target directory on remote
ssh "$REMOTE_HOST" "mkdir -p $REMOTE_PATH/target"

# Sync quarkus-app directory (contains JAR and all dependencies)
echo "   → Syncing quarkus-app directory..."
rsync -avz --progress --delete \
    "$LOCAL_PATH/target/quarkus-app/" \
    "$REMOTE_HOST:$REMOTE_PATH/target/quarkus-app/"

# Sync deployment scripts and service files
echo "   → Syncing deployment/..."
rsync -avz \
    --exclude='*.md' \
    "$LOCAL_PATH/deployment/" \
    "$REMOTE_HOST:$REMOTE_PATH/deployment/"

# Sync admin scripts
echo "   → Syncing scripts/..."
rsync -avz \
    "$LOCAL_PATH/scripts/" \
    "$REMOTE_HOST:$REMOTE_PATH/scripts/"

# Sync source resources (templates, CSS, JS, built assets)
echo "   → Syncing src/main/resources/..."
rsync -avz \
    "$LOCAL_PATH/src/main/resources/" \
    "$REMOTE_HOST:$REMOTE_PATH/src/main/resources/"

# Sync package.json and pom.xml (for reference)
echo "   → Syncing project files..."
rsync -avz \
    "$LOCAL_PATH/pom.xml" \
    "$LOCAL_PATH/package.json" \
    "$REMOTE_HOST:$REMOTE_PATH/"

echo ""
echo "   ✓ Sync complete"
echo ""

# Step 6: Create required directories on remote server
echo "Step 6: Creating required directories on remote server..."
echo ""
# Create data directories for chat messages, klawed messages, etc.
ssh "$REMOTE_HOST" "mkdir -p /var/lib/filesurf-staging/data/chat-messages"
ssh "$REMOTE_HOST" "mkdir -p /var/lib/filesurf-staging/data/klawed-messages"
ssh "$REMOTE_HOST" "mkdir -p /var/lib/filesurf-staging/data/persistent"
ssh "$REMOTE_HOST" "mkdir -p /var/lib/filesurf-staging/demos"
ssh "$REMOTE_HOST" "mkdir -p /var/log/filesurf-staging"
ssh "$REMOTE_HOST" "mkdir -p /etc/filesurf-staging"
ssh "$REMOTE_HOST" "chmod 755 /var/lib/filesurf-staging/data/chat-messages"
ssh "$REMOTE_HOST" "chmod 755 /var/lib/filesurf-staging/data/klawed-messages"
ssh "$REMOTE_HOST" "chmod 755 /var/lib/filesurf-staging/data/persistent"
ssh "$REMOTE_HOST" "chmod 755 /var/lib/filesurf-staging/demos"
ssh "$REMOTE_HOST" "chmod 755 /var/log/filesurf-staging"
# Set ownership to root (service runs as root)
ssh "$REMOTE_HOST" "chown -R root:root /var/lib/filesurf-staging"
ssh "$REMOTE_HOST" "chown -R root:root /var/log/filesurf-staging"
echo "   ✓ Directories created and permissions set"

# Step 7: Install systemd service
echo ""
echo "Step 7: Installing systemd service..."
echo ""
ssh "$REMOTE_HOST" "cp $REMOTE_PATH/deployment/filesurf-v2-staging.service /etc/systemd/system/"
ssh "$REMOTE_HOST" "systemctl daemon-reload"
echo "   ✓ Service installed"
echo ""

# Step 8: Restart service
echo "Step 8: Restarting service..."
echo ""
ssh "$REMOTE_HOST" "systemctl restart filesurf-v2-staging"
ssh "$REMOTE_HOST" "systemctl enable filesurf-v2-staging"

# Wait a few seconds for service to start
sleep 5

# Check service status
echo ""
echo "Checking service status..."
ssh "$REMOTE_HOST" "systemctl status filesurf-v2-staging --no-pager" || true

echo ""
echo "================================"
echo "Deployment Complete!"
echo "================================"
echo ""
echo "Staging application deployed to $REMOTE_HOST:9090"
echo ""
echo "Next steps:"
echo "1. Configure Cloudflare Tunnel to point staging.filesurf.io to pie-01:9090"
echo "2. Copy environment files:"
echo "   - /etc/filesurf-staging/.env (main app config)"
echo "   - /etc/filesurf-staging/klawed.env (klawed container config)"
echo ""
echo "To check service status:"
echo "  ssh $REMOTE_HOST 'systemctl status filesurf-v2-staging'"
echo ""
echo "To view logs:"
echo "  ssh $REMOTE_HOST 'journalctl -u filesurf-v2-staging -f'"
echo ""
