#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 JVM Deployment Script"
echo "================================"
echo ""

# Configuration
DEPLOY_DIR="/root/filesurf_v2_work"
DATA_DIR="/var/lib/filesurf"
LOG_DIR="/var/log/filesurf"
SERVICE_NAME="filesurf-v2"

echo "Step 1: Creating required directories..."
mkdir -p "$DATA_DIR/data"
mkdir -p "$DATA_DIR/persistent"
mkdir -p "$DATA_DIR/sessions"
mkdir -p "$LOG_DIR"

echo "Step 2: Setting permissions..."
chmod 755 "$DATA_DIR"
chmod 755 "$LOG_DIR"
chmod 700 "$DATA_DIR/data"

echo "Step 3: Checking for JVM build..."
if [ -f "$DEPLOY_DIR/target/quarkus-app/quarkus-run.jar" ]; then
    echo "   ✓ JVM build found"
    ls -lh "$DEPLOY_DIR/target/quarkus-app/quarkus-run.jar"
else
    echo "   ✗ JVM build not found at $DEPLOY_DIR/target/quarkus-app/quarkus-run.jar"
    echo "   Run './deployment/build-jvm.sh' to build it first"
    exit 1
fi

echo "Step 4: Installing systemd service..."
cp "$DEPLOY_DIR/deployment/filesurf-v2-jvm.service" /etc/systemd/system/filesurf-v2.service
systemctl daemon-reload

echo "Step 5: Service installation complete!"
echo ""
echo "To start the service:"
echo "  sudo systemctl enable $SERVICE_NAME"
echo "  sudo systemctl start $SERVICE_NAME"
echo ""
echo "To check status:"
echo "  sudo systemctl status $SERVICE_NAME"
echo ""
echo "To view logs:"
echo "  sudo journalctl -u $SERVICE_NAME -f"
echo ""
echo "Application will be available on port 9090"
echo "Metrics endpoint: http://localhost:9090/metrics"
