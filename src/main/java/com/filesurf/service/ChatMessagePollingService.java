package com.filesurf.service;

import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.model.ChatConstants;
import com.filesurf.service.FileChatService;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.quarkus.scheduler.Scheduled;
import io.quarkus.websockets.next.WebSocketConnection;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import jakarta.inject.Inject;
import jakarta.transaction.Transactional;

import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;

@ApplicationScoped
public class ChatMessagePollingService {

    private static final Logger LOGGER = Logger.getLogger(ChatMessagePollingService.class.getName());
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Inject
    FileChatService fileChatService;

    @Inject
    SQLiteQueueClientPool clientPool;

    @Inject
    MetricsService metricsService;

    // Store active WebSocket connections by session ID
    private final Map<String, WebSocketConnection> activeConnections = new ConcurrentHashMap<>();

    // Single-threaded executor to serialize all message polling
    private final ExecutorService pollingExecutor = Executors.newSingleThreadExecutor();

    // Control flag for polling loop
    private final AtomicBoolean pollingActive = new AtomicBoolean(true);

    /**
     * Shutdown hook to stop polling on application shutdown
     */
    @PreDestroy
    public void shutdown() {
        LOGGER.info("ChatMessagePollingService shutting down");
        stopPolling();
        activeConnections.clear();
        // Shutdown the executor gracefully
        pollingExecutor.shutdown();
        try {
            if (!pollingExecutor.awaitTermination(5, java.util.concurrent.TimeUnit.SECONDS)) {
                pollingExecutor.shutdownNow();
            }
        } catch (InterruptedException e) {
            pollingExecutor.shutdownNow();
            Thread.currentThread().interrupt();
        }
        LOGGER.info("ChatMessagePollingService shutdown complete");
    }

    /**
     * Register a WebSocket connection for a session
     */
    public void registerConnection(String sessionId, WebSocketConnection connection) {
        activeConnections.put(sessionId, connection);
        LOGGER.info("[SESSION:" + sessionId + "] Connection registered for polling (total active: " + activeConnections.size() + ")");
    }

    /**
     * Unregister a WebSocket connection for a session
     */
    public void unregisterConnection(String sessionId) {
        activeConnections.remove(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Connection unregistered from polling (total active: " + activeConnections.size() + ")");
    }

    /**
     * Get active connection for a session
     */
    public WebSocketConnection getConnection(String sessionId) {
        return activeConnections.get(sessionId);
    }

    /**
     * Check if a session has an active connection
     */
    public boolean hasActiveConnection(String sessionId) {
        WebSocketConnection connection = activeConnections.get(sessionId);
        return connection != null && !connection.isClosed();
    }

    /**
     * Send a message to klawed via SQLite queue
     *
     * @param sessionId The session ID
     * @param userId The user ID
     * @param message The message to send
     * @throws IOException If sending fails
     */
    public void sendMessageToKlawed(String sessionId, String userId, String message) throws IOException {
        LOGGER.info("[SESSION:" + sessionId + "] Sending message to klawed via SQLite queue");

        try {
            // Get the pooled client
            SQLiteQueueClient queueClient = clientPool.getOrCreateClient(sessionId, userId);

            // Client is configured for RECEIVING (sender=klawed, receiver=client)
            // For SENDING, we need sender=client, receiver=klawed
            // Since sendMessage() always uses config.getSenderName() as sender,
            // we need to directly insert the message with correct sender/receiver

            queueClient.sendMessageFrom("client", "klawed", message);

            // Track message sent to klawed
            metricsService.incrementMessagesSentToKlawed();

            LOGGER.info("[SESSION:" + sessionId + "] Message sent to klawed (length: " + message.length() + " chars)");
        } catch (Exception e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Error sending message to klawed: " + e.getMessage());
            throw new IOException("Failed to send message to klawed", e);
        }
    }

    /**
     * Stop the polling service
     */
    public void stopPolling() {
        pollingActive.set(false);
        LOGGER.info("Polling service stopped");
    }

    /**
     * Start the polling service
     */
    public void startPolling() {
        pollingActive.set(true);
        LOGGER.info("Polling service started");
    }

    /**
     * Async polling loop for unsent messages every 300ms.
     * Only polls for messages belonging to sessions with active WebSocket connections.
     * Messages for disconnected sessions are ignored - users who drop off don't need those messages.
     * Uses a single-threaded executor to serialize all polling operations.
     */
    @Scheduled(every = "1s")
    @ActivateRequestContext
    public void pollAndSendUnsentMessages() {
        // Don't submit if executor is shutting down (prevents RejectedExecutionException on shutdown)
        if (pollingExecutor.isShutdown()) {
            return;
        }
        // Submit to single-threaded executor to serialize polling
        pollingExecutor.submit(this::pollAndSendUnsentMessagesInternal);
    }

    /**
     * Internal polling logic - executed by single-threaded executor
     */
    @ActivateRequestContext
    void pollAndSendUnsentMessagesInternal() {
        if (!pollingActive.get()) {
            LOGGER.fine("Polling is paused");
            return;
        }

        // Clean up stale connections first
        activeConnections.entrySet().removeIf(entry -> {
            if (entry.getValue().isClosed()) {
                LOGGER.info("[SESSION:" + entry.getKey() + "] Stale connection removed from polling map");
                return true;
            }
            return false;
        });

        // Only poll if there are active connections
        if (activeConnections.isEmpty()) {
            return; // Silent - don't log when there are no connections
        }

        // Poll for unsent messages for all sessions with active connections
        for (Map.Entry<String, WebSocketConnection> entry : activeConnections.entrySet()) {
            String sessionId = entry.getKey();
            WebSocketConnection connection = entry.getValue();

            // Double-check connection is still open
            if (connection.isClosed()) {
                continue;
            }

            processUnsentMessagesForSession(sessionId, connection);
        }
    }

    /**
     * Create JSON message string based on message type
     */
    private String createJsonMessage(ChatMessageRecord message) throws Exception {
        String messageType = message.getMessageType();
        String content = message.getContent();

        if (ChatConstants.DB_MESSAGE_TYPE_ERROR.equals(messageType)) {
            return objectMapper.writeValueAsString(
                KlawedSocketMessage.createError(content)
            );
        } else if ("status".equals(messageType)) {
            // Check if it's an END_AI_TURN message (stored as status type with JSON content)
            if (content != null && content.contains("END_AI_TURN")) {
                try {
                    Map<String, Object> endTurnData = objectMapper.readValue(content, Map.class);
                    if (ChatConstants.MESSAGE_TYPE_END_AI_TURN.equals(endTurnData.get("messageType"))) {
                        return objectMapper.writeValueAsString(
                            KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_END_AI_TURN, null)
                        );
                    }
                } catch (Exception e) {
                    // Not JSON, treat as regular status
                }
            }
            return objectMapper.writeValueAsString(
                KlawedSocketMessage.createStatus(content)
            );
        } else if (ChatConstants.DB_MESSAGE_TYPE_API_CALL.equals(messageType)) {
            try {
                Map<String, Object> apiCallData = objectMapper.readValue(content, Map.class);
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_API_CALL, apiCallData)
                );
            } catch (Exception e) {
                LOGGER.warning("Failed to parse API_CALL message: " + e.getMessage());
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.createStatus("AI is thinking")
                );
            }
        } else if (ChatConstants.DB_MESSAGE_TYPE_TOOL.equals(messageType)) {
            try {
                Map<String, Object> toolData = objectMapper.readValue(content, Map.class);
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_TOOL, toolData)
                );
            } catch (Exception e) {
                LOGGER.warning("Failed to parse TOOL message: " + e.getMessage());
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.createStatus("AI is working")
                );
            }
        } else if (ChatConstants.DB_MESSAGE_TYPE_TOOL_RESULT.equals(messageType)) {
            try {
                Map<String, Object> toolResultData = objectMapper.readValue(content, Map.class);
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_TOOL_RESULT, toolResultData)
                );
            } catch (Exception e) {
                LOGGER.warning("Failed to parse TOOL_RESULT message: " + e.getMessage());
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.createStatus("AI is working")
                );
            }
        } else if (ChatConstants.DB_MESSAGE_TYPE_FILE_UPLOAD.equals(messageType)) {
            // File upload system events - send as FILE_UPLOAD type to klawed
            try {
                Map<String, Object> fileUploadData = objectMapper.readValue(content, Map.class);
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_FILE_UPLOAD, fileUploadData)
                );
            } catch (Exception e) {
                LOGGER.warning("Failed to parse FILE_UPLOAD message: " + e.getMessage());
                // Still send the raw content as a status message
                return objectMapper.writeValueAsString(
                    KlawedSocketMessage.createStatus("Files uploaded to workspace")
                );
            }
        } else if (ChatConstants.DB_MESSAGE_TYPE_AUTO_COMPACTION.equals(messageType)) {
            // Auto compaction event - don't forward to client, just log
            // Logging already handled in SQLiteQueueClient
            return null;
        } else {
            // Default to text message
            return objectMapper.writeValueAsString(
                KlawedSocketMessage.createText(content)
            );
        }
    }

    /**
     * Mark a message as sent
     */
    @Transactional
    @ActivateRequestContext
    public void markMessageAsSent(String sessionId, Long messageId) {
        fileChatService.markMessageAsSent(sessionId, messageId);
        LOGGER.fine("[SESSION:" + sessionId + "] Message " + messageId + " marked as sent in database");
    }

    /**
     * Mark a message as sent from an IO thread (WebSocket callback)
     * Runs the database operation on a worker thread
     */
    public void markMessageAsSentFromIoThread(String sessionId, Long messageId) {
        // Don't submit if executor is shutting down
        if (pollingExecutor.isShutdown()) {
            return;
        }
        // Submit to executor to run on worker thread
        pollingExecutor.submit(() -> {
            try {
                // Use @Transactional version on worker thread
                markMessageAsSent(sessionId, messageId);
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to mark message " + messageId + " as sent: " + e.getMessage());
            }
        });
    }

    /**
     * Process and send unsent messages for a given session and connection.
     * Called by the scheduled poller for each active WebSocket connection.
     *
     * @param sessionId The session ID
     * @param connection The active WebSocket connection
     */
    private void processUnsentMessagesForSession(String sessionId, WebSocketConnection connection) {
        try {
            List<ChatMessageRecord> unsentMessages = fileChatService.findUnsentMessagesForSession(sessionId);

            if (!unsentMessages.isEmpty()) {
                LOGGER.fine("[SESSION:" + sessionId + "] Found " + unsentMessages.size() + " unsent messages");
            }

            for (ChatMessageRecord message : unsentMessages) {
                try {
                    // Create appropriate message based on message type
                    String jsonMessage = createJsonMessage(message);

                    // Skip messages that should not be forwarded (e.g., AUTO_COMPACTION)
                    if (jsonMessage == null) {
                        LOGGER.fine("[SESSION:" + sessionId + "] Skipping non-forwardable message ID: " + message.getId());
                        // Use IO thread-safe method since we're in the polling thread
                        markMessageAsSentFromIoThread(sessionId, message.getId());
                        continue;
                    }

                    // Send the message asynchronously
                    connection.sendText(jsonMessage).subscribe().with(
                        success -> {
                            LOGGER.info("[SESSION:" + sessionId + "] Sent message ID: " + message.getId());
                            // Track message sent from klawed to user
                            metricsService.incrementMessagesReceivedFromKlawed();
                            // Use IO thread-safe method for WebSocket callbacks
                            markMessageAsSentFromIoThread(sessionId, message.getId());
                        },
                        failure -> {
                            LOGGER.warning("[SESSION:" + sessionId + "] Failed to send message ID: " + message.getId() +
                                         ", error: " + failure.getMessage());
                        }
                    );

                } catch (Exception e) {
                    LOGGER.severe("[SESSION:" + sessionId + "] Error processing message ID: " + message.getId() +
                                ", error: " + e.getMessage());
                }
            }
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Error polling for messages: " + e.getMessage());
        }
    }
}
