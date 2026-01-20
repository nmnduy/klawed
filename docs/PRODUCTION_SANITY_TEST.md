# Production Sanity Testing

This document describes the automated sanity testing infrastructure for FileSurf production.

## Overview

Production sanity tests run every 8 hours to verify the deployment is healthy. If tests fail, alerts are sent to Telegram.

## Components

### Script Location
```
/opt/filesurf-mon/bin/sanity-test-production.sh
```

### Cron Schedule
```
/etc/cron.d/filesurf-sanity-test
0 */8 * * * pi /opt/filesurf-mon/bin/sanity-test-production.sh
```

### Logs
```
/opt/filesurf-mon/logs/sanity-test.log
```

## Tests Performed

1. **Server Health** - Verifies https://filesurf.io is reachable
2. **Authentication Flow** - Tests login/logout with test user
3. **Session Management** - Creates and verifies session
4. **File-Chat HTTP** - Sends message and polls for AI response

## Prometheus Metrics

Metrics are pushed to Pushgateway (pie-01:9091):

| Metric | Type | Description |
|--------|------|-------------|
| `filesurf_sanity_test_result` | gauge | 1=pass, 0=fail |
| `filesurf_sanity_test_passed` | counter | Passed tests count |
| `filesurf_sanity_test_failed` | counter | Failed tests count |
| `filesurf_sanity_test_duration_seconds` | gauge | Test duration |
| `filesurf_sanity_test_run_timestamp` | gauge | Last run epoch |

## Alert Rules

Configured in `/opt/viettube/config/filesurf_alert_rules.yml`:

```yaml
- alert: FileSurfSanityTestFailed
  expr: filesurf_sanity_test_result{job="sanity-test"} == 0
  for: 5m
  labels:
    severity: critical

- alert: FileSurfSanityTestStale
  expr: time() - filesurf_sanity_test_run_timestamp{job="sanity-test"} > 28800
  for: 1h
  labels:
    severity: warning
```

## Telegram Alerts

Alerts flow through Alertmanager → Telegram webhook:

1. Prometheus evaluates rules → sends to Alertmanager
2. Alertmanager routes to Telegram receiver
3. Telegram webhook sends message to configured chat

## Manual Execution

```bash
# Run tests manually
ssh pie-01 /opt/filesurf-mon/bin/sanity-test-production.sh

# View logs
ssh pie-01 tail -f /opt/filesurf-mon/logs/sanity-test.log

# Check metrics
curl pie-01:9090/api/v1/query?query=filesurf_sanity_test_result
```

## Test User

The test user `test@example.com` must be active in the database. To verify:

```bash
ssh filesurf-0 ./scripts/invite-user.sh -a test@example.com
```
