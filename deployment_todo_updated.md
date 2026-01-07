## Objective

We want to deploy this new filesurf v2 (Quarkus/Java) implementation to a VPS, which is currently running an older Go-based version of this service.

## Deployment Host

- **VPS**: filesurf-0
- **OS**: Ubuntu (not Debian 12 as originally noted - shows Ubuntu in kernel)
- **Java**: OpenJDK 21.0.9 (already installed) ✓
- **Maven**: Not installed (needs to be installed)
- **Resources**: 961MB RAM, 25GB disk (8GB used, 15GB available)
- **SSH config**:
  ```
  Host filesurf-0
      Hostname 100.65.242.128
      User root
  ```

## Current Production Setup (Go-based v1)

- **Application**: Go-based filesurf-web binary
- **Location**: `/root/filesurf/web/`
- **Port**: 9090
- **Running**: Yes (PID 2462395, started in 2025, using screen session)
- **Service file**: No systemd service currently active
- **Reverse proxy**: Nginx installed but not running
- **Database**: SQLite at `/root/filesurf/filesurf_db/`
- **Environment**: Uses `.envrc` with:
  - GROQ_API_KEY
  - SERVER_URL=https://filesurf.io
  - COOKIE_SECRET_KEY
  - MAIL_SMTP_PASSWORD
  - ENV=production

## New Application (Quarkus/Java v2)

- **Technology**: Quarkus 3.16.4 (Java 21)
- **Default Port**: 8080
- **Database**: SQLite at `data/filesurf.db`
- **Build**: Requires Maven
- **Metrics**: Prometheus endpoint at `/metrics`
- **Required Environment Variables**:
  - Cookie configuration (cookie.secure)
  - OpenAI API settings (already in application.properties)
  - File paths (PERSIST_ROOT, SESSION_BASE_DIR)

## Prometheus/Grafana Host

- **Host**: pie-01 (192.168.1.160)
- **Note**: ⚠️ This host is currently unreachable (local network, might need VPN or be offline)
- **SSH config**:
  ```
  Host pie-01
      Hostname 192.168.1.160
      User pi
  ```

---

## Deployment Tasks

### Phase 1: Preparation
- [ ] **Install Maven on filesurf-0**
  - Install Maven 3.x for building Quarkus application
  - Verify Maven can run with Java 21

- [ ] **Setup git repo on filesurf-0**
  - Create bare git repo or configure push target
  - Allow pushing filesurf_v2 to production VPS
  - Set up deployment key if needed

- [ ] **Create production application.properties**
  - Override development settings (ports, paths, logging)
  - Set `quarkus.http.port=8080` or use environment variable
  - Configure production database path
  - Set `cookie.secure=true` for HTTPS
  - Configure session and persist directories

### Phase 2: Service Configuration
- [ ] **Create systemd service file for filesurf v2**
  - Base it on the existing template at `/root/filesurf/web/deployment/filesurf.service`
  - Update for Java/Quarkus application:
    - ExecStart should run the Quarkus JAR
    - Set appropriate Working Directory
    - Configure environment variables
    - Set User/Group (filesurf user or root?)
  - Ensure proper permissions and security hardening
  - Configure log output (journal integration)

- [ ] **Create deployment script**
  - Script to build the application on the server
  - Copy necessary files (JAR, templates, static assets, data directory)
  - Set proper permissions
  - Manage database migrations if needed

### Phase 3: Monitoring Setup
- [ ] **Update Prometheus config on pie-01** (when accessible)
  - Add scrape target for filesurf-0:8080/metrics
  - Configure appropriate scrape interval
  - Test connectivity from pie-01 to filesurf-0
  - Restart Prometheus service to load new config
  - **Alternative**: If pie-01 is not accessible, consider:
    - Running Prometheus on filesurf-0 itself
    - Using a different monitoring host
    - Documenting this for later when pie-01 is back online

### Phase 4: Migration and Cutover
- [ ] **Understand the old Go application stack**
  - Document how it's currently started (screen session found)
  - Identify any dependencies or data that needs migrating
  - Check if SQLite database schema is compatible
  - Backup current database at `/root/filesurf/filesurf_db/`

- [ ] **Stop the old service**
  - Kill the screen session running filesurf-web
  - Verify port 9090 is freed (or use different port for v2)
  - Archive or backup the old application

- [ ] **Start the new service**
  - Enable and start filesurf v2 systemd service
  - Configure nginx reverse proxy if needed (currently inactive)
  - Update DNS/firewall rules if port changed
  - Test access to https://filesurf.io

- [ ] **Verify metrics endpoint**
  - Check that `/metrics` endpoint is accessible
  - Verify Prometheus can scrape metrics (once monitoring is set up)

### Phase 5: Post-Deployment
- [ ] **Monitor logs and metrics**
  - Check application logs at configured location
  - Monitor system resources (RAM usage - only 961MB available!)
  - Watch for any errors or performance issues

- [ ] **Setup reverse proxy (nginx)**
  - Enable and configure nginx service
  - Proxy requests from port 80/443 to 8080
  - Configure SSL/TLS certificates
  - Set appropriate headers and timeouts

- [ ] **Data migration (if needed)**
  - Migrate user data from old database to new
  - Verify authentication works with new system
  - Test file upload/download functionality

---

## Additional Considerations

### Resource Constraints
⚠️ **WARNING**: The VPS only has 961MB RAM with 493MB swap already in use. Quarkus apps can be memory-intensive. Monitor carefully and consider:
- Building with native-image for lower memory footprint
- Adjusting JVM heap settings
- Using Quarkus in JVM mode with minimal heap

### Port Strategy
**Options:**
1. Use port 8080 for v2 and update nginx proxy configuration
2. Use port 9090 (same as old app) after stopping it
3. Run both temporarily on different ports for testing

### Environment Variables Needed
From old app that might be needed:
- `GROQ_API_KEY` (if using Groq for AI)
- `COOKIE_SECRET_KEY` (for session management)
- `SERVER_URL` (for generating links)
- `MAIL_SMTP_PASSWORD` (if email features are used)
- `ENV=production`

### Database Compatibility
- Old app uses `/root/filesurf/filesurf_db/`
- New app uses `data/filesurf.db`
- Need to verify schema compatibility or plan migration

### Nginx Configuration
- Nginx is installed but currently stopped
- Will need configuration to proxy to Quarkus app
- SSL/TLS certificate setup if not using Cloudflare tunnel

---

## Priority/Critical Path

1. **Install Maven** (blocks all builds)
2. **Setup git repo** (enables code deployment)
3. **Create service file** (needed for production deployment)
4. **Document old app** (understand what we're replacing)
5. **Stop old service** (free resources)
6. **Deploy and start new service** (cutover)
7. **Configure monitoring** (when pie-01 is accessible)
8. **Setup nginx** (production-ready web access)

---

## Questions to Resolve

1. Should we migrate the existing SQLite database or start fresh?
2. What is the correct domain/DNS setup? (filesurf.io currently points where?)
3. Is Cloudflare tunnel being used, or do we need nginx SSL?
4. When will pie-01 be accessible for Prometheus setup?
5. Should we use the existing `filesurf` user or create a new one?
6. Do we want to build on the server or build locally and deploy JAR?
7. What is the rollback plan if deployment fails?
