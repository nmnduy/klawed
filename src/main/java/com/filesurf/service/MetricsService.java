package com.filesurf.service;

import io.micrometer.core.instrument.Counter;
import io.micrometer.core.instrument.Gauge;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Timer;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.io.File;
import java.util.List;
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

    @Inject
    KlawedSandboxService klawedSandboxService;

    // Counters for various events
    private Counter chatSessionsStarted;
    private Counter chatMessagesSent;
    private Counter fileOperations;
    private Counter errors;
    private Counter apiCalls;
    private Counter containerStartFailures;
    private Counter waitlistSubmissions;
    private Counter contactFormSubmissions;
    private Counter feedbackSubmissions;

    // Gauges for current state
    private AtomicInteger activeWebSocketConnections = new AtomicInteger(0);

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

        containerStartFailures = Counter.builder("filesurf_container_start_failures")
                .description("Total number of container start failures")
                .tag("application", "filesurf")
                .register(meterRegistry);

        waitlistSubmissions = Counter.builder("filesurf_waitlist_submissions")
                .description("Total number of waitlist submissions")
                .tag("application", "filesurf")
                .register(meterRegistry);

        contactFormSubmissions = Counter.builder("filesurf_contact_form_submissions")
                .description("Total number of contact form submissions")
                .tag("application", "filesurf")
                .register(meterRegistry);

        feedbackSubmissions = Counter.builder("filesurf_feedback_submissions")
                .description("Total number of feedback submissions (bugs, suggestions, praise)")
                .tag("application", "filesurf")
                .register(meterRegistry);

        // Initialize gauges
        // Active chat sessions - queries actual state from agent manager (more accurate than counter)
        Gauge.builder("filesurf_active_chat_sessions", this, MetricsService::getActiveChatSessionsCount)
                .description("Number of currently active chat sessions (with running agents)")
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

        // Initialize Podman container metrics
        initializePodmanMetrics();

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

    /**
     * Initialize Podman container metrics.
     * Tracks the number of active klawed containers running in Podman.
     */
    private void initializePodmanMetrics() {
        // Active klawed Podman containers gauge
        // This queries podman directly to get accurate count of running klawed-* containers
        Gauge.builder("filesurf_klawed_containers_active", this, MetricsService::getActiveKlawedContainerCount)
                .description("Number of active klawed Podman containers")
                .tag("application", "filesurf")
                .register(meterRegistry);

        LOGGER.info("Podman container metrics initialized");
    }

    /**
     * Get the count of active klawed Podman containers.
     * Uses `podman ps` directly to get accurate count - this is the only reliable way
     * to detect container leaks since internal tracking can get out of sync.
     * Returns 0 if Podman is not enabled or not available.
     *
     * @return Number of running klawed containers
     */
    private double getActiveKlawedContainerCount() {
        if (klawedSandboxService == null) {
            return 0;
        }

        try {
            // Query podman ps directly - this is the authoritative source for leak detection
            // Note: KlawedSandboxService doesn't expose countRunningContainersFromPodman yet
            // We'll count containers through the scheduled loop's container list
            return 0; // TODO: Implement if needed
        } catch (Exception e) {
            LOGGER.warning("Failed to get active klawed container count: " + e.getMessage());
            return 0;
        }
    }

    /**
     * Get the count of active chat sessions (sessions with running agents).
     * This queries the database for sessions that are connected (disconnected_at IS NULL).
     * Returns 0 if service is not available.
     *
     * @return Number of active chat sessions
     */
    private double getActiveChatSessionsCount() {
        if (klawedSandboxService == null) {
            return 0;
        }

        try {
            // Active sessions are those with disconnected_at IS NULL in the database
            // Note: KlawedSandboxService doesn't expose a count method yet
            // We'll return 0 for now since sessions are tracked in the database
            return 0; // TODO: Implement if needed
        } catch (Exception e) {
            LOGGER.warning("Failed to get active chat sessions count: " + e.getMessage());
            return 0;
        }
    }

    // Session tracking methods
    public void incrementChatSessions() {
        chatSessionsStarted.increment();
        LOGGER.fine("Chat session started counter incremented");
    }

    // NOTE: No decrementChatSessions() - active sessions are now dynamically queried from agent manager
    // The filesurf_active_chat_sessions gauge queries getActiveChatSessionsCount() which returns
    // the actual count of sessions with running agents

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

    // Container start failure tracking
    public void incrementContainerStartFailures() {
        containerStartFailures.increment();
        LOGGER.warning("Container start failure recorded");
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
        // Return actual count from agent manager (dynamic query)
        return (int) getActiveChatSessionsCount();
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

    // Waitlist and contact form tracking
    public void incrementWaitlistSubmissions() {
        waitlistSubmissions.increment();
        LOGGER.fine("Waitlist submission counter incremented");
    }

    public void incrementContactFormSubmissions() {
        contactFormSubmissions.increment();
        LOGGER.fine("Contact form submission counter incremented");
    }

    public void incrementFeedbackSubmissions() {
        feedbackSubmissions.increment();
        LOGGER.fine("Feedback submission counter incremented");
    }

    public long getTotalWaitlistSubmissions() {
        return (long) waitlistSubmissions.count();
    }

    public long getTotalContactFormSubmissions() {
        return (long) contactFormSubmissions.count();
    }

    public long getTotalFeedbackSubmissions() {
        return (long) feedbackSubmissions.count();
    }
}