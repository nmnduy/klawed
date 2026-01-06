package com.filesurf.websocket;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.filesurf.SessionResource;
import com.filesurf.model.ChatSessionRecord;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatConstants;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.service.KlawedAgentManager;
import com.filesurf.service.SessionManager;
import com.filesurf.service.ChatMessagePollingService;
import com.filesurf.service.FileChatService;
import com.filesurf.service.SessionCleanupJobService;
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

@WebSocket(path = "/invoice-chat/ws/{sessionId}")
public class FileChatWebSocket {

    private static final Logger LOGGER = Logger.getLogger(FileChatWebSocket.class.getName());
    private final ObjectMapper objectMapper = createObjectMapper();
    private static final String USER_COOKIE_NAME = "userId";

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
    KlawedAgentManager agentManager;
    
    @Inject
    SessionManager sessionManager;

    @Inject
    SessionCleanupJobService cleanupJobService;

    @Inject
    ChatMessagePollingService chatMessagePollingService;

    @Inject
    FileChatService invoiceChatService;

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
        ChatSessionRecord chatSession = invoiceChatService.createOrUpdateChatSession(sessionId, clientIdentity);
        LOGGER.info("[SESSION:" + sessionId + "] Chat session created/updated in database");

        // Cancel any pending cleanup jobs for this session since it's being resumed
        cleanupJobService.cancelCleanupJobs(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Cleanup jobs cancelled (if any)");

        // Register connection with polling service
        chatMessagePollingService.registerConnection(sessionId, connection);

        // Check for and resend any unsent messages from previous connection
        chatMessagePollingService.pollAndSendUnsentMessagesForSession(sessionId);

        try {
            // Initialize session directory with persistent folders
            Path sessionDir = sessionManager.initializeSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] Session directory initialized: " + sessionDir);

            // Start dedicated klawed agent for this session (will return existing if already started)
            KlawedAgentManager.KlawedAgentInstance agent = agentManager.startAgentForSession(sessionId, sessionDir);
            LOGGER.info("[SESSION:" + sessionId + "] Dedicated klawed agent started or retrieved");

            // Connect to the agent
            agent.connect();
            LOGGER.info("[SESSION:" + sessionId + "] Connected to klawed agent");

            try {
                String statusMessage = "SESSION_ID:" + sessionId + "|Connected";
                // Save status message to database for poller to handle resending if needed
                try {
                    ChatMessageRecord statusDbMessage = invoiceChatService.createChatMessage(sessionId, ChatConstants.AGENT, ChatConstants.CLIENT, statusMessage, "status");
                    LOGGER.info("[SESSION:" + sessionId + "] Status message saved to database with ID: " + statusDbMessage.getId());
                } catch (Exception dbEx) {
                    LOGGER.warning("[SESSION:" + sessionId + "] Failed to save status message to database: " + dbEx.getMessage());
                }
                return objectMapper.writeValueAsString(KlawedSocketMessage.createStatus(statusMessage));
            } catch (Exception e) {
                LOGGER.severe("Failed to serialize status message: " + e.getMessage());
                return "{\"messageType\":\"STATUS\",\"content\":\"Connected\"}";
            }

        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to initialize session: " + e.getMessage());
            e.printStackTrace();
            try {
                return objectMapper.writeValueAsString(KlawedSocketMessage.createError(
                    "Failed to initialize session: " + e.getMessage()
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
            ChatMessageRecord clientMessage = invoiceChatService.createChatMessage(sessionId, ChatConstants.CLIENT, ChatConstants.AGENT, message, ChatConstants.DB_MESSAGE_TYPE_TEXT);
            LOGGER.info("[SESSION:" + sessionId + "] Incoming message saved to database with ID: " + clientMessage.getId());
            // Mark client message as sent immediately since it was successfully received by server
            invoiceChatService.markMessageAsSent(clientMessage.getId());
            LOGGER.info("[SESSION:" + sessionId + "] Client message marked as sent");
        } catch (Exception dbEx) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to save incoming message to database: " + dbEx.getMessage());
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

        try {
            // Try to get existing agent first
            KlawedAgentManager.KlawedAgentInstance agent = agentManager.getAgentForSession(sessionId);

            if (agent == null) {
                // If no agent exists, create a new one
                Path sessionDir;
                try {
                    sessionDir = sessionManager.getSessionDirectory(sessionId, userId);
                } catch (IOException e) {
                    // If session directory doesn't exist, create it
                    sessionDir = sessionManager.initializeSession(sessionId, userId);
                }
                agent = agentManager.startAgentForSession(sessionId, sessionDir);
                LOGGER.info("[SESSION:" + sessionId + "] Created new agent instance for message processing");

                // Connect the agent
                agent.connect();
            } else {
                LOGGER.info("[SESSION:" + sessionId + "] Using existing agent instance for message processing");
            }
            
            LOGGER.info("[SESSION:" + sessionId + "] Agent created and connected, sending message to klawed");
            
            // Send message to the dedicated agent asynchronously
            // Responses will be delivered via ChatMessagePollingService
            agent.sendMessageAsync(message);
            LOGGER.info("[SESSION:" + sessionId + "] Message sent to klawed, responses will be delivered via polling service");
            
        } catch (Exception e) {
            String errorMsg = "Error communicating with klawed agent: " + e.getMessage();
            LOGGER.severe("[SESSION:" + sessionId + "] " + errorMsg);
            LOGGER.severe("[SESSION:" + sessionId + "] Stack trace: " + e.getMessage());
            e.printStackTrace();
            LOGGER.info("[SESSION:" + sessionId + "] Saving error to database for poller to send");
            try {
                ChatMessageRecord errorMessage = invoiceChatService.createChatMessage(sessionId, ChatConstants.AGENT, ChatConstants.CLIENT, errorMsg, ChatConstants.DB_MESSAGE_TYPE_ERROR);
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

        // Stop the dedicated klawed agent
        agentManager.stopAgentForSession(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Klawed agent stopped");

        // Persist session folders back to per-user storage
        try {
            LOGGER.info("[SESSION:" + sessionId + "] WebSocket onClose: Persisting session data for user=" + userId);
            sessionManager.persistSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] WebSocket onClose: Session data persisted for user=" + userId);
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] WebSocket onClose: Failed to persist session data: " + e.getMessage());
            e.printStackTrace();
        }

        // Schedule cleanup instead of immediate delete to avoid race with persistence
        cleanupJobService.enqueueCleanup(sessionId);
        sessionManager.releaseSessionTracking(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Session cleanup enqueued");

        // Remove session from session store
        SessionResource.removeSession(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Session removed from session store");

        // Deactivate chat session in database
        invoiceChatService.deactivateChatSession(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Chat session deactivated in database");

        // Unregister connection from polling service
        chatMessagePollingService.unregisterConnection(sessionId);
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
            
            // Stop the dedicated klawed agent if it exists
            try {
                agentManager.stopAgentForSession(sessionId);
                LOGGER.info("[SESSION:" + sessionId + "] Klawed agent stopped");
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to stop agent (may not exist): " + e.getMessage());
            }
            
            // Persist session folders back to per-user storage
            LOGGER.info("[SESSION:" + sessionId + "] Conclude: Persisting session data for user=" + userId);
            sessionManager.persistSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] Conclude: Session data persisted for user=" + userId);
            
            // Clean up session directory immediately (not scheduled)
            try {
                sessionManager.cleanupSession(sessionId);
                LOGGER.info("[SESSION:" + sessionId + "] Session directory cleaned up");
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to cleanup session directory: " + e.getMessage());
            }
            
            // Remove session from session store
            SessionResource.removeSession(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Session removed from session store");
            
            // Deactivate chat session in database (don't delete messages)
            invoiceChatService.deactivateChatSession(sessionId);
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
    


    
}