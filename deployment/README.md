# FileSurf v2 Deployment Scripts

This directory contains scripts for building and deploying FileSurf v2.

## Deployment Methods

### Local Build + Remote Sync (Recommended)

Build the application on your local machine and sync to the server using rsync. This is faster and more reliable when you have a proper development environment locally.

#### JVM Mode (Faster Build)
```bash
./deployment/deploy-rsync.sh
```

This script will:
1. Build CSS assets locally (`npm run build`)
2. Build the Java application with Maven
3. Rsync the following to filesurf-0:/root/filesurf_v2:
   - `target/quarkus-app/` (JAR and dependencies)
   - `deployment/` (scripts and service files)
   - `src/main/resources/` (templates, CSS, JS, built assets)
   - `pom.xml` and `package.json` (for reference)
4. Run `deploy-jvm.sh` on the remote server to install/restart the service

#### Native Mode (Smaller Memory Footprint)
```bash
./deployment/deploy-rsync-native.sh
```

This script will:
1. Build CSS assets locally (`npm run build`)
2. Build native executable with GraalVM (takes 5-10 minutes)
3. Rsync the native executable and resources to filesurf-0
4. Run `deploy.sh` on the remote server to install/restart the service

**Requirements:**
- GraalVM with native-image installed locally
- Same CPU architecture as target server (x86_64)

### Remote Build (Alternative)

Build directly on the server. Useful if you don't have the proper build environment locally.

#### JVM Mode
```bash
./deployment/build-jvm.sh    # On server
./deployment/deploy-jvm.sh   # On server
```

#### Native Mode
```bash
./deployment/build-native.sh # On server
./deployment/deploy.sh       # On server
```

## Directory Sync Details

The rsync scripts only sync the **necessary directories** to minimize transfer size:

### Always Synced
- `target/quarkus-app/` or `target/*-runner` - Built application
- `deployment/` - Deployment scripts (excludes *.md files)
- `src/main/resources/` - Templates, CSS, JS, and built assets
- `pom.xml`, `package.json` - Project metadata

### Never Synced
- `node_modules/` - Not needed on server
- `data/`, `logs/` - Server-specific data
- `.git/` - Version control data
- `target/classes/`, `target/maven-status/` - Build intermediates

## Remote Server Configuration

- **Host:** filesurf-0
- **Path:** /root/filesurf_v2
- **Service:** filesurf-v2
- **Port:** 9090

## Prerequisites

### Local Machine
- Node.js and npm (for CSS builds)
- Maven (for Java builds)
- GraalVM with native-image (for native builds)
- SSH access to filesurf-0

### Remote Server
- Java 21 runtime (for JVM mode)
- No Java needed (for native mode)
- systemd
- Required directories created by deployment scripts

## Troubleshooting

### Rsync Connection Issues
```bash
# Test SSH connection
ssh filesurf-0 echo "Connected"

# Test rsync with dry-run
rsync -avzn target/quarkus-app/ filesurf-0:/root/filesurf_v2/target/quarkus-app/
```

### Build Failures
```bash
# Clean and rebuild
mvn clean
npm run build
mvn package -DskipTests
```

### Service Issues
```bash
# Check service status
ssh filesurf-0 'systemctl status filesurf-v2'

# View logs
ssh filesurf-0 'journalctl -u filesurf-v2 -f'

# Restart service
ssh filesurf-0 'systemctl restart filesurf-v2'
```
