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

echo "Local path:  $LOCAL_PATH"
echo "Remote host: $REMOTE_HOST"
echo "Remote path: $REMOTE_PATH"
echo ""

# Step 0: Tag current commit as production
echo "Step 0: Tagging current commit as 'production'..."
cd "$LOCAL_PATH"
git tag -f production HEAD
git push -f origin production 2>/dev/null || echo "   (Could not push tag to origin)"
echo "   ✓ Tagged $(git rev-parse --short HEAD) as 'production'"
echo ""

# Step 1: Build assets locally
echo "Step 1: Building CSS assets locally..."
cd "$LOCAL_PATH"
npm run build
echo "   ✓ CSS build complete"
echo ""

# Step 2: Build Java application
echo "Step 2: Building Java application (JVM mode)..."
mvn clean package -DskipTests -Dquarkus.profile=prod
echo "   ✓ Maven build complete"
echo ""

# Step 3: Verify build artifacts
echo "Step 3: Verifying build artifacts..."
if [ ! -f "$LOCAL_PATH/target/quarkus-app/quarkus-run.jar" ]; then
    echo "   ✗ ERROR: quarkus-run.jar not found!"
    exit 1
fi
if [ ! -d "$LOCAL_PATH/src/main/resources/META-INF/resources/dist" ]; then
    echo "   ✗ ERROR: dist directory not found!"
    exit 1
fi
echo "   ✓ All artifacts present"
echo ""

# Step 4: Sync to remote server
echo "Step 4: Syncing to $REMOTE_HOST..."
echo ""

# Sync target directory (built JAR and dependencies)
echo "   → Syncing target/quarkus-app/..."
rsync -avz --delete \
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

# Step 5: Deploy on remote server
echo "Step 5: Deploying on remote server..."
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
