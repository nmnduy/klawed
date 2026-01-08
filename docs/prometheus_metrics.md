# FileSurf v2 - Prometheus Metrics

This document describes all custom metrics exposed by the FileSurf v2 application through the Prometheus endpoint (`/metrics`).

## Application Metrics

### Session Metrics
- **filesurf_chat_sessions_started**: [application] Total number of chat sessions started
- **filesurf_active_chat_sessions**: [application] Number of currently active chat sessions

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