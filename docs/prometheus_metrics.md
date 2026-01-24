# FileSurf v2 - Prometheus Metrics

This document describes all custom metrics exposed by the FileSurf v2 application through the Prometheus endpoint (`/metrics`).

## Application Metrics

### Session Metrics
- **filesurf_chat_sessions_started**: [application] Total number of chat sessions started (WebSocket connections)
- **filesurf_chat_sessions_created**: [application] Total number of NEW chat sessions created
- **filesurf_chat_sessions_resumed**: [application] Total number of EXISTING chat sessions resumed
- **filesurf_active_chat_sessions**: [application] Number of currently active chat sessions

### Container Lifecycle Metrics
- **filesurf_containers_started**: [application] Total number of containers started (new or after stop)
- **filesurf_containers_reused**: [application] Total number of containers reused (already running, no action taken)
- **filesurf_containers_stopped**: [application] Total number of containers stopped gracefully (idle/inactive)
- **filesurf_containers_killed**: [application] Total number of containers forcefully killed
- **filesurf_container_start_failures**: [application] Total number of container start failures
- **filesurf_klawed_containers_active**: [application] Number of active klawed Podman containers (gauge)
- **filesurf_conversation_seeded**: [application] Total number of times conversation history was seeded to klawed containers

### Message Metrics
- **filesurf_chat_messages_sent**: [application] Total number of chat messages sent

### WebSocket Metrics
- **filesurf_active_websocket_connections**: [application] Number of currently active WebSocket connections

### File Operation Metrics
- **filesurf_file_operations**: [application] Total number of file operations performed

### API Metrics
- **filesurf_api_calls**: [application] Total number of API calls made

### Error Metrics
- **filesurf_errors**: [application] [type] Total number of errors encountered
  - **type**: Error type (e.g., "application", "database_save", "file_attribute_read", "directory_listing", "websocket_serialization")

### Disk Space Metrics
- **filesurf_disk_free_bytes**: [application] [directory] Free disk space available (bytes)
  - **directory**: Directory being monitored ("data" or "logs")
- **filesurf_disk_total_bytes**: [application] [directory] Total disk space (bytes)
  - **directory**: Directory being monitored ("data" or "logs")

## Performance Timing Metrics

### Chat Response Timing
- **filesurf_chat_response_time_seconds**: [application] Time taken to process chat responses
  - Provides quantiles: 0.5 (median), 0.95, 0.99
  - Also available as `_count` and `_sum` for calculating averages
- **filesurf_chat_response_time_seconds_max**: [application] Maximum time taken to process chat responses

### File Operation Timing
- **filesurf_file_operation_time_seconds**: [application] Time taken to perform file operations
  - Provides quantiles: 0.5 (median), 0.95, 0.99
  - Also available as `_count` and `_sum` for calculating averages
- **filesurf_file_operation_time_seconds_max**: [application] Maximum time taken to perform file operations

### API Call Timing
- **filesurf_api_call_time_seconds**: [application] Time taken to make API calls
  - Provides quantiles: 0.5 (median), 0.95, 0.99
  - Also available as `_count` and `_sum` for calculating averages
- **filesurf_api_call_time_seconds_max**: [application] Maximum time taken to make API calls

## Standard Micrometer Metrics

In addition to custom metrics, the following standard metrics are automatically exposed:

### JVM Metrics
- **jvm_memory_used_bytes**: Memory usage by area (heap/non-heap) and pool
- **jvm_memory_max_bytes**: Maximum memory available
- **jvm_classes_loaded_classes**: Number of loaded classes
- **jvm_threads_live_threads**: Current number of live threads
- **jvm_gc_pause_seconds**: GC pause times
- **jvm_gc_memory_promoted_bytes**: Memory promoted during GC
- **jvm_buffer_total_capacity_bytes**: Buffer pool capacities
- **jvm_info**: JVM version information

### System Metrics
- **system_cpu_usage**: System CPU usage percentage
- **system_load_average_1m**: 1-minute load average
- **process_cpu_time_ns**: CPU time used by the JVM process

### HTTP Server Metrics
- **http_server_requests_seconds**: HTTP request processing time
- **http_server_bytes_read**: Bytes received by the server
- **http_server_bytes_written**: Bytes sent by the server

### Netty Metrics
- **netty_allocator_pooled_cache_size**: Netty allocator cache sizes
- **netty_allocator_pooled_chunk_size**: Netty allocator chunk sizes

### Worker Pool Metrics
- **worker_pool_queue_size**: Pending elements in worker pools
- **worker_pool_active**: Active worker threads
- **worker_pool_usage_seconds**: Time spent using worker pool resources

## Metric Types

- **counter**: Monotonically increasing counter (resets on restart)
- **gauge**: Current value that can go up or down
- **summary**: Records observations and provides quantile calculations
- **histogram**: Similar to summary but with configurable buckets

## Usage Examples

### Monitoring Active Sessions
```promql
# Current active chat sessions
filesurf_active_chat_sessions{application="filesurf"}

# Rate of new session starts per minute
rate(filesurf_chat_sessions_started_total[1m])

# Rate of NEW sessions created per minute
rate(filesurf_chat_sessions_created_total[1m])

# Rate of EXISTING sessions resumed per minute
rate(filesurf_chat_sessions_resumed_total[1m])

# Ratio of resumed to created sessions (higher = more returning users)
rate(filesurf_chat_sessions_resumed_total[5m]) / rate(filesurf_chat_sessions_created_total[5m])
```

### Monitoring Container Lifecycle
```promql
# Current number of active klawed containers
filesurf_klawed_containers_active{application="filesurf"}

# Rate of containers started per minute
rate(filesurf_containers_started_total[1m])

# Rate of containers reused (already running)
rate(filesurf_containers_reused_total[1m])

# Rate of containers stopped per minute
rate(filesurf_containers_stopped_total[1m])

# Container start failure rate
rate(filesurf_container_start_failures_total[5m])

# Container reuse efficiency (higher = better, containers staying alive)
rate(filesurf_containers_reused_total[5m]) / (rate(filesurf_containers_started_total[5m]) + rate(filesurf_containers_reused_total[5m]))

# Rate of conversation seeding events per minute
rate(filesurf_conversation_seeded_total[1m])

# Total conversation seeding events
filesurf_conversation_seeded_total{application="filesurf"}
```

### Error Rate Monitoring
```promql
# Error rate per minute
rate(filesurf_errors_total[1m])

# Error rate by type
rate(filesurf_errors_total{type="application"}[5m])
```

### Performance Monitoring
```promql
# 95th percentile response time for chat
filesurf_chat_response_time_seconds{application="filesurf", quantile="0.95"}

# Average file operation time
filesurf_file_operation_time_seconds_sum{application="filesurf"} / filesurf_file_operation_time_seconds_count{application="filesurf"}
```

### System Health
```promql
# Memory usage percentage
jvm_memory_used_bytes{area="heap"} / jvm_memory_max_bytes{area="heap"} * 100

# GC pause time rate
rate(jvm_gc_pause_seconds_sum[5m])

# Disk usage percentage (data directory)
100 * (1 - (filesurf_disk_free_bytes{directory="data"} / filesurf_disk_total_bytes{directory="data"}))

# Free disk space in GB
filesurf_disk_free_bytes{directory="data"} / 1024 / 1024 / 1024
```

## Alerting Examples

```yaml
# High error rate
- alert: HighErrorRate
  expr: rate(filesurf_errors_total[5m]) > 0.1
  for: 2m
  labels:
    severity: warning

# No active sessions for extended period
- alert: NoActiveSessions
  expr: filesurf_active_chat_sessions == 0
  for: 1h
  labels:
    severity: info

# High container start failure rate
- alert: HighContainerStartFailures
  expr: rate(filesurf_container_start_failures_total[5m]) > 0.1
  for: 5m
  labels:
    severity: critical
  annotations:
    summary: "High rate of container start failures"
    description: "Container start failures are happening at {{ $value }} per second"

# Container leak detection (more containers than sessions)
- alert: ContainerLeak
  expr: filesurf_klawed_containers_active > filesurf_active_chat_sessions + 5
  for: 10m
  labels:
    severity: warning
  annotations:
    summary: "Possible container leak detected"
    description: "{{ $value }} containers running but only {{ query \"filesurf_active_chat_sessions\" }} active sessions"

# Low container reuse rate (containers stopping/starting too frequently)
- alert: LowContainerReuseRate
  expr: rate(filesurf_containers_reused_total[10m]) / (rate(filesurf_containers_started_total[10m]) + rate(filesurf_containers_reused_total[10m])) < 0.3
  for: 15m
  labels:
    severity: info
  annotations:
    summary: "Low container reuse rate"
    description: "Only {{ $value | humanizePercentage }} of container accesses are reusing existing containers"

# High response time
- alert: HighChatResponseTime
  expr: filesurf_chat_response_time_seconds{quantile="0.95"} > 5
  for: 5m
  labels:
    severity: warning

# Memory pressure
- alert: HighMemoryUsage
  expr: jvm_memory_used_bytes{area="heap"} / jvm_memory_max_bytes{area="heap"} > 0.8
  for: 5m
  labels:
    severity: critical

# Low disk space warning (less than 10% free)
- alert: DiskSpaceLow
  expr: (filesurf_disk_free_bytes / filesurf_disk_total_bytes) < 0.1
  for: 5m
  labels:
    severity: warning
  annotations:
    summary: "FileSurf disk space low on {{ $labels.directory }} directory"
    description: "Only {{ $value | humanizePercentage }} of disk space remaining"

# Critical disk space (less than 5% free)
- alert: DiskSpaceCritical
  expr: (filesurf_disk_free_bytes / filesurf_disk_total_bytes) < 0.05
  for: 5m
  labels:
    severity: critical
  annotations:
    summary: "FileSurf disk space critical on {{ $labels.directory }} directory"
    description: "Only {{ $value | humanizePercentage }} of disk space remaining - immediate action required"
```

## Notes

1. All custom metrics include the `application="filesurf"` tag for identification
2. Error metrics include a `type` tag for categorizing different error sources
3. Timing metrics (summary type) provide pre-calculated quantiles for common percentiles
4. Counters reset when the application restarts
5. Gauges represent current state and can fluctuate

### Session Tracking

The session metrics now distinguish between **new** and **resumed** sessions:

- **filesurf_chat_sessions_started**: Incremented every time a WebSocket connection is established (both new and resumed)
- **filesurf_chat_sessions_created**: Incremented only when a brand NEW session is created (first time)
- **filesurf_chat_sessions_resumed**: Incremented when an EXISTING session reconnects (user returning)

This allows you to track user return rates and session patterns:
```promql
# What percentage of connections are from returning users?
rate(filesurf_chat_sessions_resumed_total[1h]) / rate(filesurf_chat_sessions_started_total[1h]) * 100
```

### Container Lifecycle Tracking

The container metrics provide visibility into the full lifecycle of klawed containers:

- **filesurf_containers_started**: Container was started (either brand new or after being stopped)
- **filesurf_containers_reused**: Container was already running when session connected (efficient!)
- **filesurf_containers_stopped**: Container was gracefully stopped (due to inactivity/idle timeout)
- **filesurf_containers_killed**: Container was forcefully killed (reserved for future use)
- **filesurf_container_start_failures**: Container failed to start (configuration or resource issue)

**Container Reuse Efficiency**: A high reuse rate indicates containers are staying alive and being reused efficiently. Low reuse means containers are being stopped/started frequently, which can impact performance.

```promql
# Container reuse efficiency (target: >50%)
rate(filesurf_containers_reused_total[10m]) / 
  (rate(filesurf_containers_started_total[10m]) + rate(filesurf_containers_reused_total[10m]))
```

**Container Leak Detection**: Compare `filesurf_klawed_containers_active` (gauge) with `filesurf_active_chat_sessions` (gauge). If containers significantly exceed active sessions, you may have a leak.

### Conversation Seeding Tracking

The **filesurf_conversation_seeded** metric tracks how many times klawed containers have been seeded with conversation history:

- **When it happens**: When a user resumes an existing session and the klawed container needs to be started, the system seeds the conversation with up to 100 of the most recent TEXT messages from the chat history.
- **Why it matters**: This metric helps track how often users are returning to existing sessions and getting contextual continuity. High seeding rates indicate good user retention and session resumption patterns.
- **What's included**: Only TEXT messages from the user and assistant are seeded (excludes tool messages, system messages, etc.)

```promql
# How often are we seeding conversations?
rate(filesurf_conversation_seeded_total[1h])

# Ratio of seeded conversations to container starts (indicates resume pattern)
rate(filesurf_conversation_seeded_total[5m]) / rate(filesurf_containers_started_total[5m])
```

**NOTE**: The `filesurf_errors` metric tracks different types of errors encountered in the application. The `type` tag helps identify error sources:
- `application`: General application errors
- `database_save`: Database save failures
- `file_attribute_read`: File attribute read failures
- `directory_listing`: Directory listing failures
- `websocket_serialization`: WebSocket message serialization failures

**NOTE**: Timing metrics (`*_time_seconds`) use the summary metric type which provides pre-calculated quantiles. This is more efficient than histograms for high-cardinality metrics but doesn't allow for arbitrary quantile calculations in PromQL. Use the provided quantiles (0.5, 0.95, 0.99) or calculate averages using `_sum / _count`.

## Disk Space Monitoring

The application monitors disk space for two key directories:
- **data/**: Contains SQLite database and user session files
- **logs/**: Contains application logs

If directories don't exist at startup, the application falls back to monitoring the root directory.

### Key Points:
- Metrics are read on-demand when Prometheus scrapes (no background polling)
- Values are in bytes (use `/1024/1024/1024` to convert to GB)
- Both free and total space are provided for calculating usage percentages
- Minimal performance impact (< 1ms per scrape)