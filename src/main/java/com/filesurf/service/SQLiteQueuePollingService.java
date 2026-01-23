package com.filesurf.service;

import io.quarkus.scheduler.Scheduled;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;

/**
 * Background service that polls SQLite queue databases for klawed responses
 * and saves them to the main application database.
 *
 * This service bridges the gap between:
 * 1. Klawed writing responses to SQLite queue
 * 2. Application database (chat_message table)
 * 3. WebSocket delivery (handled by ChatMessagePollingService)
 */
@ApplicationScoped
public class SQLiteQueuePollingService {

    private static final Logger LOGGER = Logger.getLogger(SQLiteQueuePollingService.class.getName());

    @Inject
    FileChatService fileChatService;

    @Inject
    SQLiteQueueClientPool clientPool;

    // Track active sessions that need polling
    // Map: sessionId -> userId
    private final Map<String, String> activeSessions = new ConcurrentHashMap<>();

    // Shutdown flag to stop polling during shutdown
    private final AtomicBoolean shuttingDown = new AtomicBoolean(false);

    /**
     * Shutdown hook to stop polling on application shutdown
     */
    @PreDestroy
    public void shutdown() {
        LOGGER.info("SQLiteQueuePollingService shutting down");
        shuttingDown.set(true);
        activeSessions.clear();
        LOGGER.info("SQLiteQueuePollingService shutdown complete");
    }

    /**
     * Register a session for SQLite queue polling
     */
    public void registerSession(String sessionId, String userId) {
        activeSessions.put(sessionId, userId);
        LOGGER.info("[SESSION:" + sessionId + "] Registered for SQLite queue polling (total active: " + activeSessions.size() + ")");
    }

    /**
     * Unregister a session from SQLite queue polling
     */
    public void unregisterSession(String sessionId) {
        activeSessions.remove(sessionId);
        // Clean up the pooled client for this session
        clientPool.removeSession(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Unregistered from SQLite queue polling (total active: " + activeSessions.size() + ")");
    }

    /**
     * Poll SQLite queues for klawed responses every 500ms
     */
    @Scheduled(every = "1s")
    @ActivateRequestContext
    public void pollSQLiteQueues() {
        // Skip if shutdown is in progress
        if (shuttingDown.get()) {
            return;
        }

        if (activeSessions.isEmpty()) {
            return; // Silent - no logging when there are no active sessions
        }

        // Poll each active session's SQLite queue
        for (Map.Entry<String, String> entry : activeSessions.entrySet()) {
            String sessionId = entry.getKey();
            String userId = entry.getValue();

            try {
                pollSessionQueue(sessionId, userId);
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error polling SQLite queue: " + e.getMessage());
            }
        }
    }

    /**
     * Poll a specific session's SQLite queue for klawed responses
     */
    private void pollSessionQueue(String sessionId, String userId) {
        try {
            // Get or create the singleton client for this session
            SQLiteQueueClient queueClient = clientPool.getOrCreateClient(sessionId, userId);

            // receiveMessages() will automatically save messages to the main database
            // via FileChatService (see SQLiteQueueClient.receiveMessages() implementation)
            List<String> messages = queueClient.receiveMessages();

            if (!messages.isEmpty()) {
                LOGGER.info("[SESSION:" + sessionId + "] Received " + messages.size() + " message(s) from klawed");
            }

        } catch (Exception e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Error receiving messages from SQLite queue: " + e.getMessage());
        }
    }
}
