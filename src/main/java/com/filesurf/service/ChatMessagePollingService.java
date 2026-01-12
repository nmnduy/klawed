package com.filesurf.service;

import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.model.ChatConstants;
import com.filesurf.service.FileChatService;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.quarkus.scheduler.Scheduled;
import io.quarkus.websockets.next.WebSocketConnection;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import jakarta.inject.Inject;
import jakarta.transaction.Transactional;
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
    
    // Store active WebSocket connections by session ID
    private final Map<String, WebSocketConnection> activeConnections = new ConcurrentHashMap<>();
    
    // Control flag for polling loop
    private final AtomicBoolean pollingActive = new AtomicBoolean(true);

    /**
     * Register a WebSocket connection for a session
     */
    public void registerConnection(String sessionId, WebSocketConnection connection) {
        activeConnections.put(sessionId, connection);
        LOGGER.fine("[SESSION:" + sessionId + "] Connection registered for polling");
    }
    
    /**
     * Unregister a WebSocket connection for a session
     */
    public void unregisterConnection(String sessionId) {
        activeConnections.remove(sessionId);
        LOGGER.fine("[SESSION:" + sessionId + "] Connection unregistered from polling");
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
     * Async polling loop for unsent messages every 300ms
     * Uses SQLite-friendly transaction handling
     */
    @Scheduled(every = "0.3s")
    @ActivateRequestContext
    public void pollAndSendUnsentMessages() {
        if (!pollingActive.get()) {
            LOGGER.fine("Polling is paused");
            return;
        }
        
        LOGGER.fine("Polling for unsent messages...");
        
        try {
            // Get unsent messages
            List<ChatMessageRecord> unsentMessages = fileChatService.findUnsentMessages();
        
            LOGGER.fine("Found " + unsentMessages.size() + " unsent messages");
            
            for (ChatMessageRecord message : unsentMessages) {
                try {
                    String sessionId = message.getSessionStringId();
                    
                    // Check if there's an active WebSocket connection for this session
                    WebSocketConnection connection = activeConnections.get(sessionId);
                    if (connection != null && !connection.isClosed()) {
                        // Create appropriate message based on message type
                        String jsonMessage;
                        if (ChatConstants.DB_MESSAGE_TYPE_ERROR.equals(message.getMessageType())) {
                            jsonMessage = objectMapper.writeValueAsString(
                                KlawedSocketMessage.createError(message.getContent())
                            );
                        } else if ("status".equals(message.getMessageType())) {
                            // Check if it's an END_AI_TURN message (stored as status type with JSON content)
                            String content = message.getContent();
                            if (content != null && content.contains("END_AI_TURN")) {
                                try {
                                    Map<String, Object> endTurnData = objectMapper.readValue(content, Map.class);
                                    if (ChatConstants.MESSAGE_TYPE_END_AI_TURN.equals(endTurnData.get("messageType"))) {
                                        jsonMessage = objectMapper.writeValueAsString(
                                            KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_END_AI_TURN, null)
                                        );
                                    } else {
                                        jsonMessage = objectMapper.writeValueAsString(
                                            KlawedSocketMessage.createStatus(content)
                                        );
                                    }
                                } catch (Exception e) {
                                    // Not JSON, treat as regular status
                                    jsonMessage = objectMapper.writeValueAsString(
                                        KlawedSocketMessage.createStatus(content)
                                    );
                                }
                            } else {
                                jsonMessage = objectMapper.writeValueAsString(
                                    KlawedSocketMessage.createStatus(content)
                                );
                            }
                        } else if (ChatConstants.DB_MESSAGE_TYPE_API_CALL.equals(message.getMessageType())) {
                            // Parse the API call JSON and create an API_CALL message
                            try {
                                Map<String, Object> apiCallData = objectMapper.readValue(message.getContent(), Map.class);
                                jsonMessage = objectMapper.writeValueAsString(
                                    KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_API_CALL, apiCallData)
                                );
                            } catch (Exception e) {
                                LOGGER.warning("Failed to parse API_CALL message: " + e.getMessage());
                                // Fall back to status message
                                jsonMessage = objectMapper.writeValueAsString(
                                    KlawedSocketMessage.createStatus("AI is thinking")
                                );
                            }
                        } else {
                            // Default to text message
                            jsonMessage = objectMapper.writeValueAsString(
                                KlawedSocketMessage.createText(message.getContent())
                            );
                        }
                        
                        // Send the message asynchronously
                        connection.sendText(jsonMessage).subscribe().with(
                            success -> {
                                LOGGER.info("[SESSION:" + sessionId + "] Resent unsent message ID: " + message.getId());
                                // Mark as sent
                                markMessageAsSent(message.getId());
                            },
                            failure -> {
                                LOGGER.warning("[SESSION:" + sessionId + "] Failed to resend message ID: " + message.getId() + 
                                             ", error: " + failure.getMessage());
                            }
                        );
                    } else {
                        LOGGER.fine("[SESSION:" + sessionId + "] No active WebSocket connection for unsent message ID: " + message.getId());
                        // Remove stale connection from map
                        if (connection != null && connection.isClosed()) {
                            activeConnections.remove(sessionId);
                            LOGGER.fine("[SESSION:" + sessionId + "] Stale connection removed from polling map");
                        }
                    }
                    
                } catch (Exception e) {
                    LOGGER.severe("Error processing unsent message ID: " + message.getId() + ", error: " + e.getMessage());
                }
            }
        } catch (Exception e) {
            LOGGER.severe("Error in polling loop: " + e.getMessage());
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
     * Poll for unsent messages for a specific session
     */
    @ActivateRequestContext
    public void pollAndSendUnsentMessagesForSession(String sessionId) {
        LOGGER.fine("[SESSION:" + sessionId + "] Polling for unsent messages...");
        
        try {
            // Get unsent messages for this session
            List<ChatMessageRecord> unsentMessages = fileChatService.findUnsentMessagesForSession(sessionId);
            
            LOGGER.fine("[SESSION:" + sessionId + "] Found " + unsentMessages.size() + " unsent messages");
            
            for (ChatMessageRecord message : unsentMessages) {
                try {
                    // Check if there's an active WebSocket connection for this session
                    WebSocketConnection connection = activeConnections.get(sessionId);
                    if (connection != null && !connection.isClosed()) {
                        // Create appropriate message based on message type
                        String jsonMessage;
                        if (ChatConstants.DB_MESSAGE_TYPE_ERROR.equals(message.getMessageType())) {
                            jsonMessage = objectMapper.writeValueAsString(
                                KlawedSocketMessage.createError(message.getContent())
                            );
                        } else if ("status".equals(message.getMessageType())) {
                            // Check if it's an END_AI_TURN message (stored as status type with JSON content)
                            String content = message.getContent();
                            if (content != null && content.contains("END_AI_TURN")) {
                                try {
                                    Map<String, Object> endTurnData = objectMapper.readValue(content, Map.class);
                                    if (ChatConstants.MESSAGE_TYPE_END_AI_TURN.equals(endTurnData.get("messageType"))) {
                                        jsonMessage = objectMapper.writeValueAsString(
                                            KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_END_AI_TURN, null)
                                        );
                                    } else {
                                        jsonMessage = objectMapper.writeValueAsString(
                                            KlawedSocketMessage.createStatus(content)
                                        );
                                    }
                                } catch (Exception e) {
                                    // Not JSON, treat as regular status
                                    jsonMessage = objectMapper.writeValueAsString(
                                        KlawedSocketMessage.createStatus(content)
                                    );
                                }
                            } else {
                                jsonMessage = objectMapper.writeValueAsString(
                                    KlawedSocketMessage.createStatus(content)
                                );
                            }
                        } else if (ChatConstants.DB_MESSAGE_TYPE_API_CALL.equals(message.getMessageType())) {
                            // Parse the API call JSON and create an API_CALL message
                            try {
                                Map<String, Object> apiCallData = objectMapper.readValue(message.getContent(), Map.class);
                                jsonMessage = objectMapper.writeValueAsString(
                                    KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_API_CALL, apiCallData)
                                );
                            } catch (Exception e) {
                                LOGGER.warning("Failed to parse API_CALL message: " + e.getMessage());
                                // Fall back to status message
                                jsonMessage = objectMapper.writeValueAsString(
                                    KlawedSocketMessage.createStatus("AI is thinking")
                                );
                            }
                        } else {
                            // Default to text message
                            jsonMessage = objectMapper.writeValueAsString(
                                KlawedSocketMessage.createText(message.getContent())
                            );
                        }
                        
                        // Send the message asynchronously
                        connection.sendText(jsonMessage).subscribe().with(
                            success -> {
                                LOGGER.info("[SESSION:" + sessionId + "] Resent unsent message ID: " + message.getId());
                                // Mark as sent
                                markMessageAsSent(message.getId());
                            },
                            failure -> {
                                LOGGER.warning("[SESSION:" + sessionId + "] Failed to resend message ID: " + message.getId() + 
                                             ", error: " + failure.getMessage());
                            }
                        );
                    } else {
                        LOGGER.fine("[SESSION:" + sessionId + "] No active WebSocket connection for unsent message ID: " + message.getId());
                        // Remove stale connection from map
                        if (connection != null && connection.isClosed()) {
                            activeConnections.remove(sessionId);
                            LOGGER.fine("[SESSION:" + sessionId + "] Stale connection removed from polling map");
                        }
                    }
                    
                } catch (Exception e) {
                    LOGGER.severe("[SESSION:" + sessionId + "] Error processing unsent message ID: " + message.getId() + 
                                ", error: " + e.getMessage());
                }
            }
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Error in session polling: " + e.getMessage());
        }
    }
}