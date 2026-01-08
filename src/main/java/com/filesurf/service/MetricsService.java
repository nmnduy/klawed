package com.filesurf.service;

import io.micrometer.core.instrument.Counter;
import io.micrometer.core.instrument.Gauge;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Timer;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.io.File;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.logging.Logger;

/**
 * Service for tracking application metrics and usage statistics.
 * Provides Prometheus-compatible metrics for monitoring application health and usage.
 */
@ApplicationScoped
public class MetricsService {

    private static final Logger LOGGER = Logger.getLogger(MetricsService.class.getName());

    @Inject
    MeterRegistry meterRegistry;

    // Counters for various events
    private Counter chatSessionsStarted;
    private Counter chatMessagesSent;
    private Counter fileOperations;
    private Counter errors;
    private Counter apiCalls;
    
    // Gauges for current state
    private AtomicInteger activeChatSessions;
    private AtomicInteger activeWebSocketConnections;
    
    // Timers for performance metrics
    private Timer chatResponseTime;
    private Timer fileOperationTime;
    private Timer apiCallTime;
    
    // Custom metrics tracking
    private ConcurrentHashMap<String, AtomicInteger> userActivityMap = new ConcurrentHashMap<>();
    
    // Disk space monitoring
    private File dataDirectory;
    private File logsDirectory;

    public void initializeMetrics() {
        LOGGER.info("Initializing application metrics");
        
        // Initialize counters
        chatSessionsStarted = Counter.builder("filesurf_chat_sessions_started")
                .description("Total number of chat sessions started")
                .tag("application", "filesurf")
                .register(meterRegistry);
                
        chatMessagesSent = Counter.builder("filesurf_chat_messages_sent")
                .description("Total number of chat messages sent")
                .tag("application", "filesurf")
                .register(meterRegistry);
                
        fileOperations = Counter.builder("filesurf_file_operations")
                .description("Total number of file operations performed")
                .tag("application", "filesurf")
                .register(meterRegistry);
                
        errors = Counter.builder("filesurf_errors")
                .description("Total number of errors encountered")
                .tag("application", "filesurf")
                .tag("type", "application")
                .register(meterRegistry);
                
        apiCalls = Counter.builder("filesurf_api_calls")
                .description("Total number of API calls made")
                .tag("application", "filesurf")
                .register(meterRegistry);
        
        // Initialize gauges
        activeChatSessions = new AtomicInteger(0);
        Gauge.builder("filesurf_active_chat_sessions", activeChatSessions, AtomicInteger::get)
                .description("Number of currently active chat sessions")
                .tag("application", "filesurf")
                .register(meterRegistry);
                
        activeWebSocketConnections = new AtomicInteger(0);
        Gauge.builder("filesurf_active_websocket_connections", activeWebSocketConnections, AtomicInteger::get)
                .description("Number of currently active WebSocket connections")
                .tag("application", "filesurf")
                .register(meterRegistry);
        
        // Initialize timers
        chatResponseTime = Timer.builder("filesurf_chat_response_time")
                .description("Time taken to process chat responses")
                .tag("application", "filesurf")
                .publishPercentiles(0.5, 0.95, 0.99)
                .register(meterRegistry);
                
        fileOperationTime = Timer.builder("filesurf_file_operation_time")
                .description("Time taken to perform file operations")
                .tag("application", "filesurf")
                .publishPercentiles(0.5, 0.95, 0.99)
                .register(meterRegistry);
                
        apiCallTime = Timer.builder("filesurf_api_call_time")
                .description("Time taken to make API calls")
                .tag("application", "filesurf")
                .publishPercentiles(0.5, 0.95, 0.99)
                .register(meterRegistry);
        
        // Initialize disk space monitoring
        initializeDiskSpaceMetrics();
        
        LOGGER.info("Application metrics initialized successfully");
    }
    
    private void initializeDiskSpaceMetrics() {
        // Monitor data directory (where database and session files are stored)
        dataDirectory = new File("data");
        if (!dataDirectory.exists()) {
            dataDirectory = new File(".");
            LOGGER.warning("Data directory not found, monitoring root directory instead");
        }
        
        // Monitor logs directory
        logsDirectory = new File("logs");
        if (!logsDirectory.exists()) {
            logsDirectory = dataDirectory;
            LOGGER.warning("Logs directory not found, using data directory for monitoring");
        }
        
        // Free disk space for data directory (in bytes)
        Gauge.builder("filesurf_disk_free_bytes", dataDirectory, File::getUsableSpace)
                .description("Free disk space available for data storage (bytes)")
                .tag("application", "filesurf")
                .tag("directory", "data")
                .register(meterRegistry);
        
        // Total disk space for data directory (in bytes)
        Gauge.builder("filesurf_disk_total_bytes", dataDirectory, File::getTotalSpace)
                .description("Total disk space for data storage (bytes)")
                .tag("application", "filesurf")
                .tag("directory", "data")
                .register(meterRegistry);
        
        // Free disk space for logs directory (in bytes)
        Gauge.builder("filesurf_disk_free_bytes", logsDirectory, File::getUsableSpace)
                .description("Free disk space available for logs (bytes)")
                .tag("application", "filesurf")
                .tag("directory", "logs")
                .register(meterRegistry);
        
        // Total disk space for logs directory (in bytes)
        Gauge.builder("filesurf_disk_total_bytes", logsDirectory, File::getTotalSpace)
                .description("Total disk space for logs (bytes)")
                .tag("application", "filesurf")
                .tag("directory", "logs")
                .register(meterRegistry);
        
        LOGGER.info("Disk space metrics initialized - monitoring data and logs directories");
    }
    
    // Session tracking methods
    public void incrementChatSessions() {
        chatSessionsStarted.increment();
        activeChatSessions.incrementAndGet();
        LOGGER.fine("Chat session started. Total sessions: " + activeChatSessions.get());
    }
    
    public void decrementChatSessions() {
        int current = activeChatSessions.decrementAndGet();
        if (current < 0) {
            activeChatSessions.set(0);
        }
        LOGGER.fine("Chat session ended. Active sessions: " + activeChatSessions.get());
    }
    
    // WebSocket connection tracking
    public void incrementWebSocketConnections() {
        activeWebSocketConnections.incrementAndGet();
        LOGGER.fine("WebSocket connection established. Active connections: " + activeWebSocketConnections.get());
    }
    
    public void decrementWebSocketConnections() {
        int current = activeWebSocketConnections.decrementAndGet();
        if (current < 0) {
            activeWebSocketConnections.set(0);
        }
        LOGGER.fine("WebSocket connection closed. Active connections: " + activeWebSocketConnections.get());
    }
    
    // Message tracking
    public void incrementChatMessages() {
        chatMessagesSent.increment();
        LOGGER.fine("Chat message sent");
    }
    
    // File operation tracking
    public void incrementFileOperations() {
        fileOperations.increment();
        LOGGER.fine("File operation performed");
    }
    
    // Error tracking
    public void incrementErrors(String errorType) {
        errors.increment();
        LOGGER.warning("Error recorded: " + errorType);
    }
    
    // API call tracking
    public void incrementApiCalls() {
        apiCalls.increment();
        LOGGER.fine("API call made");
    }
    
    // Timer methods
    public Timer.Sample startChatResponseTimer() {
        return Timer.start(meterRegistry);
    }
    
    public void stopChatResponseTimer(Timer.Sample sample) {
        sample.stop(chatResponseTime);
    }
    
    public Timer.Sample startFileOperationTimer() {
        return Timer.start(meterRegistry);
    }
    
    public void stopFileOperationTimer(Timer.Sample sample) {
        sample.stop(fileOperationTime);
    }
    
    public Timer.Sample startApiCallTimer() {
        return Timer.start(meterRegistry);
    }
    
    public void stopApiCallTimer(Timer.Sample sample) {
        sample.stop(apiCallTime);
    }
    
    // User activity tracking
    public void trackUserActivity(String userId) {
        userActivityMap.computeIfAbsent(userId, k -> new AtomicInteger(0)).incrementAndGet();
        
        // Update gauge for this user
        AtomicInteger userCounter = userActivityMap.get(userId);
        Gauge.builder("filesurf_user_activity", userCounter, AtomicInteger::get)
                .description("Activity count for user")
                .tag("application", "filesurf")
                .tag("user_id", userId)
                .register(meterRegistry);
    }
    
    // Get current metrics
    public int getActiveChatSessions() {
        return activeChatSessions.get();
    }
    
    public int getActiveWebSocketConnections() {
        return activeWebSocketConnections.get();
    }
    
    public long getTotalChatSessions() {
        return (long) chatSessionsStarted.count();
    }
    
    public long getTotalChatMessages() {
        return (long) chatMessagesSent.count();
    }
    
    public long getTotalFileOperations() {
        return (long) fileOperations.count();
    }
    
    public long getTotalErrors() {
        return (long) errors.count();
    }
    
    public long getTotalApiCalls() {
        return (long) apiCalls.count();
    }
}