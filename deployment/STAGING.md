# Staging Environment Setup

## Overview
FileSurf v2 staging environment runs on **pie-01** on port **9090**, accessible via **staging.filesurf.io** through Cloudflare Tunnel.

## Architecture
- **Host**: pie-01 (Raspberry Pi or similar)
- **Port**: 9090 (same as production, but isolated)
- **Domain**: staging.filesurf.io
- **Access**: Via Cloudflare Tunnel (configured separately)
- **Profile**: `staging` (separate from `prod` profile)

## Directory Structure

### Application Files
- **App Root**: `/root/filesurf_v2_staging/`
- **JAR Location**: `/root/filesurf_v2_staging/target/quarkus-app/quarkus-run.jar`

### Data Directories
All data is isolated from production:
- **Database**: `/var/lib/filesurf-staging/data/filesurf.db`
- **Persistent Storage**: `/var/lib/filesurf-staging/persistent/`
- **Demo Videos**: `/var/lib/filesurf-staging/demos/`
- **Klawed Messages**: `/var/lib/filesurf-staging/data/klawed-messages/`
- **Chat Messages**: `/var/lib/filesurf-staging/data/chat-messages/`
- **Sessions DB**: `/var/lib/filesurf-staging/data/sessions.db`
- **Feedback DB**: `/var/lib/filesurf-staging/data/feedback.db`
- **Blog DB**: `/var/lib/filesurf-staging/data/blog.db`
- **Container Tracking**: `/var/lib/filesurf-staging/data/containers.db`

### Log Files
- **Application Logs**: `/var/log/filesurf-staging/application.log`
- **Klawed Logs**: `/var/log/filesurf-staging/klawed-agents.log`
- **Service Logs**: `journalctl -u filesurf-v2-staging`

### Configuration Files
- **Environment**: `/etc/filesurf-staging/.env` (main app config)
- **Klawed Env**: `/etc/filesurf-staging/klawed.env` (klawed container config)
- **Service File**: `/etc/systemd/system/filesurf-v2-staging.service`

## Deployment

### First-Time Setup
1. **Deploy the application**:
   ```bash
   ./deployment/deploy-staging.sh
   ```

2. **Create environment files on pie-01**:
   ```bash
   # SSH into pie-01
   ssh pie-01
   
   # Create main environment file
   sudo mkdir -p /etc/filesurf-staging
   sudo nano /etc/filesurf-staging/.env
   ```
   
   Add necessary environment variables:
   ```bash
   # Example .env content
   OPENAI_API_KEY=your_openai_key_here
   STRIPE_SECRET_KEY=sk_test_...
   STRIPE_PUBLIC_KEY=pk_test_...
   STRIPE_WEBHOOK_SECRET=whsec_...
   ```

3. **Create klawed environment file**:
   ```bash
   sudo nano /etc/filesurf-staging/klawed.env
   ```
   
   Add klawed-specific variables:
   ```bash
   # Example klawed.env content
   OPENAI_API_KEY=your_openai_key_here
   AWS_ACCESS_KEY_ID=your_aws_key
   AWS_SECRET_ACCESS_KEY=your_aws_secret
   AWS_DEFAULT_REGION=us-east-1
   ```

4. **Configure Cloudflare Tunnel**:
   - Point `staging.filesurf.io` to `http://pie-01:9090`
   - Ensure SSL/TLS is enabled (staging uses secure cookies)

5. **Start the service**:
   ```bash
   sudo systemctl start filesurf-v2-staging
   sudo systemctl enable filesurf-v2-staging
   ```

### Subsequent Deployments
Just run the deployment script:
```bash
./deployment/deploy-staging.sh
```

The script will:
1. Tag current commit as `staging`
2. Build CSS assets
3. Build Java application
4. Run local verification test
5. Sync files to pie-01
6. Create/verify directories
7. Restart the service

## Service Management

### Check Service Status
```bash
ssh pie-01 'systemctl status filesurf-v2-staging'
```

### View Logs
```bash
# Live tail
ssh pie-01 'journalctl -u filesurf-v2-staging -f'

# Last 100 lines
ssh pie-01 'journalctl -u filesurf-v2-staging -n 100'

# Application log file
ssh pie-01 'tail -f /var/log/filesurf-staging/application.log'

# Klawed agent logs
ssh pie-01 'tail -f /var/log/filesurf-staging/klawed-agents.log'
```

### Restart Service
```bash
ssh pie-01 'systemctl restart filesurf-v2-staging'
```

### Stop Service
```bash
ssh pie-01 'systemctl stop filesurf-v2-staging'
```

## Configuration Differences

### Staging Profile (`application.properties`)
The staging profile uses:
- Port: 9090
- Secure cookies: `true` (requires HTTPS via Cloudflare)
- Database path: `/var/lib/filesurf-staging/data/filesurf.db`
- Persistent storage: `/var/lib/filesurf-staging/persistent/`
- Podman sandbox: Enabled
- Base URL: `https://staging.filesurf.io`

### Production vs Staging
| Aspect | Production (filesurf-0) | Staging (pie-01) |
|--------|------------------------|------------------|
| Host | filesurf-0 | pie-01 |
| Port | 9090 | 9090 |
| Domain | filesurf.io | staging.filesurf.io |
| Profile | `prod` | `staging` |
| Data Path | `/var/lib/filesurf/` | `/var/lib/filesurf-staging/` |
| Log Path | `/var/log/filesurf/` | `/var/log/filesurf-staging/` |
| Service Name | `filesurf-v2` | `filesurf-v2-staging` |
| Git Tag | `production` | `staging` |

## Testing Staging

### Access the Application
1. Open browser to https://staging.filesurf.io
2. Should see FileSurf login page
3. Test authentication with invited user

### Health Check
```bash
curl https://staging.filesurf.io/q/health/ready
```

### Metrics (Tailscale only)
```bash
# From within Tailscale network
curl http://pie-01:9090/metrics
```

## Rollback

If staging deployment fails, you can rollback:

```bash
# SSH to pie-01
ssh pie-01

# Check current git tags
cd /root/filesurf_v2_staging
git tag -l

# Checkout previous version
git checkout <previous-tag>

# Restart service
sudo systemctl restart filesurf-v2-staging
```

## Cloudflare Tunnel Configuration

### Example cloudflared config.yml
```yaml
tunnel: your-tunnel-id
credentials-file: /path/to/credentials.json

ingress:
  # Staging subdomain
  - hostname: staging.filesurf.io
    service: http://pie-01:9090
    originRequest:
      noTLSVerify: false
  
  # Production (example)
  - hostname: filesurf.io
    service: http://filesurf-0:9090
  
  # Catch-all
  - service: http_status:404
```

## Monitoring

### Key Metrics to Monitor
- Service uptime: `systemctl is-active filesurf-v2-staging`
- Memory usage: `ps aux | grep filesurf-v2-staging`
- Disk usage: `df -h /var/lib/filesurf-staging`
- Log file sizes: `du -sh /var/log/filesurf-staging/*`

### Alerts to Set Up
- Service down/restart events
- High memory usage (>512MB)
- Disk space low (<1GB free)
- High error rate in logs

## Troubleshooting

### Service Won't Start
1. Check logs: `journalctl -u filesurf-v2-staging -n 50`
2. Verify Java is installed: `java -version`
3. Check JAR exists: `ls -lh /root/filesurf_v2_staging/target/quarkus-app/quarkus-run.jar`
4. Verify directories exist: `ls -ld /var/lib/filesurf-staging`

### Port Already in Use
```bash
# Check what's using port 9090
ssh pie-01 'lsof -i :9090'

# If it's another process, stop it or change staging port
```

### Database Lock Issues
```bash
# Check for stale processes
ssh pie-01 'fuser /var/lib/filesurf-staging/data/filesurf.db'

# If needed, stop service and clear locks
ssh pie-01 'systemctl stop filesurf-v2-staging'
ssh pie-01 'rm -f /var/lib/filesurf-staging/data/filesurf.db-shm'
ssh pie-01 'rm -f /var/lib/filesurf-staging/data/filesurf.db-wal'
ssh pie-01 'systemctl start filesurf-v2-staging'
```

### Container Issues
```bash
# List running containers on pie-01
ssh pie-01 'podman ps'

# Stop all klawed containers
ssh pie-01 'podman stop $(podman ps -q --filter label=app=filesurf-staging)'

# Clean up stopped containers
ssh pie-01 'podman container prune -f'
```

## Security Notes

1. **Firewall**: Port 9090 should be blocked from external access (only accessible via Cloudflare Tunnel)
2. **Secure Cookies**: Staging uses HTTPS and secure cookies like production
3. **Isolated Data**: Staging data is completely separate from production
4. **Environment Files**: Keep API keys and secrets secure in `/etc/filesurf-staging/`
5. **Podman Sandbox**: Klawed agents run in isolated containers with resource limits

## Maintenance

### Database Backups
```bash
# Backup staging database
ssh pie-01 'sqlite3 /var/lib/filesurf-staging/data/filesurf.db .dump > /root/staging-backup.sql'

# Copy backup locally
scp pie-01:/root/staging-backup.sql ./
```

### Log Rotation
Logs are automatically rotated:
- Application logs: 10MB max, 5 backups
- Klawed logs: 50MB max, 10 backups
- Journal logs: Managed by systemd

### Cleanup Old Files
```bash
# Remove old klawed message DBs (>60 days)
ssh pie-01 'find /var/lib/filesurf-staging/data/klawed-messages -name "*.db*" -mtime +60 -delete'

# Remove old chat message DBs (>60 days)
ssh pie-01 'find /var/lib/filesurf-staging/data/chat-messages -name "*.db*" -mtime +60 -delete'
```

## Differences from Production Deployment

1. **No Tag Rotation**: Staging uses simple `staging` tag, no rollback tags
2. **Different Host**: Deploys to pie-01 instead of filesurf-0
3. **Different Paths**: Uses `/root/filesurf_v2_staging` instead of `/root/filesurf_v2`
4. **Profile**: Uses `-Dquarkus.profile=staging` instead of `prod`
5. **Service Name**: `filesurf-v2-staging` instead of `filesurf-v2`
6. **Data Isolation**: All data paths use `-staging` suffix

## Future Improvements

1. **Automated Testing**: Add smoke tests after deployment
2. **Blue-Green Deployment**: Support multiple staging slots
3. **Database Migrations**: Test schema changes before production
4. **Performance Testing**: Load testing on staging before prod
5. **Monitoring Dashboard**: Grafana/Prometheus for staging metrics
