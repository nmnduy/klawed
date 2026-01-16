#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 Remote Deployment"
echo "Build Local + Rsync to Server"
echo "================================"
echo ""

# Configuration
REMOTE_HOST="filesurf-0"
REMOTE_PATH="/root/filesurf_v2"
LOCAL_PATH="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL_TEST_PORT="${LOCAL_TEST_PORT:-8085}"
LOCAL_TEST_DURATION="${LOCAL_TEST_DURATION:-10}"

echo "Local path:  $LOCAL_PATH"
echo "Remote host: $REMOTE_HOST"
echo "Remote path: $REMOTE_PATH"
echo ""

# Step 0: Rotate rollback tags and tag current commit as production
echo "Step 0: Updating production/rollback tags..."
cd "$LOCAL_PATH"

# Check if we're currently on a rollback tag
CURRENT_COMMIT=$(git rev-parse HEAD)
IS_ROLLBACK=false

if git show-ref --tags --quiet --verify refs/tags/production-rollback-n1; then
    if [ "$CURRENT_COMMIT" = "$(git rev-parse production-rollback-n1)" ]; then
        IS_ROLLBACK=true
        echo "   ⚠ Currently on production-rollback-n1 tag"
    fi
fi
if git show-ref --tags --quiet --verify refs/tags/production-rollback-n2; then
    if [ "$CURRENT_COMMIT" = "$(git rev-parse production-rollback-n2)" ]; then
        IS_ROLLBACK=true
        echo "   ⚠ Currently on production-rollback-n2 tag"
    fi
fi
if git show-ref --tags --quiet --verify refs/tags/production-rollback-n3; then
    if [ "$CURRENT_COMMIT" = "$(git rev-parse production-rollback-n3)" ]; then
        IS_ROLLBACK=true
        echo "   ⚠ Currently on production-rollback-n3 tag"
    fi
fi

if [ "$IS_ROLLBACK" = true ]; then
    echo "   → SKIPPING tag rotation (deploying from rollback tag)"
    echo "   → Will deploy current commit $(git rev-parse --short HEAD) without modifying production tags"
    echo ""
else
    # Find previous production commit (if it exists)
    PREV_PROD_COMMIT=""
    if git show-ref --tags --quiet --verify refs/tags/production; then
        PREV_PROD_COMMIT=$(git rev-parse production)
        echo "   Previous production commit: $PREV_PROD_COMMIT"
    else
        echo "   No existing 'production' tag found (first run?)"
    fi

    # Rotate rollback tags (n2 -> n3, n1 -> n2)
    if git show-ref --tags --quiet --verify refs/tags/production-rollback-n3; then
        git tag -d production-rollback-n3 >/dev/null
    fi
    if git show-ref --tags --quiet --verify refs/tags/production-rollback-n2; then
        git tag -f production-rollback-n3 "$(git rev-parse production-rollback-n2)" >/dev/null
        echo "   → Moved production-rollback-n2 to production-rollback-n3"
    fi
    if git show-ref --tags --quiet --verify refs/tags/production-rollback-n1; then
        git tag -f production-rollback-n2 "$(git rev-parse production-rollback-n1)" >/dev/null
        echo "   → Moved production-rollback-n1 to production-rollback-n2"
    fi

    # Tag previous production as newest rollback (n1)
    if [ -n "$PREV_PROD_COMMIT" ]; then
        git tag -f production-rollback-n1 "$PREV_PROD_COMMIT" >/dev/null
        echo "   → Tagged previous production as production-rollback-n1"
    fi

    # Tag current commit as production
    git tag -f production HEAD
    git push -f origin production production-rollback-n1 production-rollback-n2 production-rollback-n3 2>/dev/null || echo '   (Could not push tags to origin)'
    echo "   ✓ Tagged $(git rev-parse --short HEAD) as 'production' (rollbacks rotated)"
    echo ""
fi

# Step 1: Build assets locally
echo "Step 1: Building CSS assets locally..."
cd "$LOCAL_PATH"
npm run build
echo "   ✓ CSS build complete"
echo ""

# Step 2: Build application
echo "Step 2: Building application..."
mvn clean package -DskipTests -Dquarkus.profile=prod
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

# Start the application in background
cd "$LOCAL_PATH/target/quarkus-app"
java \
    -Dquarkus.http.port="$LOCAL_TEST_PORT" \
    -Dquarkus.datasource.jdbc.url="jdbc:sqlite:$LOCAL_TEST_DIR/data/test.db?journal_mode=WAL" \
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

# Step 6: Deploy on remote server
echo "Step 6: Deploying on remote server..."
echo ""
ssh "$REMOTE_HOST" "cd $REMOTE_PATH && ./deployment/deploy-jvm.sh"

echo ""
echo "================================"
echo "Deployment Complete!"
echo "================================"
echo ""
echo "The application has been built locally and deployed to $REMOTE_HOST"
echo ""
echo "To check service status:"
echo "  ssh $REMOTE_HOST 'systemctl status filesurf-v2'"
echo ""
echo "To view logs:"
echo "  ssh $REMOTE_HOST 'journalctl -u filesurf-v2 -f'"
echo ""
