/*
 * zmq_thread_pool.c - Thread pool for asynchronous tool execution in ZMQ daemon mode
 *
 * Provides a thread pool to execute tools asynchronously, preventing blocking
 * of the main ZMQ daemon event loop during long-running tool operations.
 */

#include "zmq_thread_pool.h"
#include "zmq_socket.h"
#include "logger.h"
#include "klawed_internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

// Helper function to get current time in milliseconds
static int64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

// Worker thread function
static void* worker_thread(void *arg) {
    ZMQThreadPool *pool = (ZMQThreadPool*)arg;
    
    LOG_DEBUG("ZMQ Thread Pool: Worker thread started");
    
    while (true) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait for task or shutdown
        while (pool->task_queue_head == NULL && !pool->shutdown_requested) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        // Check for shutdown
        if (pool->shutdown_requested && pool->task_queue_head == NULL) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // Get task from queue
        ZMQToolTask *task = pool->task_queue_head;
        if (task) {
            // Remove from queue
            pool->task_queue_head = task->next;
            if (pool->task_queue_head == NULL) {
                pool->task_queue_tail = NULL;
            }
            pool->queue_size--;
            
            // Signal that queue is not full anymore
            pthread_cond_signal(&pool->queue_not_full_cond);
            
            // Update statistics
            pool->stats.queued_tasks = pool->queue_size;
            pool->stats.active_threads++;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        if (task) {
            // Execute tool
            task->start_time_ms = get_current_time_ms();
            
            LOG_INFO("ZMQ Thread Pool: Executing tool '%s' (id: %s) in worker thread",
                     task->tool_name, task->tool_id);
            
            // Send TOOL request message before execution
            zmq_send_tool_request(task->zmq_ctx, task->tool_name, task->tool_id, task->tool_parameters);
            
            // Execute tool
            cJSON *tool_result = execute_tool(task->tool_name, task->tool_parameters, task->state);
            
            // Send tool result response
            zmq_send_tool_result(task->zmq_ctx, task->tool_name, task->tool_id, tool_result, 0);
            
            // Update task completion time
            task->end_time_ms = get_current_time_ms();
            
            // Update statistics
            pthread_mutex_lock(&pool->queue_mutex);
            pool->stats.total_tasks_completed++;
            pool->stats.active_threads--;
            
            int64_t execution_time = task->end_time_ms - task->start_time_ms;
            pool->stats.total_execution_time_ms += execution_time;
            
            if (execution_time > pool->stats.max_execution_time_ms) {
                pool->stats.max_execution_time_ms = execution_time;
            }
            
            if (pool->stats.total_tasks_completed > 0) {
                pool->stats.avg_execution_time_ms = pool->stats.total_execution_time_ms / pool->stats.total_tasks_completed;
            }
            
            // Signal that a task completed (for wait_for_completion)
            pthread_cond_broadcast(&pool->queue_cond);
            pthread_mutex_unlock(&pool->queue_mutex);
            
            LOG_INFO("ZMQ Thread Pool: Tool '%s' (id: %s) completed in %lld ms",
                     task->tool_name, task->tool_id, (long long)execution_time);
            
            // Clean up task
            if (task->tool_name) free(task->tool_name);
            if (task->tool_id) free(task->tool_id);
            if (task->tool_parameters) cJSON_Delete(task->tool_parameters);
            free(task);
            
            if (tool_result) cJSON_Delete(tool_result);
        }
    }
    
    LOG_DEBUG("ZMQ Thread Pool: Worker thread exiting");
    return NULL;
}

ZMQThreadPool* zmq_thread_pool_init(int max_threads, int max_queue_size) {
    ZMQThreadPoolConfig config;
    zmq_thread_pool_default_config(&config);
    config.max_threads = max_threads;
    config.max_queue_size = max_queue_size;
    
    return zmq_thread_pool_init_with_config(&config);
}

ZMQThreadPool* zmq_thread_pool_init_with_config(ZMQThreadPoolConfig *config) {
    if (!config) {
        LOG_ERROR("ZMQ Thread Pool: Config cannot be NULL");
        return NULL;
    }
    
    if (config->max_threads <= 0) {
        LOG_ERROR("ZMQ Thread Pool: max_threads must be positive");
        return NULL;
    }
    
    if (config->max_queue_size < 0) {
        LOG_ERROR("ZMQ Thread Pool: max_queue_size cannot be negative");
        return NULL;
    }
    
    ZMQThreadPool *pool = calloc(1, sizeof(ZMQThreadPool));
    if (!pool) {
        LOG_ERROR("ZMQ Thread Pool: Failed to allocate pool memory");
        return NULL;
    }
    
    // Initialize mutex and condition variables
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        LOG_ERROR("ZMQ Thread Pool: Failed to initialize mutex");
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->queue_cond, NULL) != 0) {
        LOG_ERROR("ZMQ Thread Pool: Failed to initialize condition variable");
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->queue_not_full_cond, NULL) != 0) {
        LOG_ERROR("ZMQ Thread Pool: Failed to initialize not-full condition variable");
        pthread_cond_destroy(&pool->queue_cond);
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    // Allocate thread array
    pool->threads = calloc((size_t)config->max_threads, sizeof(pthread_t));
    if (!pool->threads) {
        LOG_ERROR("ZMQ Thread Pool: Failed to allocate thread array");
        pthread_cond_destroy(&pool->queue_not_full_cond);
        pthread_cond_destroy(&pool->queue_cond);
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    // Initialize pool state
    pool->max_threads = config->max_threads;
    pool->max_queue_size = config->max_queue_size;
    pool->queue_size = 0;
    pool->task_queue_head = NULL;
    pool->task_queue_tail = NULL;
    pool->shutdown_requested = false;
    pool->running = true;
    
    // Copy configuration
    pool->config = *config;
    
    // Initialize statistics
    memset(&pool->stats, 0, sizeof(pool->stats));
    
    // Create initial threads
    int threads_to_create = config->min_threads > 0 ? config->min_threads : 1;
    if (threads_to_create > config->max_threads) {
        threads_to_create = config->max_threads;
    }
    
    for (int i = 0; i < threads_to_create; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            LOG_ERROR("ZMQ Thread Pool: Failed to create worker thread %d", i);
            // Clean up already created threads
            pool->shutdown_requested = true;
            pthread_cond_broadcast(&pool->queue_cond);
            
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            
            free(pool->threads);
            pthread_cond_destroy(&pool->queue_not_full_cond);
            pthread_cond_destroy(&pool->queue_cond);
            pthread_mutex_destroy(&pool->queue_mutex);
            free(pool);
            return NULL;
        }
        pool->thread_count++;
    }
    
    LOG_INFO("ZMQ Thread Pool: Initialized with %d threads (max: %d, queue size: %d)",
             pool->thread_count, pool->max_threads, pool->max_queue_size);
    
    return pool;
}

int zmq_thread_pool_submit_task(ZMQThreadPool *pool,
                               const char *tool_name,
                               const char *tool_id,
                               cJSON *tool_parameters,
                               struct ConversationState *state,
                               struct ZMQContext *zmq_ctx) {
    if (!pool || !tool_name || !tool_id || !state || !zmq_ctx) {
        LOG_ERROR("ZMQ Thread Pool: Invalid parameters for submit_task");
        return -1;
    }
    
    if (pool->shutdown_requested) {
        LOG_ERROR("ZMQ Thread Pool: Cannot submit task, pool is shutting down");
        return -1;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Check if queue is full
    if (pool->max_queue_size > 0 && pool->queue_size >= pool->max_queue_size) {
        LOG_WARN("ZMQ Thread Pool: Task queue full (%d/%d), cannot submit task for tool '%s'",
                 pool->queue_size, pool->max_queue_size, tool_name);
        pthread_mutex_unlock(&pool->queue_mutex);
        return -1;
    }
    
    // Create task
    ZMQToolTask *task = calloc(1, sizeof(ZMQToolTask));
    if (!task) {
        LOG_ERROR("ZMQ Thread Pool: Failed to allocate task memory for tool '%s'", tool_name);
        pthread_mutex_unlock(&pool->queue_mutex);
        return -1;
    }
    
    task->tool_name = strdup(tool_name);
    task->tool_id = strdup(tool_id);
    task->tool_parameters = tool_parameters ? cJSON_Duplicate(tool_parameters, 1) : cJSON_CreateObject();
    task->state = state;
    task->zmq_ctx = zmq_ctx;
    task->enqueue_time_ms = get_current_time_ms();
    task->next = NULL;
    
    if (!task->tool_name || !task->tool_id || !task->tool_parameters) {
        LOG_ERROR("ZMQ Thread Pool: Failed to duplicate task data for tool '%s'", tool_name);
        if (task->tool_name) free(task->tool_name);
        if (task->tool_id) free(task->tool_id);
        if (task->tool_parameters) cJSON_Delete(task->tool_parameters);
        free(task);
        pthread_mutex_unlock(&pool->queue_mutex);
        return -1;
    }
    
    // Add to queue
    if (pool->task_queue_tail) {
        pool->task_queue_tail->next = task;
        pool->task_queue_tail = task;
    } else {
        pool->task_queue_head = task;
        pool->task_queue_tail = task;
    }
    
    pool->queue_size++;
    pool->stats.total_tasks_submitted++;
    pool->stats.queued_tasks = pool->queue_size;
    
    LOG_DEBUG("ZMQ Thread Pool: Submitted task for tool '%s' (id: %s), queue size: %d/%d",
              tool_name, tool_id, pool->queue_size, pool->max_queue_size);
    
    // Signal worker threads
    pthread_cond_signal(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return 0;
}



int zmq_thread_pool_get_stats(ZMQThreadPool *pool, ZMQThreadPoolStats *stats) {
    if (!pool || !stats) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    *stats = pool->stats;
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return 0;
}

int zmq_thread_pool_shutdown(ZMQThreadPool *pool, int64_t wait_ms) {
    if (!pool) {
        return -1;
    }
    
    LOG_INFO("ZMQ Thread Pool: Shutting down thread pool");
    
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown_requested = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Wait for threads to finish
    for (int i = 0; i < pool->thread_count; i++) {
        if (wait_ms > 0) {
            // For timeout support, we'll use a simple approach:
            // Detach the thread and let it clean up itself
            LOG_DEBUG("ZMQ Thread Pool: Detaching thread %d (timeout: %lld ms)", 
                     i, (long long)wait_ms);
            pthread_detach(pool->threads[i]);
        } else {
            pthread_join(pool->threads[i], NULL);
        }
    }
    
    pool->running = false;
    
    LOG_INFO("ZMQ Thread Pool: Shutdown complete");
    
    return 0;
}

void zmq_thread_pool_cleanup(ZMQThreadPool *pool) {
    if (!pool) return;
    
    // Shutdown if still running
    if (pool->running) {
        zmq_thread_pool_shutdown(pool, 5000); // Wait up to 5 seconds
    }
    
    // Clean up any remaining tasks in queue
    pthread_mutex_lock(&pool->queue_mutex);
    ZMQToolTask *task = pool->task_queue_head;
    while (task) {
        ZMQToolTask *next = task->next;
        if (task->tool_name) free(task->tool_name);
        if (task->tool_id) free(task->tool_id);
        if (task->tool_parameters) cJSON_Delete(task->tool_parameters);
        free(task);
        task = next;
    }
    pool->task_queue_head = NULL;
    pool->task_queue_tail = NULL;
    pool->queue_size = 0;
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Free thread array
    if (pool->threads) {
        free(pool->threads);
    }
    
    // Destroy synchronization primitives
    pthread_cond_destroy(&pool->queue_not_full_cond);
    pthread_cond_destroy(&pool->queue_cond);
    pthread_mutex_destroy(&pool->queue_mutex);
    
    free(pool);
    
    LOG_DEBUG("ZMQ Thread Pool: Cleanup completed");
}

void zmq_thread_pool_default_config(ZMQThreadPoolConfig *config) {
    if (!config) return;
    
    config->min_threads = 2;
    config->max_threads = 8;
    config->max_queue_size = 100;
    config->thread_idle_timeout_ms = 60000; // 60 seconds
    config->task_timeout_ms = 300000; // 5 minutes
}

void zmq_thread_pool_log_stats(ZMQThreadPool *pool) {
    if (!pool) return;
    
    ZMQThreadPoolStats stats;
    if (zmq_thread_pool_get_stats(pool, &stats) == 0) {
        LOG_INFO("ZMQ Thread Pool Statistics:");
        LOG_INFO("  Total tasks submitted: %d", stats.total_tasks_submitted);
        LOG_INFO("  Total tasks completed: %d", stats.total_tasks_completed);
        LOG_INFO("  Total tasks failed: %d", stats.total_tasks_failed);
        LOG_INFO("  Active threads: %d", stats.active_threads);
        LOG_INFO("  Queued tasks: %d", stats.queued_tasks);
        LOG_INFO("  Average execution time: %lld ms", (long long)stats.avg_execution_time_ms);
        LOG_INFO("  Maximum execution time: %lld ms", (long long)stats.max_execution_time_ms);
    }
}

bool zmq_thread_pool_is_accepting_tasks(ZMQThreadPool *pool) {
    if (!pool) return false;
    
    pthread_mutex_lock(&pool->queue_mutex);
    bool accepting = pool->running && !pool->shutdown_requested &&
                     (pool->max_queue_size == 0 || pool->queue_size < pool->max_queue_size);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return accepting;
}

int zmq_thread_pool_get_queued_task_count(ZMQThreadPool *pool) {
    if (!pool) return -1;
    
    pthread_mutex_lock(&pool->queue_mutex);
    int count = pool->queue_size;
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return count;
}

int zmq_thread_pool_get_active_thread_count(ZMQThreadPool *pool) {
    if (!pool) return -1;
    
    pthread_mutex_lock(&pool->queue_mutex);
    int count = pool->stats.active_threads;
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return count;
}

int zmq_thread_pool_wait_for_completion(ZMQThreadPool *pool, int64_t timeout_ms) {
    if (!pool) return -1;
    
    int64_t start_time = get_current_time_ms();
    
    while (true) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Check if all tasks are completed
        bool all_completed = (pool->queue_size == 0 && pool->stats.active_threads == 0);
        
        if (all_completed) {
            pthread_mutex_unlock(&pool->queue_mutex);
            return 0;
        }
        
        // Check timeout
        if (timeout_ms > 0) {
            int64_t current_time = get_current_time_ms();
            int64_t elapsed = current_time - start_time;
            
            if (elapsed >= timeout_ms) {
                pthread_mutex_unlock(&pool->queue_mutex);
                LOG_WARN("ZMQ Thread Pool: Timeout waiting for task completion (%lld ms elapsed)",
                         (long long)elapsed);
                return -1;
            }
            
            // Calculate remaining time
            int64_t remaining_ms = timeout_ms - elapsed;
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += remaining_ms / 1000;
            timeout.tv_nsec += (remaining_ms % 1000) * 1000000;
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000;
            }
            
            // Wait for condition
            pthread_cond_timedwait(&pool->queue_cond, &pool->queue_mutex, &timeout);
        } else {
            // Wait indefinitely
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
    }
    
    return 0;
}
