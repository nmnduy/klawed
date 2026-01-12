# FileSurf v2 Deployment Guide

Complete guide for building, deploying, and debugging FileSurf v2 in production.

## Table of Contents
- [Quick Start](#quick-start)
- [Build Options](#build-options)
- [Deployment](#deployment)
- [Logging & Debugging](#logging--debugging)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Service Management](#service-management)
- [Monitoring](#monitoring)

---

## Quick Start

### For Production (Recommended: JVM Build)
```bash
# 1. Build the application
./deployment/build-jvm.sh

# 2. Deploy to production (specify klawed sandbox image version)
./deployment/deploy-jvm.sh --image-version 1.0.0

# 3. Start the service
sudo systemctl enable filesurf-v2
sudo systemctl start filesurf-v2

# 4. Check status
sudo systemctl status filesurf-v2
```

### For Low-Memory VPS (Native Build)
```bash
# 1. Build native executable (takes 5-10 minutes)
./deployment/build-native.sh

# 2. Deploy to production
./deployment/deploy.sh

# 3. Start the service
sudo systemctl enable filesurf-v2
sudo systemctl start filesurf-v2
```

---

## Build Options

### JVM Build (Faster, Recommended)
**Pros:**
- Fast build time (1-2 minutes)
- Better debugging capabilities
- Easier to troubleshoot
- Full JVM tooling support

**Cons:**
- Higher memory usage (~256MB)
- Slower startup time

**Build:**
```bash
./deployment/build-jvm.sh
```

**What it does:**
1. Cleans previous builds
2. Builds Tailwind CSS with cache-busting hashes
3. Packages Quarkus application as JAR

**Output:** `target/quarkus-app/quarkus-run.jar`

### Native Build (Lower Memory)
**Pros:**
- Very low memory usage (~50-100MB)
- Fast startup time
- Single executable file

**Cons:**
- Long build time (5-10 minutes)
- Requires GraalVM
- Limited debugging options

**Build:**
```bash
./deployment/build-native.sh
```

**What it does:**
1. Cleans previous builds
2. Builds Tailwind CSS with cache-busting hashes
3. Compiles native executable with GraalVM (5-10 minutes)

**Output:** `target/filesurf-1.0.0-SNAPSHOT-runner`

---

## Deployment

### Directory Structure
```
/root/filesurf_v2/             # Application directory
/var/lib/filesurf/             # Data directory
  ├── data/                    # SQLite database
  │   └── filesurf.db
  ├── persistent/              # Persistent storage
  └── sessions/                # Session files
/var/log/filesurf/             # Log directory
  ├── application.log          # Main application logs
  └── klawed-agents.log        # AI agent logs
```

### Deployment Steps

#### 1. Build the Application
Choose either JVM or Native build:
```bash
# JVM build (recommended)
./deployment/build-jvm.sh

# OR Native build (for low-memory VPS)
./deployment/build-native.sh
```

**Note:** Both build scripts automatically:
- Build Tailwind CSS with content-based cache busting
- Generate hashed CSS files (e.g., `main.b017b445.css`)
- Create `css-version.properties` for template resolution
- Package everything into the deployment artifact

CSS cache busting ensures browsers always load the latest styles without manual cache clearing.

#### 2. Create Required Directories
The deployment scripts handle this automatically, but if needed manually:
```bash
sudo mkdir -p /var/lib/filesurf/data
sudo mkdir -p /var/lib/filesurf/persistent
sudo mkdir -p /var/lib/filesurf/sessions
sudo mkdir -p /var/log/filesurf

sudo chmod 755 /var/lib/filesurf
sudo chmod 700 /var/lib/filesurf/data
sudo chmod 755 /var/log/filesurf
```

#### 3. Deploy the Service
```bash
# For JVM build (with klawed sandbox image version)
./deployment/deploy-jvm.sh --image-version 1.0.0

# Or specify full image name
./deployment/deploy-jvm.sh --image klawed-sandbox:1.0.0

# For Native build
./deployment/deploy.sh
```

**Important:** Always specify a klawed sandbox image version. Using `:latest` is not allowed for reproducibility.

#### 4. Start the Service
```bash
sudo systemctl daemon-reload
sudo systemctl enable filesurf-v2
sudo systemctl start filesurf-v2
```

---

## Logging & Debugging

### Log Locations

#### Development Mode
When running `mvn quarkus:dev`:
- **Application logs:** `./logs/application.log`
- **AI agent logs:** `./logs/klawed-agents.log`
- **Console output:** Terminal
- **Database:** `./data/filesurf.db`

#### Production Mode
When running as systemd service:
- **Application logs:** `/var/log/filesurf/application.log`
- **AI agent logs:** `/var/log/filesurf/klawed-agents.log`
- **Systemd journal:** `journalctl -u filesurf-v2`
- **Database:** `/var/lib/filesurf/data/filesurf.db`

### Log Files Details

#### Application Log (`application.log`)
- **Location (dev):** `./logs/application.log`
- **Location (prod):** `/var/log/filesurf/application.log`
- **Level:** INFO (prod), DEBUG (dev)
- **Max size:** 10MB
- **Rotation:** Daily + on boot
- **Backups:** 5 files
- **Contents:**
  - HTTP requests/responses
  - Database operations
  - WebSocket connections
  - Session management
  - Error stack traces

#### Klawed Agents Log (`klawed-agents.log`)
- **Location (dev):** `./logs/klawed-agents.log`
- **Location (prod):** `/var/log/filesurf/klawed-agents.log`
- **Level:** INFO
- **Max size:** 50MB
- **Rotation:** Daily + on boot
- **Backups:** 10 files
- **Contents:**
  - AI agent spawning/termination
  - Agent communication
  - Tool usage (file operations, bash commands)
  - Agent errors and warnings
  - Session-specific messages (includes sessionId)

### Viewing Logs

#### Real-time Log Monitoring
```bash
# Watch application logs
tail -f /var/log/filesurf/application.log

# Watch AI agent logs
tail -f /var/log/filesurf/klawed-agents.log

# Watch systemd journal
sudo journalctl -u filesurf-v2 -f

# Follow all logs together
sudo tail -f /var/log/filesurf/*.log
```

#### Searching Logs
```bash
# Find errors in application log
grep -i error /var/log/filesurf/application.log

# Find logs for specific session
grep "session-abc123" /var/log/filesurf/klawed-agents.log

# Search last 100 lines for exception
tail -100 /var/log/filesurf/application.log | grep -i exception

# Find logs from today
grep "$(date +%Y-%m-%d)" /var/log/filesurf/application.log
```

#### Rotated Log Files
```bash
# List all rotated logs
ls -lh /var/log/filesurf/

# View previous day's log
less /var/log/filesurf/application.log.2026-01-07

# Search all rotated logs
zgrep "error" /var/log/filesurf/application.log.* 2>/dev/null
```

### Debug Mode

#### Enable Debug Logging
Edit `/root/filesurf_v2/src/main/resources/application-prod.properties`:
```properties
# Change from INFO to DEBUG
quarkus.log.file.level=DEBUG
quarkus.log.level=DEBUG
```

Then restart the service:
```bash
sudo systemctl restart filesurf-v2
```

#### Temporary Debug Mode (Without Restart)
For JVM builds, you can attach a debugger:
```bash
# Stop the service
sudo systemctl stop filesurf-v2

# Run manually with debug logging
cd /root/filesurf_v2
java -Dquarkus.profile=prod \
     -Dquarkus.log.level=DEBUG \
     -jar target/quarkus-app/quarkus-run.jar
```

---

## Configuration

### Key Configuration Files

#### `application.properties` (Development)
```properties
# HTTP
quarkus.http.port=8080

# Logs
quarkus.log.file.path=logs/application.log
quarkus.log.handler.file.KLAWED_AGENTS.path=logs/klawed-agents.log

# Database
quarkus.datasource.jdbc.url=jdbc:sqlite:data/filesurf.db

# Storage
filesurf.persist.root=./data/persistent
filesurf.sessions.base-dir=/tmp/fs-sessions
```

#### `application-prod.properties` (Production)
```properties
# HTTP
quarkus.http.port=9090

# Logs
quarkus.log.file.path=/var/log/filesurf/application.log
quarkus.log.handler.file.KLAWED_AGENTS.path=/var/log/filesurf/klawed-agents.log

# Database
quarkus.datasource.jdbc.url=jdbc:sqlite:/var/lib/filesurf/data/filesurf.db

# Storage
filesurf.persist.root=/var/lib/filesurf/persistent
filesurf.sessions.base-dir=/var/lib/filesurf/sessions

# Security
cookie.secure=true
```

### Environment Variables

The systemd service can be customized with environment variables:

#### Klawed Sandbox Image Version
The deployment script sets the klawed sandbox Docker image version during deployment.

**Command line options:**
```bash
# Specify version tag
./deployment/deploy-jvm.sh --image-version 1.2.3

# Or specify full image name
./deployment/deploy-jvm.sh --image klawed-sandbox:1.2.3
```

The script will:
1. Validate that `:latest` is not used (for reproducibility)
2. Update the systemd service file with the specified version
3. Reload and restart the service

**Manual configuration:**
Edit `/etc/systemd/system/filesurf-v2.service`:
```ini
[Service]
Environment="KLAWED_SANDBOX_IMAGE=klawed-sandbox:1.2.3"
```

After editing:
```bash
sudo systemctl daemon-reload
sudo systemctl restart filesurf-v2
```

**Why not use `:latest`?**
- Ensures reproducible deployments
- Prevents unexpected behavior from container image updates
- Makes it clear which version is running
- Aligns with production best practices

#### JVM Memory Settings (JVM Build)
Edit `/etc/systemd/system/filesurf-v2.service`:
```ini
[Service]
Environment="JAVA_OPTS=-Xmx512m -Xms256m"  # Increase memory
```

#### Custom Port
```ini
[Service]
Environment="QUARKUS_HTTP_PORT=8080"
```

#### API Keys
```ini
[Service]
Environment="OPENAI_API_KEY=your-key-here"
```

After editing, reload:
```bash
sudo systemctl daemon-reload
sudo systemctl restart filesurf-v2
```

---

## Troubleshooting

### Common Issues

#### 1. Service Won't Start
```bash
# Check service status
sudo systemctl status filesurf-v2

# View recent logs
sudo journalctl -u filesurf-v2 -n 50

# Check for errors
sudo journalctl -u filesurf-v2 | grep -i error
```

**Common causes:**
- Port already in use (check with `lsof -i :9090`)
- Missing directories (run deploy script again)
- Database locked (stop all instances)
- Insufficient permissions

#### 2. Port Already in Use
```bash
# Find what's using port 9090
sudo lsof -i :9090

# Kill the process
sudo kill -9 <PID>

# Or change port in application-prod.properties
```

#### 3. Database Locked
```bash
# Check for lock files
ls -la /var/lib/filesurf/data/filesurf.db*

# Remove WAL files (ONLY if service is stopped)
sudo systemctl stop filesurf-v2
sudo rm /var/lib/filesurf/data/filesurf.db-wal
sudo rm /var/lib/filesurf/data/filesurf.db-shm
sudo systemctl start filesurf-v2
```

#### 4. Out of Memory (JVM Build)
```bash
# Check current memory usage
ps aux | grep quarkus-run.jar

# Reduce memory in service file
sudo nano /etc/systemd/system/filesurf-v2.service
# Change: -Xmx256m to -Xmx128m

sudo systemctl daemon-reload
sudo systemctl restart filesurf-v2
```

#### 5. Application Logs Not Appearing
```bash
# Check log directory permissions
ls -ld /var/log/filesurf/
sudo chmod 755 /var/log/filesurf/

# Check if log files exist
ls -la /var/log/filesurf/

# Create missing directories
sudo mkdir -p /var/log/filesurf/
sudo systemctl restart filesurf-v2
```

#### 6. WebSocket Connection Failures
```bash
# Check if service is listening
sudo netstat -tlnp | grep 9090

# Check firewall (if applicable)
sudo ufw status
sudo ufw allow 9090/tcp

# Check logs for WebSocket errors
grep -i websocket /var/log/filesurf/application.log
```

#### 7. AI Agent (Klawed) Issues
```bash
# Check if klawed binary exists
ls -la /usr/local/bin/klawed

# Check klawed logs
tail -f /var/log/filesurf/klawed-agents.log

# Check if API key is configured
grep OPENAI_API_KEY /etc/systemd/system/filesurf-v2.service

# Test klawed manually
/usr/local/bin/klawed --version
```

### Debugging Workflow

#### Step 1: Check Service Status
```bash
sudo systemctl status filesurf-v2
```
Look for: `Active: active (running)` or error messages

#### Step 2: Check Recent Logs
```bash
sudo journalctl -u filesurf-v2 -n 100
```
Look for: startup messages, errors, exceptions

#### Step 3: Check Application Logs
```bash
tail -50 /var/log/filesurf/application.log
```
Look for: HTTP errors, database errors, stack traces

#### Step 4: Check Port Binding
```bash
sudo lsof -i :9090
```
Should show: java or filesurf-runner listening

#### Step 5: Check Database
```bash
sqlite3 /var/lib/filesurf/data/filesurf.db "SELECT COUNT(*) FROM sessions;"
```
Should return: a number (not an error)

#### Step 6: Test Endpoint
```bash
curl http://localhost:9090/health/ready
curl http://localhost:9090/metrics
```
Should return: JSON response or metrics data

### Getting Help

When reporting issues, include:
```bash
# 1. Service status
sudo systemctl status filesurf-v2

# 2. Last 50 log lines
sudo journalctl -u filesurf-v2 -n 50

# 3. Application log errors
tail -100 /var/log/filesurf/application.log | grep -i error

# 4. System information
uname -a
free -h
df -h
```

---

## Service Management

### Basic Commands
```bash
# Start service
sudo systemctl start filesurf-v2

# Stop service
sudo systemctl stop filesurf-v2

# Restart service
sudo systemctl restart filesurf-v2

# View status
sudo systemctl status filesurf-v2

# Enable auto-start on boot
sudo systemctl enable filesurf-v2

# Disable auto-start
sudo systemctl disable filesurf-v2

# View logs (real-time)
sudo journalctl -u filesurf-v2 -f

# View logs (last 100 lines)
sudo journalctl -u filesurf-v2 -n 100
```

### Service Configuration Files

#### JVM Service File
**Location:** `/etc/systemd/system/filesurf-v2.service`

```ini
[Unit]
Description=FileSurf v2 Web Application (Quarkus JVM)
After=network.target

[Service]
Type=simple
User=root
Group=root
WorkingDirectory=/root/filesurf_v2

ExecStart=/usr/bin/java \
    -Xmx256m \
    -Xms128m \
    -XX:+UseSerialGC \
    -XX:MaxMetaspaceSize=128m \
    -XX:+TieredCompilation \
    -XX:TieredStopAtLevel=1 \
    -Djava.net.preferIPv4Stack=true \
    -Dquarkus.profile=prod \
    -jar target/quarkus-app/quarkus-run.jar

Restart=always
RestartSec=10

NoNewPrivileges=true
PrivateTmp=true

StandardOutput=journal
StandardError=journal
SyslogIdentifier=filesurf-v2

[Install]
WantedBy=multi-user.target
```

#### Native Service File
**Location:** `/etc/systemd/system/filesurf-v2.service`

```ini
[Unit]
Description=FileSurf v2 Web Application (Quarkus)
After=network.target

[Service]
Type=simple
User=root
Group=root
WorkingDirectory=/root/filesurf_v2
ExecStart=/root/filesurf_v2/target/filesurf-1.0.0-SNAPSHOT-runner -Dquarkus.profile=prod
Restart=always
RestartSec=10

Environment="JAVA_OPTS=-Xmx256m -Xms128m -XX:+UseSerialGC -XX:MaxMetaspaceSize=128m"

NoNewPrivileges=true
PrivateTmp=true

StandardOutput=journal
StandardError=journal
SyslogIdentifier=filesurf-v2

[Install]
WantedBy=multi-user.target
```

### After Changing Service Files
```bash
sudo systemctl daemon-reload
sudo systemctl restart filesurf-v2
```

---

## Monitoring

### Health Checks

#### Ready Check
```bash
curl http://localhost:9090/health/ready
```
Returns: `{"status":"UP"}`

#### Live Check
```bash
curl http://localhost:9090/health/live
```
Returns: `{"status":"UP"}`

### Metrics Endpoint

#### Prometheus Metrics
```bash
curl http://localhost:9090/metrics
```

**Available metrics:**
- `http_server_requests_seconds_*` - HTTP request metrics
- `jvm_memory_*` - JVM memory usage (JVM build only)
- `jvm_threads_*` - Thread counts
- `system_cpu_*` - CPU usage
- `process_*` - Process metrics
- Custom application metrics

#### Key Metrics to Monitor

```bash
# HTTP request count
curl -s http://localhost:9090/metrics | grep http_server_requests_seconds_count

# Memory usage (JVM)
curl -s http://localhost:9090/metrics | grep jvm_memory_used_bytes

# CPU usage
curl -s http://localhost:9090/metrics | grep system_cpu_usage

# Thread count
curl -s http://localhost:9090/metrics | grep jvm_threads_live
```

### Performance Monitoring

#### Check Service Resource Usage
```bash
# Memory and CPU usage
systemctl status filesurf-v2

# Detailed process info
ps aux | grep filesurf

# Memory breakdown
sudo pmap -x $(pgrep -f filesurf-v2)
```

#### Check Disk Usage
```bash
# Database size
du -h /var/lib/filesurf/data/filesurf.db

# Session files
du -sh /var/lib/filesurf/sessions/

# Log files
du -sh /var/log/filesurf/
```

#### Check Open Connections
```bash
# WebSocket connections
sudo netstat -an | grep :9090 | grep ESTABLISHED | wc -l

# File descriptors
sudo lsof -p $(pgrep -f filesurf-v2) | wc -l
```

### Alerting

#### Monitor Service Health (Cron Job)
Create `/root/monitor-filesurf.sh`:
```bash
#!/bin/bash
if ! systemctl is-active --quiet filesurf-v2; then
    echo "FileSurf v2 is down! Restarting..."
    systemctl start filesurf-v2
    echo "Service restarted at $(date)" >> /var/log/filesurf/restarts.log
fi
```

Add to crontab:
```bash
crontab -e
# Add:
*/5 * * * * /root/monitor-filesurf.sh
```

---

## Security Notes

### Production Checklist
- [ ] `cookie.secure=true` in `application-prod.properties`
- [ ] Database file permissions: `chmod 700 /var/lib/filesurf/data/`
- [ ] Log directory permissions: `chmod 755 /var/log/filesurf/`
- [ ] Service runs as non-root user (recommended)
- [ ] Firewall configured (only expose necessary ports)
- [ ] HTTPS/TLS configured (if public-facing)
- [ ] Regular backups of `/var/lib/filesurf/data/`

### User Management
```bash
# Invite new user
./scripts/invite-user.sh user@example.com

# List users
./scripts/invite-user.sh --list

# Deactivate user
./scripts/invite-user.sh --deactivate user@example.com
```

---

## Backup & Restore

### Backup Database
```bash
# Stop service (recommended)
sudo systemctl stop filesurf-v2

# Backup database
sudo cp /var/lib/filesurf/data/filesurf.db /root/backups/filesurf-$(date +%Y%m%d).db

# Or use SQLite backup command
sqlite3 /var/lib/filesurf/data/filesurf.db ".backup /root/backups/filesurf-$(date +%Y%m%d).db"

# Start service
sudo systemctl start filesurf-v2
```

### Restore Database
```bash
sudo systemctl stop filesurf-v2
sudo cp /root/backups/filesurf-20260108.db /var/lib/filesurf/data/filesurf.db
sudo systemctl start filesurf-v2
```

### Automated Backup (Cron)
```bash
crontab -e
# Add daily backup at 2 AM:
0 2 * * * systemctl stop filesurf-v2 && cp /var/lib/filesurf/data/filesurf.db /root/backups/filesurf-$(date +\%Y\%m\%d).db && systemctl start filesurf-v2
```

---

## Version Information

- **Application:** FileSurf v2 1.0.0-SNAPSHOT
- **Quarkus:** 3.16.4
- **Java:** 21
- **Database:** SQLite 3
- **Port:** 9090 (production), 8080 (development)

---

## Quick Reference

### URLs
- **Application:** http://localhost:9090/file-chat
- **Login:** http://localhost:9090/auth/login
- **Metrics:** http://localhost:9090/metrics
- **Health:** http://localhost:9090/health/ready

### Files
- **JVM Build:** `target/quarkus-app/quarkus-run.jar`
- **Native Build:** `target/filesurf-1.0.0-SNAPSHOT-runner`
- **Database:** `/var/lib/filesurf/data/filesurf.db`
- **App Log:** `/var/log/filesurf/application.log`
- **Agent Log:** `/var/log/filesurf/klawed-agents.log`
- **Service:** `/etc/systemd/system/filesurf-v2.service`

### Commands
```bash
# Build
./deployment/build-jvm.sh

# Deploy
./deployment/deploy-jvm.sh

# Service
sudo systemctl restart filesurf-v2
sudo systemctl status filesurf-v2

# Logs
sudo journalctl -u filesurf-v2 -f
tail -f /var/log/filesurf/application.log

# Health
curl http://localhost:9090/health/ready
curl http://localhost:9090/metrics
```

---

## Support

For issues or questions:
1. Check logs: `/var/log/filesurf/`
2. Check service: `systemctl status filesurf-v2`
3. Review this README
4. Check main project `KLAWED.md` file
