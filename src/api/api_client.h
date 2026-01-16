/*
 * api_client.h - API Client with Retry Logic
 *
 * Core API communication layer that handles:
 * - API calls with exponential backoff retry logic
 * - Provider lazy initialization
 * - Context overflow recovery (auto-compaction integration)
 * - API call status messages (ZMQ/SQLite queue)
 * - Error handling and logging
 *
 * Note: call_api_with_retries() is declared in klawed_internal.h
 */

#ifndef API_CLIENT_H
#define API_CLIENT_H

#include "../klawed_internal.h"

#endif // API_CLIENT_H
