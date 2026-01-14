package com.filesurf.websocket;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.filesurf.SessionResource;
import com.filesurf.model.ChatSessionRecord;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatConstants;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.service.KlawedSandboxService;
import com.filesurf.service.SessionManager;
import com.filesurf.service.ChatMessagePollingService;
import com.filesurf.service.FileChatService;
import io.quarkus.websockets.next.OnOpen;
import io.quarkus.websockets.next.OnTextMessage;
import io.quarkus.websockets.next.OnClose;
import io.quarkus.websockets.next.WebSocket;
import io.quarkus.websockets.next.WebSocketConnection;
import io.quarkus.websockets.next.HandshakeRequest;
import io.smallrye.mutiny.Uni;
import jakarta.inject.Inject;
import jakarta.transaction.Transactional;
import jakarta.enterprise.context.control.ActivateRequestContext;

import java.nio.file.Path;
import java.util.List;
import java.util.logging.Logger;
import java.io.IOException;

@WebSocket(path = "/file-chat/ws/{sessionId}")
public class FileChatWebSocket {

    private static final Logger LOGGER = Logger.getLogger(FileChatWebSocket.class.getName());
    private final ObjectMapper objectMapper = createObjectMapper();
    private static final String USER_COOKIE_NAME = "filesurf_userId";

    /**
     * Create and configure ObjectMapper with JavaTimeModule
     */
    private ObjectMapper createObjectMapper() {
        ObjectMapper mapper = new ObjectMapper();
        mapper.registerModule(new JavaTimeModule());
        return mapper;
    }

    private String parseCookie(String cookieHeader, String name) {
        if (cookieHeader == null || name == null) return null;
        String[] parts = cookieHeader.split(";\\s*");
        for (String part : parts) {
            int eq = part.indexOf('=');
            if (eq > 0) {
                String k = part.substring(0, eq).trim();
                if (k.equals(name)) {
                    return part.substring(eq + 1).trim();
                }
            }
        }
        return null;
    }

    @Inject
    KlawedSandboxService klawedSandboxService;
    
    @Inject
    SessionManager sessionManager;

    @Inject
    ChatMessagePollingService chatMessagePollingService;

    @Inject
    FileChatService fileChatService;

    @Inject
    com.filesurf.service.MetricsService metricsService;

    @OnOpen
    @ActivateRequestContext
    public String onOpen(WebSocketConnection connection) {
        // Get session ID from path parameter
        String sessionId = connection.pathParam("sessionId");

        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("WebSocket connection rejected: No session ID provided in query parameter");
            try {
                return objectMapper.writeValueAsString(KlawedSocketMessage.createError(
                    "No session ID provided. Please generate a session first by calling /session/generate"
                ));
            } catch (Exception e) {
                LOGGER.severe("Failed to serialize error message: " + e.getMessage());
                return "{\"messageType\":\"ERROR\",\"content\":\"No session ID provided\"}";
            }
        }

        // Validate session ID
        if (!SessionResource.validateSession(sessionId)) {
            LOGGER.severe("WebSocket connection rejected: Invalid session ID: " + sessionId);
            try {
                return objectMapper.writeValueAsString(KlawedSocketMessage.createError(
                    "Invalid session ID. Please generate a new session by calling /session/generate"
                ));
            } catch (Exception e) {
                LOGGER.severe("Failed to serialize error message: " + e.getMessage());
                return "{\"messageType\":\"ERROR\",\"content\":\"Invalid session ID\"}";
            }
        }

        // Determine user ID from cookies (HandshakeRequest); reject if absent
        String userId = null;
        HandshakeRequest handshake = connection.handshakeRequest();
        if (handshake != null) {
            var cookieHeaders = handshake.headers("Cookie");
            if (cookieHeaders != null) {
                for (String cookieHeader : cookieHeaders) {
                    String maybe = parseCookie(cookieHeader, USER_COOKIE_NAME);
                    if (maybe != null && !maybe.isBlank()) {
                        userId = maybe;
                        break;
                    }
                }
            }
        }
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] WebSocket connection rejected: No userId cookie provided");
            try {
                return objectMapper.writeValueAsString(KlawedSocketMessage.createError(
                    "No user ID provided. Please generate a session via /session/generate to obtain user cookie."
                ));
            } catch (Exception e) {
                LOGGER.severe("Failed to serialize error message: " + e.getMessage());
                return "{\"messageType\":\"ERROR\",\"content\":\"No user ID provided\"}";
            }
        }

        LOGGER.info("[SESSION:" + sessionId + "] Using userId from cookie: " + userId);

        // Get client identity for this session
        String clientIdentity = SessionResource.getClientIdentity(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] WebSocket connection opened for client: " + clientIdentity);

        // Create or update chat session in database
        ChatSessionRecord chatSession = fileChatService.createOrUpdateChatSession(sessionId, clientIdentity);
        LOGGER.info("[SESSION:" + sessionId + "] Chat session created/updated in database");

        // Register session with KlawedSandboxService
        klawedSandboxService.registerSession(sessionId, userId);
        LOGGER.info("[SESSION:" + sessionId + "] Session registered with KlawedSandboxService");

        // Register connection with polling service
        chatMessagePollingService.registerConnection(sessionId, connection);

        // Check for and resend any unsent messages from previous connection
        chatMessagePollingService.pollAndSendUnsentMessagesForSession(sessionId);

        try {
            // Initialize session directory with persistent folders
            Path sessionDir = sessionManager.initializeSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] Session directory initialized: " + sessionDir);

            try {
                String statusMessage = "SESSION_ID:" + sessionId + "|Connected";
                // Save status message to database for poller to handle resending if needed
                try {
                    ChatMessageRecord statusDbMessage = fileChatService.createChatMessage(sessionId, ChatConstants.AGENT, ChatConstants.CLIENT, statusMessage, "status");
                    LOGGER.info("[SESSION:" + sessionId + "] Status message saved to database with ID: " + statusDbMessage.getId());
                } catch (Exception dbEx) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to save status message to database: " + dbEx.getMessage());
                }
                // Track metrics for successful WebSocket connection
                metricsService.incrementWebSocketConnections();
                metricsService.incrementChatSessions();
                metricsService.trackUserActivity(userId);
                
                return objectMapper.writeValueAsString(KlawedSocketMessage.createStatus(statusMessage));
            } catch (Exception e) {
                LOGGER.severe("Failed to serialize status message: " + e.getMessage());
                // Track error in metrics
                metricsService.incrementErrors("websocket_serialization");
                return "{\"messageType\":\"STATUS\",\"content\":\"Connected\"}";
            }

        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to initialize session: " + e.getMessage());
            e.printStackTrace();
            
            // Track error metrics
            metricsService.incrementErrors("session_initialization");
            
            try {
                // Do not forward internal details to the client
                return objectMapper.writeValueAsString(KlawedSocketMessage.createError(
                    "Failed to initialize session"
                ));
            } catch (Exception jsonEx) {
                LOGGER.severe("Failed to serialize error message: " + jsonEx.getMessage());
                return "{\"messageType\":\"ERROR\",\"content\":\"Failed to initialize session\"}";
            }
        }
    }

    @OnTextMessage
    @ActivateRequestContext
    public void onMessage(String message, WebSocketConnection connection) {
        // Get session ID from path parameter
        String sessionId = connection.pathParam("sessionId");

        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("Received message without session ID");
            try {
                connection.sendText(objectMapper.writeValueAsString(
                    KlawedSocketMessage.createError("No session ID provided")
                )).subscribe().with(
                    success -> LOGGER.info("Error message sent successfully"),
                    failure -> LOGGER.severe("Failed to send error message: " + failure.getMessage())
                );
            } catch (Exception e) {
                LOGGER.severe("Failed to send error message: " + e.getMessage());
            }
            return;
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] Received message from client: " + 
                   message.substring(0, Math.min(100, message.length())) + 
                   (message.length() > 100 ? "..." : ""));
        LOGGER.info("[SESSION:" + sessionId + "] Full message length: " + message.length() + " chars");
        
        // Check for special commands
        if (message != null && !message.trim().isEmpty()) {
            String trimmedMessage = message.trim();
            
            // Handle conclude session command
            if ("/conclude".equalsIgnoreCase(trimmedMessage) || "/conclude session".equalsIgnoreCase(trimmedMessage)) {
                handleConcludeCommand(sessionId, connection);
                return;
            }
        }
        
        // Save incoming message to database and mark as sent immediately
        // (client-to-agent messages don't need to be sent via WebSocket)
        try {
            ChatMessageRecord clientMessage = fileChatService.createChatMessage(sessionId, ChatConstants.CLIENT, ChatConstants.AGENT, message, ChatConstants.DB_MESSAGE_TYPE_TEXT);
            LOGGER.info("[SESSION:" + sessionId + "] Incoming message saved to database with ID: " + clientMessage.getId());
            // Mark client message as sent immediately since it was successfully received by server
            fileChatService.markMessageAsSent(clientMessage.getId());
            LOGGER.info("[SESSION:" + sessionId + "] Client message marked as sent");
        } catch (Exception dbEx) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to save incoming message to database: " + dbEx.getMessage());
            // Track error in metrics
            metricsService.incrementErrors("database_save");
        }
        
        // Determine user ID from cookies (HandshakeRequest); if absent, fail message handling
        String userId = null;
        HandshakeRequest handshake = connection.handshakeRequest();
        if (handshake != null) {
            var cookieHeaders = handshake.headers("Cookie");
            if (cookieHeaders != null) {
                for (String cookieHeader : cookieHeaders) {
                    String maybe = parseCookie(cookieHeader, USER_COOKIE_NAME);
                    if (maybe != null && !maybe.isBlank()) {
                        userId = maybe;
                        break;
                    }
                }
            }
        }
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on message; rejecting.");
            try {
                connection.sendText(objectMapper.writeValueAsString(
                    KlawedSocketMessage.createError("No user ID cookie. Please regenerate session.")))
                    .subscribe();
            } catch (Exception e) {
                LOGGER.severe("Failed to send missing userId error: " + e.getMessage());
            }
            return;
        }
        
        // Track metrics for chat message
        metricsService.incrementChatMessages();
        metricsService.trackUserActivity(userId);

        try {
            LOGGER.info("[SESSION:" + sessionId + "] Sending message to klawed via ChatMessagePollingService");
            
            // Send message to klawed via SQLite queue
            chatMessagePollingService.sendMessageToKlawed(sessionId, userId, message);
            LOGGER.info("[SESSION:" + sessionId + "] Message sent to klawed, responses will be delivered via polling service");
            
        } catch (Exception e) {
            String errorMsg = "Error communicating with klawed agent: " + e.getMessage();
            LOGGER.severe("[SESSION:" + sessionId + "] " + errorMsg);
            LOGGER.severe("[SESSION:" + sessionId + "] Stack trace: " + e.getMessage());
            e.printStackTrace();
            LOGGER.info("[SESSION:" + sessionId + "] Saving error to database for poller to send");
            try {
                ChatMessageRecord errorMessage = fileChatService.createChatMessage(sessionId, ChatConstants.AGENT, ChatConstants.CLIENT, errorMsg, ChatConstants.DB_MESSAGE_TYPE_ERROR);
                LOGGER.info("[SESSION:" + sessionId + "] Error message saved to database with ID: " + errorMessage.getId());
            } catch (Exception dbEx) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to save error message to database: " + dbEx.getMessage());
            }
        }
    }

    @OnClose
    @ActivateRequestContext
    public void onClose(WebSocketConnection connection) {
        // Get session ID from path parameter (same as in onOpen)
        String sessionId = connection.pathParam("sessionId");
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("WebSocket connection closed without session ID");
            return;
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] WebSocket connection closed");

        // Determine user ID from cookies (HandshakeRequest); if absent, log and skip persistence
        String userId = null;
        HandshakeRequest handshake = connection.handshakeRequest();
        if (handshake != null) {
            var cookieHeaders = handshake.headers("Cookie");
            if (cookieHeaders != null) {
                for (String cookieHeader : cookieHeaders) {
                    String maybe = parseCookie(cookieHeader, USER_COOKIE_NAME);
                    if (maybe != null && !maybe.isBlank()) {
                        userId = maybe;
                        break;
                    }
                }
            }
        }
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on close; skipping persistence.");
        } else {
            LOGGER.info("[SESSION:" + sessionId + "] Closing connection with userId=" + userId);
        }

        // Unregister session from KlawedSandboxService
        klawedSandboxService.unregisterSession(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Session unregistered from KlawedSandboxService");

        // Persist session folders back to per-user storage
        try {
            LOGGER.info("[SESSION:" + sessionId + "] WebSocket onClose: Persisting session data for user=" + userId);
            sessionManager.persistSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] WebSocket onClose: Session data persisted for user=" + userId);
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] WebSocket onClose: Failed to persist session data: " + e.getMessage());
            e.printStackTrace();
        }

        // DON'T release session tracking, remove from store, or deactivate on temporary WebSocket close
        // These should only happen on explicit session conclusion (/conclude command)
        // This allows the user to reconnect and resume their session seamlessly
        
        // DO unregister from polling service to stop sending messages to closed WebSocket
        chatMessagePollingService.unregisterConnection(sessionId);
        
        // DO update metrics for WebSocket connection closure (but not session metrics - session is still active)
        metricsService.decrementWebSocketConnections();
        LOGGER.info("[SESSION:" + sessionId + "] WebSocket closed, session remains active for reconnection");
    }
    

    
    /**
     * Handle conclude session command
     */
    private void handleConcludeCommand(String sessionId, WebSocketConnection connection) {
        LOGGER.info("[SESSION:" + sessionId + "] Handling conclude session command");
        
        try {
            // Get user ID from cookies
            String userId = null;
            HandshakeRequest handshake = connection.handshakeRequest();
            if (handshake != null) {
                var cookieHeaders = handshake.headers("Cookie");
                if (cookieHeaders != null) {
                    for (String cookieHeader : cookieHeaders) {
                        String maybe = parseCookie(cookieHeader, USER_COOKIE_NAME);
                        if (maybe != null && !maybe.isBlank()) {
                            userId = maybe;
                            break;
                        }
                    }
                }
            }
            
            if (userId == null || userId.isBlank()) {
                LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on conclude command");
                connection.sendText(objectMapper.writeValueAsString(
                    KlawedSocketMessage.createError("No user ID cookie. Cannot conclude session.")
                )).subscribe();
                return;
            }
            
            // Get workspace path before cleanup
            Path workspace = sessionManager.getWorkspaceForSession(sessionId);
            
            // Clean up klawed artifacts from workspace (.klawed/, SQLite queue files)
            if (workspace != null) {
                cleanupKlawedArtifacts(sessionId, workspace);
            }
            
            // Persist session folders back to per-user storage
            LOGGER.info("[SESSION:" + sessionId + "] Conclude: Persisting session data for user=" + userId);
            sessionManager.persistSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] Conclude: Session data persisted for user=" + userId);
            
            // Release session tracking
            sessionManager.releaseSessionTracking(sessionId);
            
            // Remove session from session store
            SessionResource.removeSession(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Session removed from session store");
            
            // Deactivate chat session in database (don't delete messages)
            fileChatService.deactivateChatSession(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Chat session deactivated in database");
            
            // Send success response
            String response = "Session concluded. Persistent files saved and temporary directory cleaned up.";
            connection.sendText(objectMapper.writeValueAsString(
                KlawedSocketMessage.createText(response)
            )).subscribe().with(
                success -> LOGGER.info("[SESSION:" + sessionId + "] Conclude command response sent"),
                failure -> LOGGER.severe("[SESSION:" + sessionId + "] Failed to send conclude command response: " + failure.getMessage())
            );
            
            // Close the WebSocket connection since session is concluded
            connection.close();
            
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to conclude session: " + e.getMessage());
            try {
                connection.sendText(objectMapper.writeValueAsString(
                    KlawedSocketMessage.createError("Failed to conclude session: " + e.getMessage())
                )).subscribe();
            } catch (Exception sendEx) {
                LOGGER.severe("[SESSION:" + sessionId + "] Failed to send error response: " + sendEx.getMessage());
            }
        }
    }
    
    /**
     * Clean up klawed artifacts from the user's workspace.
     * This includes:
     * - .klawed/ directory (logs, config)
     * - klawed_messages_{sessionId}.db and related WAL files
     */
    private void cleanupKlawedArtifacts(String sessionId, Path workspace) {
        LOGGER.info("[SESSION:" + sessionId + "] Cleaning up klawed artifacts from workspace: " + workspace);
        
        // Delete .klawed/ directory
        Path klawedDir = workspace.resolve(".klawed");
        if (java.nio.file.Files.exists(klawedDir)) {
            try {
                deleteDirectory(klawedDir);
                LOGGER.info("[SESSION:" + sessionId + "] Deleted .klawed/ directory");
            } catch (java.io.IOException e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete .klawed/ directory: " + e.getMessage());
            }
        }
        
        // Delete SQLite queue files (klawed_messages_{sessionId}.db, .db-shm, .db-wal)
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        String[] sqliteExtensions = {"", "-shm", "-wal"};
        
        for (String ext : sqliteExtensions) {
            Path dbFile = workspace.resolve(dbFileName + ext);
            if (java.nio.file.Files.exists(dbFile)) {
                try {
                    java.nio.file.Files.delete(dbFile);
                    LOGGER.info("[SESSION:" + sessionId + "] Deleted " + dbFile.getFileName());
                } catch (java.io.IOException e) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to delete " + dbFile.getFileName() + ": " + e.getMessage());
                }
            }
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] Klawed artifact cleanup completed");
    }
    
    /**
     * Recursively delete a directory.
     */
    private void deleteDirectory(Path dir) throws java.io.IOException {
        if (!java.nio.file.Files.exists(dir)) {
            return;
        }
        try (java.util.stream.Stream<Path> walk = java.nio.file.Files.walk(dir)) {
            walk.sorted((a, b) -> b.compareTo(a)) // reverse order to delete children first
                .forEach(path -> {
                    try {
                        java.nio.file.Files.delete(path);
                    } catch (java.io.IOException e) {
                        LOGGER.warning("Failed to delete: " + path + " - " + e.getMessage());
                    }
                });
        }
    }
}