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
mkdir -p "$LOG_DIR"

echo "Step 2: Setting permissions..."
chmod 755 "$DATA_DIR"
chmod 755 "$LOG_DIR"
chmod 700 "$DATA_DIR/data"

echo "Step 3: Checking for native executable..."
if [ -f "$DEPLOY_DIR/target/filesurf-1.0.0-SNAPSHOT-runner" ]; then
    echo "   ✓ Native executable found"
    chmod +x "$DEPLOY_DIR/target/filesurf-1.0.0-SNAPSHOT-runner"
else
    echo "   ✗ Native executable not found at $DEPLOY_DIR/target/filesurf-1.0.0-SNAPSHOT-runner"
    echo "   Run 'mvn clean package -Pnative' to build it first"
    exit 1
fi

echo "Step 4: Installing systemd service..."
cp "$DEPLOY_DIR/deployment/filesurf-v2.service" /etc/systemd/system/
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
