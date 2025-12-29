/*
 * zmq_thread_pool.h - Thread pool for asynchronous tool execution in ZMQ daemon mode
 *
 * Provides a thread pool to execute tools asynchronously, preventing blocking
 * of the main ZMQ daemon event loop during long-running tool operations.
 */

#ifndef ZMQ_THREAD_POOL_H
#define ZMQ_THREAD_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <cjson/cJSON.h>

// Forward declarations
struct ConversationState;
struct ZMQContext;

// Tool execution task
typedef struct ZMQToolTask {
    char *tool_name;           // Name of the tool to execute
    char *tool_id;             // Unique ID for this tool call
    cJSON *tool_parameters;    // Tool parameters (JSON)
    struct ConversationState *state;  // Conversation state
    struct ZMQContext *zmq_ctx;       // ZMQ context for sending results
    int64_t enqueue_time_ms;   // When task was enqueued
    int64_t start_time_ms;     // When task started execution
    int64_t end_time_ms;       // When task completed execution
    struct ZMQToolTask *next;  // Next task in queue
} ZMQToolTask;

// Thread pool statistics
typedef struct ZMQThreadPoolStats {
    int total_tasks_submitted;
    int total_tasks_completed;
    int total_tasks_failed;
    int active_threads;
    int queued_tasks;
    int64_t total_execution_time_ms;
    int64_t avg_execution_time_ms;
    int64_t max_execution_time_ms;
} ZMQThreadPoolStats;

// Thread pool configuration
typedef struct ZMQThreadPoolConfig {
    int min_threads;           // Minimum number of threads in pool
    int max_threads;           // Maximum number of threads in pool
    int max_queue_size;        // Maximum number of queued tasks
    int64_t thread_idle_timeout_ms;  // Time before idle threads exit
    int64_t task_timeout_ms;   // Maximum time a task can run
} ZMQThreadPoolConfig;

// Thread pool
typedef struct ZMQThreadPool {
    pthread_t *threads;        // Array of thread IDs
    int thread_count;          // Current number of threads
    int max_threads;           // Maximum allowed threads

    // Task queue
    ZMQToolTask *task_queue_head;
    ZMQToolTask *task_queue_tail;
    int queue_size;
    int max_queue_size;

    // Synchronization
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    pthread_cond_t queue_not_full_cond;

    // Statistics
    ZMQThreadPoolStats stats;

    // Configuration
    ZMQThreadPoolConfig config;

    // Control flags
    bool shutdown_requested;
    bool running;
} ZMQThreadPool;

/**
 * Initialize thread pool with default configuration
 * @param max_threads Maximum number of threads in pool
 * @param max_queue_size Maximum number of queued tasks
 * @return Initialized thread pool or NULL on failure
 */
ZMQThreadPool* zmq_thread_pool_init(int max_threads, int max_queue_size);

/**
 * Initialize thread pool with custom configuration
 * @param config Thread pool configuration
 * @return Initialized thread pool or NULL on failure
 */
ZMQThreadPool* zmq_thread_pool_init_with_config(ZMQThreadPoolConfig *config);

/**
 * Submit a tool execution task to the thread pool
 * @param pool Thread pool
 * @param tool_name Name of the tool to execute
 * @param tool_id Unique ID for this tool call
 * @param tool_parameters Tool parameters (JSON)
 * @param state Conversation state
 * @param zmq_ctx ZMQ context for sending results
 * @return 0 on success, -1 on failure (queue full)
 */
int zmq_thread_pool_submit_task(ZMQThreadPool *pool,
                               const char *tool_name,
                               const char *tool_id,
                               cJSON *tool_parameters,
                               struct ConversationState *state,
                               struct ZMQContext *zmq_ctx);

/**
 * Wait for all submitted tasks to complete
 * @param pool Thread pool
 * @param timeout_ms Maximum time to wait in milliseconds (0 = wait forever)
 * @return 0 if all tasks completed, -1 if timeout or error
 */
int zmq_thread_pool_wait_for_completion(ZMQThreadPool *pool, int64_t timeout_ms);

/**
 * Get current thread pool statistics
 * @param pool Thread pool
 * @param stats Output parameter for statistics
 * @return 0 on success, -1 on failure
 */
int zmq_thread_pool_get_stats(ZMQThreadPool *pool, ZMQThreadPoolStats *stats);

/**
 * Shutdown thread pool gracefully
 * @param pool Thread pool to shutdown
 * @param wait_ms Maximum time to wait for threads to finish (0 = wait forever)
 * @return 0 on success, -1 on failure
 */
int zmq_thread_pool_shutdown(ZMQThreadPool *pool, int64_t wait_ms);

/**
 * Clean up thread pool resources
 * @param pool Thread pool to clean up
 */
void zmq_thread_pool_cleanup(ZMQThreadPool *pool);

/**
 * Get default thread pool configuration
 * @param config Output parameter for default configuration
 */
void zmq_thread_pool_default_config(ZMQThreadPoolConfig *config);

/**
 * Print thread pool statistics to log
 * @param pool Thread pool
 */
void zmq_thread_pool_log_stats(ZMQThreadPool *pool);

/**
 * Check if thread pool is accepting new tasks
 * @param pool Thread pool
 * @return true if accepting tasks, false if shutting down or full
 */
bool zmq_thread_pool_is_accepting_tasks(ZMQThreadPool *pool);

/**
 * Get number of queued tasks
 * @param pool Thread pool
 * @return Number of tasks in queue
 */
int zmq_thread_pool_get_queued_task_count(ZMQThreadPool *pool);

/**
 * Get number of active threads
 * @param pool Thread pool
 * @return Number of active threads
 */
int zmq_thread_pool_get_active_thread_count(ZMQThreadPool *pool);

#endif // ZMQ_THREAD_POOL_H
