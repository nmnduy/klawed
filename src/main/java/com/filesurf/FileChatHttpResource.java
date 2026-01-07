package com.filesurf;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.filesurf.model.ChatConstants;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatSessionRecord;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.service.KlawedAgentManager;
import com.filesurf.service.SessionManager;
import com.filesurf.service.SessionCleanupJobService;
import com.filesurf.service.AgentShutdownJobService;
import com.filesurf.service.FileChatService;
import jakarta.inject.Inject;
import jakarta.transaction.Transactional;
import jakarta.ws.rs.*;
import jakarta.ws.rs.core.Context;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.HttpHeaders;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;

import java.io.IOException;
import java.util.List;
import java.util.logging.Logger;

/**
 * HTTP endpoint for file chat AI agent.
 * Provides REST API for AI agents to interact with the file chat agent
 * via HTTP requests instead of WebSocket.
 */
@Path("/file-chat/http")
public class FileChatHttpResource {

    private static final Logger LOGGER = Logger.getLogger(FileChatHttpResource.class.getName());
    private final ObjectMapper objectMapper = createObjectMapper();
    private static final String USER_COOKIE_NAME = "filesurf_userId";

    @Inject
    KlawedAgentManager agentManager;
    
    @Inject
    SessionManager sessionManager;

    @Inject
    SessionCleanupJobService cleanupJobService;
    
    @Inject
    AgentShutdownJobService agentShutdownJobService;

    @Inject
    FileChatService fileChatService;

    /**
     * Initialize or connect to a chat session via HTTP.
     * Similar to WebSocket's onOpen method.
     */
    @POST
    @Path("/session/{sessionId}")
    @Produces(MediaType.APPLICATION_JSON)
    @Transactional
    public Response initializeSession(
            @PathParam("sessionId") String sessionId,
            @Context HttpHeaders headers) {
        
        LOGGER.info("[SESSION:" + sessionId + "] HTTP session initialization requested");

        // Validate session ID
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("HTTP session initialization rejected: No session ID provided");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided. Please generate a session first by calling /session/generate\"}")
                    .build();
        }

        if (!SessionResource.validateSession(sessionId)) {
            LOGGER.severe("HTTP session initialization rejected: Invalid session ID: " + sessionId);
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"Invalid session ID. Please generate a new session by calling /session/generate\"}")
                    .build();
        }

        // Get user ID from cookies
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] HTTP session initialization rejected: No userId cookie provided");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID provided. Please generate a session via /session/generate to obtain user cookie.\"}")
                    .build();
        }

        LOGGER.info("[SESSION:" + sessionId + "] Using userId from cookie: " + userId);

        // Get client identity for this session
        String clientIdentity = SessionResource.getClientIdentity(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] HTTP session initialization for client: " + clientIdentity);

        try {
            // Create or update chat session in database
            ChatSessionRecord chatSession = fileChatService.createOrUpdateChatSession(sessionId, clientIdentity);
            LOGGER.info("[SESSION:" + sessionId + "] Chat session created/updated in database");

            // Cancel any pending cleanup jobs for this session since it's being resumed
            cleanupJobService.cancelCleanupJobs(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Cleanup jobs cancelled (if any)");
            
            // Cancel any pending agent shutdown jobs for this session since it's reconnecting
            agentShutdownJobService.cancelShutdownJob(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Agent shutdown jobs cancelled (if any)");

            // Initialize session directory with persistent folders
            java.nio.file.Path sessionDir = sessionManager.initializeSession(sessionId, userId);
            LOGGER.info("[SESSION:" + sessionId + "] Session directory initialized: " + sessionDir);

            // Check if agent already exists for this session (from previous connection)
            KlawedAgentManager.KlawedAgentInstance agent = agentManager.getAgentForSession(sessionId);
            if (agent != null) {
                LOGGER.info("[SESSION:" + sessionId + "] Reusing existing klawed agent from previous connection");
            } else {
                // Start new dedicated klawed agent for this session
                agent = agentManager.startAgentForSession(sessionId, sessionDir);
                LOGGER.info("[SESSION:" + sessionId + "] Started new dedicated klawed agent");
            }

            // Connect to the agent
            agent.connect();
            LOGGER.info("[SESSION:" + sessionId + "] Connected to klawed agent");

            // Save status message to database
            String statusMessage = "SESSION_ID:" + sessionId + "|Connected via HTTP";
            ChatMessageRecord statusDbMessage = fileChatService.createChatMessage(
                sessionId, ChatConstants.AGENT, ChatConstants.CLIENT, statusMessage, "status");
            LOGGER.info("[SESSION:" + sessionId + "] Status message saved to database with ID: " + statusDbMessage.getId());

            return Response.ok()
                    .entity("{\"status\": \"connected\", \"sessionId\": \"" + sessionId + 
                            "\", \"message\": \"" + statusMessage + "\"}")
                    .build();

        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to initialize session: " + e.getMessage());
            e.printStackTrace();
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to initialize session: " + e.getMessage() + "\"}")
                    .build();
        }
    }

    /**
     * Send a message to the AI agent via HTTP.
     * Similar to WebSocket's onMessage method.
     */
    @POST
    @Path("/message/{sessionId}")
    @Consumes(MediaType.TEXT_PLAIN)
    @Produces(MediaType.APPLICATION_JSON)
    @Transactional
    public Response sendMessage(
            @PathParam("sessionId") String sessionId,
            String message,
            @Context HttpHeaders headers) {
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("Received HTTP message without session ID");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] Received HTTP message from client: " + 
                   message.substring(0, Math.min(100, message.length())) + 
                   (message.length() > 100 ? "..." : ""));
        LOGGER.info("[SESSION:" + sessionId + "] Full message length: " + message.length() + " chars");
        
        // Save incoming message to database and mark as sent immediately
        try {
            ChatMessageRecord clientMessage = fileChatService.createChatMessage(
                sessionId, ChatConstants.CLIENT, ChatConstants.AGENT, message, ChatConstants.DB_MESSAGE_TYPE_TEXT);
            LOGGER.info("[SESSION:" + sessionId + "] Incoming message saved to database with ID: " + clientMessage.getId());
            // Mark client message as sent immediately since it was successfully received by server
            fileChatService.markMessageAsSent(clientMessage.getId());
            LOGGER.info("[SESSION:" + sessionId + "] Client message marked as sent");
        } catch (Exception dbEx) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to save incoming message to database: " + dbEx.getMessage());
        }
        
        // Get user ID from cookies
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on message; rejecting.");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID cookie. Please regenerate session.\"}")
                    .build();
        }

        try {
            // Try to get existing agent first
            KlawedAgentManager.KlawedAgentInstance agent = agentManager.getAgentForSession(sessionId);

            if (agent == null) {
                // If no agent exists, create a new one
                java.nio.file.Path sessionDir;
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
            // Responses will be delivered via database polling
            agent.sendMessageAsync(message);
            LOGGER.info("[SESSION:" + sessionId + "] Message sent to klawed, responses will be available via polling");
            
            return Response.ok()
                    .entity("{\"status\": \"message_sent\", \"sessionId\": \"" + sessionId + 
                            "\", \"messageId\": \"queued\", \"message\": \"Message sent to agent\"}")
                    .build();
            
        } catch (Exception e) {
            String errorMsg = "Error communicating with klawed agent: " + e.getMessage();
            LOGGER.severe("[SESSION:" + sessionId + "] " + errorMsg);
            LOGGER.severe("[SESSION:" + sessionId + "] Stack trace: " + e.getMessage());
            e.printStackTrace();
            LOGGER.info("[SESSION:" + sessionId + "] Saving error to database");
            try {
                ChatMessageRecord errorMessage = fileChatService.createChatMessage(
                    sessionId, ChatConstants.AGENT, ChatConstants.CLIENT, errorMsg, ChatConstants.DB_MESSAGE_TYPE_ERROR);
                LOGGER.info("[SESSION:" + sessionId + "] Error message saved to database with ID: " + errorMessage.getId());
            } catch (Exception dbEx) {
                LOGGER.warning("[SESSION:" + sessionId + "] Failed to save error message to database: " + dbEx.getMessage());
            }
            
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"" + errorMsg + "\"}")
                    .build();
        }
    }

    /**
     * Poll for messages from the AI agent.
     * Returns unsent messages for the session.
     */
    @GET
    @Path("/poll/{sessionId}")
    @Produces(MediaType.APPLICATION_JSON)
    @Transactional
    public Response pollMessages(
            @PathParam("sessionId") String sessionId,
            @QueryParam("since") Long sinceTimestamp,
            @Context HttpHeaders headers) {
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("Poll request without session ID");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }

        // Get user ID from cookies
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on poll request; rejecting.");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID cookie. Please regenerate session.\"}")
                    .build();
        }

        LOGGER.info("[SESSION:" + sessionId + "] HTTP poll request received");

        try {
            List<ChatMessageRecord> unsentMessages;
            if (sinceTimestamp != null && sinceTimestamp > 0) {
                // Get messages since the given timestamp
                unsentMessages = fileChatService.findMessagesBySessionAndReceiverSince(
                    sessionId, ChatConstants.CLIENT, sinceTimestamp);
            } else {
                // Get all unsent messages for this session
                unsentMessages = fileChatService.findUnsentMessagesForSession(sessionId);
            }
            
            LOGGER.info("[SESSION:" + sessionId + "] Found " + unsentMessages.size() + " unsent messages");
            
            // Mark messages as sent
            for (ChatMessageRecord message : unsentMessages) {
                fileChatService.markMessageAsSent(message.getId());
            }
            
            // Convert messages to JSON
            String messagesJson = objectMapper.writeValueAsString(unsentMessages);
            
            return Response.ok()
                    .entity("{\"status\": \"success\", \"sessionId\": \"" + sessionId + 
                            "\", \"count\": " + unsentMessages.size() + ", \"messages\": " + messagesJson + "}")
                    .build();
            
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to poll messages: " + e.getMessage());
            e.printStackTrace();
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to poll messages: " + e.getMessage() + "\"}")
                    .build();
        }
    }

    /**
     * Get all messages for a session (for debugging).
     */
    @GET
    @Path("/messages/{sessionId}")
    @Produces(MediaType.APPLICATION_JSON)
    @Transactional
    public Response getAllMessages(
            @PathParam("sessionId") String sessionId,
            @Context HttpHeaders headers) {
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("Get messages request without session ID");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }

        // Get user ID from cookies
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on get messages request; rejecting.");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID cookie. Please regenerate session.\"}")
                    .build();
        }

        LOGGER.info("[SESSION:" + sessionId + "] HTTP get all messages request received");

        try {
            List<ChatMessageRecord> allMessages = fileChatService.findMessagesBySession(sessionId);
            
            LOGGER.info("[SESSION:" + sessionId + "] Found " + allMessages.size() + " total messages");
            
            // Convert messages to JSON
            String messagesJson = objectMapper.writeValueAsString(allMessages);
            
            return Response.ok()
                    .entity("{\"status\": \"success\", \"sessionId\": \"" + sessionId + 
                            "\", \"count\": " + allMessages.size() + ", \"messages\": " + messagesJson + "}")
                    .build();
            
        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to get messages: " + e.getMessage());
            e.printStackTrace();
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to get messages: " + e.getMessage() + "\"}")
                    .build();
        }
    }

    /**
     * Close a session via HTTP.
     * Similar to WebSocket's onClose method.
     */
    @DELETE
    @Path("/session/{sessionId}")
    @Produces(MediaType.APPLICATION_JSON)
    @Transactional
    public Response closeSession(
            @PathParam("sessionId") String sessionId,
            @Context HttpHeaders headers) {
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("HTTP session close request without session ID");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] HTTP session close request received");

        // Get user ID from cookies
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on close; skipping persistence.");
        } else {
            LOGGER.info("[SESSION:" + sessionId + "] Closing session with userId=" + userId);
        }

        try {
            // Schedule agent shutdown with grace period instead of immediate stop
            // This allows user to reconnect and reuse the agent if it's just a temporary disconnect
            agentShutdownJobService.enqueueShutdown(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Klawed agent shutdown scheduled (grace period: 5 minutes)");

            // Persist session folders back to per-user storage
            if (userId != null && !userId.isBlank()) {
                LOGGER.info("[SESSION:" + sessionId + "] Persisting session data for user=" + userId);
                sessionManager.persistSession(sessionId, userId);
                LOGGER.info("[SESSION:" + sessionId + "] Session data persisted for user=" + userId);
            }

            // Schedule cleanup instead of immediate delete to avoid race with persistence
            cleanupJobService.enqueueCleanup(sessionId);
            sessionManager.releaseSessionTracking(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Session cleanup enqueued");

            // Remove session from session store
            SessionResource.removeSession(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Session removed from session store");

            // Deactivate chat session in database
            fileChatService.deactivateChatSession(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Chat session deactivated in database");

            return Response.ok()
                    .entity("{\"status\": \"closed\", \"sessionId\": \"" + sessionId + "\", \"message\": \"Session closed successfully\"}")
                    .build();

        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to close session: " + e.getMessage());
            e.printStackTrace();
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to close session: " + e.getMessage() + "\"}")
                    .build();
        }
    }

    /**
     * Conclude and delete a chat session completely (including session record).
     */
    @POST
    @Path("/session/{sessionId}/conclude")
    @Produces(MediaType.APPLICATION_JSON)
    @Transactional
    public Response concludeSession(
            @PathParam("sessionId") String sessionId,
            @Context HttpHeaders headers) {
        
        if (sessionId == null || sessionId.trim().isEmpty()) {
            LOGGER.severe("HTTP session conclude request without session ID");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No session ID provided\"}")
                    .build();
        }
        
        LOGGER.info("[SESSION:" + sessionId + "] HTTP session conclude request received");

        // Get user ID from cookies
        String userId = extractUserIdFromCookies(headers);
        if (userId == null || userId.isBlank()) {
            LOGGER.severe("[SESSION:" + sessionId + "] Missing userId cookie on conclude request; rejecting.");
            return Response.status(Response.Status.BAD_REQUEST)
                    .entity("{\"error\": \"No user ID cookie. Please regenerate session.\"}")
                    .build();
        }

        try {
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
            fileChatService.deactivateChatSession(sessionId);
            LOGGER.info("[SESSION:" + sessionId + "] Chat session deactivated in database");
            
            return Response.ok()
                    .entity("{\"status\": \"concluded\", \"sessionId\": \"" + sessionId + 
                            "\", \"message\": \"Session concluded. Persistent files saved and temporary directory cleaned up.\"}")
                    .build();

        } catch (Exception e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to conclude session: " + e.getMessage());
            e.printStackTrace();
            return Response.status(Response.Status.INTERNAL_SERVER_ERROR)
                    .entity("{\"error\": \"Failed to conclude session: " + e.getMessage() + "\"}")
                    .build();
        }
    }
    
    /**
     * Create and configure ObjectMapper with JavaTimeModule
     */
    private ObjectMapper createObjectMapper() {
        ObjectMapper mapper = new ObjectMapper();
        mapper.registerModule(new JavaTimeModule());
        return mapper;
    }

    /**
     * Extract userId from cookies in headers
     */
    private String extractUserIdFromCookies(HttpHeaders headers) {
        if (headers == null) return null;
        var cookies = headers.getCookies();
        Cookie cookie = cookies != null ? cookies.get(USER_COOKIE_NAME) : null;
        if (cookie != null && cookie.getValue() != null && !cookie.getValue().isBlank()) {
            return cookie.getValue();
        }
        return null;
    }


}