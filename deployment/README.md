# FileSurf v2 Deployment Scripts

This directory contains scripts for building and deploying FileSurf v2.

## Deployment Methods

### Production Deployment

Build the application on your local machine and deploy to **filesurf-0** (production):

```bash
./deployment/deploy-rsync.sh
```

This script will:
1. Rotate rollback tags (production-rollback-n1, n2, n3) and tag current commit as 'production'
2. Build CSS assets locally (`npm run build`)
3. Build the Java application with Maven
4. Run locally for 10 seconds to verify the build works
5. Rsync the following to filesurf-0:/root/filesurf_v2:
   - `target/quarkus-app/` (JAR and dependencies)
   - `deployment/` (scripts and service files)
   - `src/main/resources/` (templates, CSS, JS, built assets)
   - `pom.xml` and `package.json` (for reference)
6. Run `deploy.sh` on the remote server to install/restart the service

### Staging Deployment

Build and deploy to **pie-01** (staging environment):

```bash
./deployment/deploy-staging.sh
```

This script will:
1. Tag current commit as 'staging'
2. Build CSS assets and Java application
3. Run local verification test
4. Rsync files to pie-01:/root/filesurf_v2_staging
5. Install systemd service and restart

See [STAGING.md](./STAGING.md) for complete staging environment documentation.

### Remote Build (Alternative)

Build directly on the server. Useful if you don't have the proper build environment locally.

```bash
# On server
./deployment/build.sh    # Build the application
./deployment/deploy.sh   # Deploy and restart service
```

## Directory Sync Details

The rsync script only syncs the **necessary directories** to minimize transfer size:

### Always Synced
- `target/quarkus-app/` - Built application (JAR and dependencies)
- `deployment/` - Deployment scripts (excludes *.md files)
- `src/main/resources/` - Templates, CSS, JS, and built assets
- `pom.xml`, `package.json` - Project metadata

### Never Synced
- `node_modules/` - Not needed on server
- `data/`, `logs/` - Server-specific data
- `.git/` - Version control data
- `target/classes/`, `target/maven-status/` - Build intermediates

## Production Rollback System

The deployment script maintains a rollback tag history:

- `production` - Current production version
- `production-rollback-n1` - Previous production (1 version back)
- `production-rollback-n2` - 2 versions back
- `production-rollback-n3` - 3 versions back

To rollback to a previous version:

```bash
# Check out the rollback tag
git checkout production-rollback-n1

# Deploy (will skip tag rotation since you're on a rollback tag)
./deployment/deploy-rsync.sh

# After verifying, make it the new production
git tag -f production HEAD
git push -f origin production
```

## Remote Server Configuration

### Production (filesurf-0)
- **Host:** filesurf-0
- **Path:** /root/filesurf_v2
- **Service:** filesurf-v2
- **Port:** 9090
- **Domain:** filesurf.io
- **Data Directory:** /var/lib/filesurf
- **Log Directory:** /var/log/filesurf
- **Profile:** `prod`

### Staging (pie-01)
- **Host:** pie-01
- **Path:** /root/filesurf_v2_staging
- **Service:** filesurf-v2-staging
- **Port:** 9090
- **Domain:** staging.filesurf.io
- **Data Directory:** /var/lib/filesurf-staging
- **Log Directory:** /var/log/filesurf-staging
- **Profile:** `staging`

See [STAGING.md](./STAGING.md) for detailed staging configuration.

## Prerequisites

### Local Machine
- Node.js and npm (for CSS builds)
- Maven (for Java builds)
- Java 21 (for local testing)
- SSH access to filesurf-0

### Remote Server
- Java 21 runtime
- systemd
- Podman (for sandboxed klawed agents)
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

### Local Testing
```bash
# Test the build locally before deploying
cd target/quarkus-app
java -Dquarkus.profile=prod -jar quarkus-run.jar

# Access at http://localhost:9090
```

## Environment Configuration

### Production Overrides
Production-specific configuration is set via `%prod.` prefixes in `application.properties`:

- Database: `/var/lib/filesurf/data/filesurf.db`
- Sessions DB: `/var/lib/filesurf/data/sessions.db`
- Container Tracking DB: `/var/lib/filesurf/data/containers.db`
- Feedback DB: `/var/lib/filesurf/data/feedback.db`
- Klawed Messages: `/var/lib/filesurf/data/klawed-messages/`
- Persistent Storage: `/var/lib/filesurf/persistent/`
- Demo Videos: `/var/lib/filesurf/demos/`
- Application Log: `/var/log/filesurf/application.log`
- Klawed Agent Log: `/var/log/filesurf/klawed-agents.log`

### Environment File
Sensitive configuration (API keys, etc.) is stored in `/etc/filesurf/.env` on the server and loaded by systemd.
