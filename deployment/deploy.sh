#!/bin/bash
set -e

echo "================================"
echo "FileSurf v2 Deployment Script"
echo "================================"
echo ""

# Configuration
DEPLOY_DIR="/root/filesurf_v2"
DATA_DIR="/var/lib/filesurf"
LOG_DIR="/var/log/filesurf"
SERVICE_NAME="filesurf-v2"

echo "Step 1: Creating required directories..."
mkdir -p "$DATA_DIR/data"
mkdir -p "$DATA_DIR/persistent"
mkdir -p "$DATA_DIR/sessions"
mkdir -p "$DATA_DIR/demos"
mkdir -p "$LOG_DIR"

echo "Step 2: Setting permissions..."
chmod 755 "$DATA_DIR"
chmod 755 "$LOG_DIR"
chmod 700 "$DATA_DIR/data"

echo "Step 3: Checking for build artifacts..."
if [ -f "$DEPLOY_DIR/target/quarkus-app/quarkus-run.jar" ]; then
    echo "   ✓ Build found"
    ls -lh "$DEPLOY_DIR/target/quarkus-app/quarkus-run.jar"
else
    echo "   ✗ Build not found at $DEPLOY_DIR/target/quarkus-app/quarkus-run.jar"
    echo "   Run './deployment/build.sh' to build it first"
    exit 1
fi

echo "Step 4: Installing systemd service..."
cp "$DEPLOY_DIR/deployment/filesurf-v2.service" /etc/systemd/system/filesurf-v2.service
systemctl daemon-reload

echo "Step 5: Enabling and restarting service..."
systemctl enable $SERVICE_NAME

# Stop gracefully first (wait for containers to shut down)
if systemctl is-active --quiet $SERVICE_NAME; then
    echo "   Stopping existing service (waiting for graceful shutdown)..."
    systemctl stop $SERVICE_NAME
    echo "   ✓ Service stopped"
fi

# Start the new version
echo "   Starting new version..."
systemctl start $SERVICE_NAME

echo ""
echo "Step 6: Checking service status..."
sleep 3
systemctl status $SERVICE_NAME --no-pager || true

echo ""
echo "================================"
echo "Deployment Complete!"
echo "================================"
echo ""
echo "Service: $SERVICE_NAME"
echo "Status: $(systemctl is-active $SERVICE_NAME)"
echo "Application URL: http://localhost:9090"
echo "Metrics endpoint: http://localhost:9090/metrics"
echo ""
echo "To view logs:"
echo "  sudo journalctl -u $SERVICE_NAME -f"
echo ""
