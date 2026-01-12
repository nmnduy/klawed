#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 Remote Deployment (Native)"
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

# Check if GraalVM is installed
if ! command -v native-image &> /dev/null; then
    echo "ERROR: GraalVM native-image not found!"
    echo "Please install GraalVM and native-image first."
    exit 1
fi

# Step 1: Build assets locally
echo "Step 1: Building CSS assets locally..."
cd "$LOCAL_PATH"
npm run build
echo "   ✓ CSS build complete"
echo ""

# Step 2: Build native application
echo "Step 2: Building native application..."
echo "This will take several minutes..."
mvn clean package -Pnative -DskipTests -Dquarkus.profile=prod
echo "   ✓ Native build complete"
echo ""

# Step 3: Verify build artifacts
echo "Step 3: Verifying build artifacts..."
if [ ! -f "$LOCAL_PATH/target/filesurf-1.0.0-SNAPSHOT-runner" ]; then
    echo "   ✗ ERROR: Native executable not found!"
    exit 1
fi
if [ ! -d "$LOCAL_PATH/src/main/resources/META-INF/resources/dist" ]; then
    echo "   ✗ ERROR: dist directory not found!"
    exit 1
fi
echo "   ✓ All artifacts present"
ls -lh "$LOCAL_PATH/target/filesurf-1.0.0-SNAPSHOT-runner"
echo ""

# Step 4: Sync to remote server
echo "Step 4: Syncing to $REMOTE_HOST..."
echo ""

# Create target directory on remote
ssh "$REMOTE_HOST" "mkdir -p $REMOTE_PATH/target"

# Sync native executable
echo "   → Syncing native executable..."
rsync -avz --progress \
    "$LOCAL_PATH/target/filesurf-1.0.0-SNAPSHOT-runner" \
    "$REMOTE_HOST:$REMOTE_PATH/target/"

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
ssh "$REMOTE_HOST" "cd $REMOTE_PATH && ./deployment/deploy.sh"

echo ""
echo "================================"
echo "Deployment Complete!"
echo "================================"
echo ""
echo "The native application has been built locally and deployed to $REMOTE_HOST"
echo ""
echo "To check service status:"
echo "  ssh $REMOTE_HOST 'systemctl status filesurf-v2'"
echo ""
echo "To view logs:"
echo "  ssh $REMOTE_HOST 'journalctl -u filesurf-v2 -f'"
echo ""
