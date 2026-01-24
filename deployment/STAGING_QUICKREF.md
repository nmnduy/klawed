# Staging Deployment Quick Reference

## Deploy to Staging
```bash
./deployment/deploy-staging.sh
```

## First-Time Setup on pie-01
```bash
# 1. Create environment files
ssh pie-01
sudo mkdir -p /etc/filesurf-staging
sudo nano /etc/filesurf-staging/.env
sudo nano /etc/filesurf-staging/klawed.env

# 2. Deploy will handle the rest
exit
./deployment/deploy-staging.sh
```

## Check Staging Status
```bash
# Service status
ssh pie-01 'systemctl status filesurf-v2-staging'

# Live logs
ssh pie-01 'journalctl -u filesurf-v2-staging -f'

# Application log
ssh pie-01 'tail -f /var/log/filesurf-staging/application.log'
```

## Restart Staging
```bash
ssh pie-01 'systemctl restart filesurf-v2-staging'
```

## Access Staging
- **URL**: https://staging.filesurf.io (via Cloudflare Tunnel)
- **Port**: 9090 on pie-01
- **Health**: https://staging.filesurf.io/q/health/ready

## Cloudflare Tunnel
Point `staging.filesurf.io` to `http://pie-01:9090` in your cloudflared config.

## Key Differences from Production
- Host: pie-01 (not filesurf-0)
- Path: `/root/filesurf_v2_staging`
- Service: `filesurf-v2-staging`
- Data: `/var/lib/filesurf-staging/`
- Logs: `/var/log/filesurf-staging/`
- Profile: `staging` (not `prod`)

## See Also
- [STAGING.md](./STAGING.md) - Complete staging documentation
- [README.md](./README.md) - General deployment guide
