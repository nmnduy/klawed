package com.filesurf.service;

import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.model.ChatConstants;
import com.filesurf.service.FileChatService;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.quarkus.scheduler.Scheduled;
import io.quarkus.websockets.next.WebSocketConnection;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import jakarta.inject.Inject;
import jakarta.transaction.Transactional;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;

@ApplicationScoped
public class ChatMessagePollingService {

    private static final Logger LOGGER = Logger.getLogger(ChatMessagePollingService.class.getName());
    private final ObjectMapper objectMapper = new ObjectMapper();
    
    @Inject
    FileChatService fileChatService;
    
    @ConfigProperty(name = "klawed.sqlite-queue.sender-name", defaultValue = "client")
    String senderName;
    
    @ConfigProperty(name = "klawed.sqlite-queue.receiver-name", defaultValue = "klawed")
    String receiverName;
    
    // Klawed messages directory (where DB files are stored)
    @ConfigProperty(name = "klawed.sqlite-queue.db-dir", defaultValue = "./data/klawed-messages")
    String sqliteQueueDbDir;
    
    // Store active WebSocket connections by session ID
    private final Map<String, WebSocketConnection> activeConnections = new ConcurrentHashMap<>();
    
    // Control flag for polling loop
    private final AtomicBoolean pollingActive = new AtomicBoolean(true);

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
        
        // Determine SQLite database path (in separate messages directory)
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        Path sqliteDbPath = Path.of(sqliteQueueDbDir).resolve(dbFileName);
        
        // Create SQLiteQueueClient with proper configuration
        SQLiteQueueClient.Config config = new SQLiteQueueClient.Config(sqliteDbPath.toString())
            .withSenderName(senderName)
            .withReceiverName(receiverName)
            .withSessionId(sessionId)
            .withFileChatService(fileChatService);
        
        SQLiteQueueClient queueClient = new SQLiteQueueClient(config);
        
        try {
            // Connect and initialize schema
            queueClient.connect();
            
            // Send message
            queueClient.sendMessage(message);
            
            LOGGER.info("[SESSION:" + sessionId + "] Message sent to klawed (length: " + message.length() + " chars)");
        } finally {
            // Clean up
            queueClient.shutdown();
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
     */
    @Scheduled(every = "0.3s")
    @ActivateRequestContext
    public void pollAndSendUnsentMessages() {
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
        
        // Don't log every poll - only log when we find messages (below)
        
        // Poll for unsent messages only for sessions with active connections
        for (Map.Entry<String, WebSocketConnection> entry : activeConnections.entrySet()) {
            String sessionId = entry.getKey();
            WebSocketConnection connection = entry.getValue();
            
            // Double-check connection is still open
            if (connection.isClosed()) {
                continue;
            }
            
            try {
                List<ChatMessageRecord> unsentMessages = fileChatService.findUnsentMessagesForSession(sessionId);
                
                if (!unsentMessages.isEmpty()) {
                    LOGGER.fine("[SESSION:" + sessionId + "] Found " + unsentMessages.size() + " unsent messages");
                }
                
                for (ChatMessageRecord message : unsentMessages) {
                    try {
                        // Create appropriate message based on message type
                        String jsonMessage = createJsonMessage(message);
                        
                        // Send the message asynchronously
                        connection.sendText(jsonMessage).subscribe().with(
                            success -> {
                                LOGGER.info("[SESSION:" + sessionId + "] Sent message ID: " + message.getId());
                                markMessageAsSent(message.getId());
                            },
                            failure -> {
                                LOGGER.warning("[SESSION:" + sessionId + "] Failed to send message ID: " + message.getId() + 
                                             ", error: " + failure.getMessage());
                            }
                        );
                        
                    } catch (Exception e) {
                        LOGGER.severe("[SESSION:" + sessionId + "] Error processing message ID: " + message.getId() + ", error: " + e.getMessage());
                    }
                }
            } catch (Exception e) {
                LOGGER.severe("[SESSION:" + sessionId + "] Error polling for messages: " + e.getMessage());
            }
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
    public void markMessageAsSent(Long messageId) {
        fileChatService.markMessageAsSent(messageId);
        LOGGER.fine("Message " + messageId + " marked as sent in database");
    }

    /**
     * Poll for unsent messages for a specific session.
     * Called when a WebSocket connection is established to catch up on any missed messages.
     */
    @ActivateRequestContext
    public void pollAndSendUnsentMessagesForSession(String sessionId) {
        LOGGER.fine("[SESSION:" + sessionId + "] Polling for unsent messages...");
        
        try {
            // Get unsent messages for this session
            List<ChatMessageRecord> unsentMessages = fileChatService.findUnsentMessagesForSession(sessionId);
            
            if (!unsentMessages.isEmpty()) {
                LOGGER.fine("[SESSION:" + sessionId + "] Found " + unsentMessages.size() + " unsent messages");
            }
            
            WebSocketConnection connection = activeConnections.get(sessionId);
            if (connection == null || connection.isClosed()) {
                LOGGER.fine("[SESSION:" + sessionId + "] No active WebSocket connection, skipping poll");
                return;
            }
            
            for (ChatMessageRecord message : unsentMessages) {
                try {
                    // Create appropriate message based on message type
                    String jsonMessage = createJsonMessage(message);
                    
                    // Send the message asynchronously
                    connection.sendText(jsonMessage).subscribe().with(
                        success -> {
                            LOGGER.info("[SESSION:" + sessionId + "] Sent message ID: " + message.getId());
                            markMessageAsSent(message.getId());
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
            LOGGER.severe("[SESSION:" + sessionId + "] Error in session polling: " + e.getMessage());
        }
    }
}