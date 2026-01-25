package com.filesurf.service;

import com.filesurf.repository.SessionChatRepository;
import com.filesurf.model.ChatMessageRecord;
import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import java.util.List;
import java.util.logging.Logger;

@ApplicationScoped
public class FileChatService {

    private static final Logger LOGGER = Logger.getLogger(FileChatService.class.getName());

    @Inject
    SessionChatRepository sessionChatRepository;

    @Inject
    MetricsService metricsService;

    @PostConstruct
    void init() {
        LOGGER.info("Initializing FileChatService with per-session databases...");
        // Note: Schema is initialized per-session when needed
    }

    /**
     * Create or update a chat session and track metrics.
     * @param sessionId The session ID
     * @param clientIdentity The client identity
     * @param isNewSession Whether this is a new session (true) or resumed (false)
     */
    public void createOrUpdateChatSession(String sessionId, String clientIdentity, boolean isNewSession) {
        LOGGER.info("[SESSION:" + sessionId + "] Creating/updating chat session for client: " + clientIdentity);
        
        // Track metrics based on whether this was a new or resumed session
        if (isNewSession) {
            metricsService.incrementChatSessionsCreated();
            LOGGER.info("[SESSION:" + sessionId + "] New session created");
        } else {
            metricsService.incrementChatSessionsResumed();
            LOGGER.info("[SESSION:" + sessionId + "] Existing session resumed");
        }
    }

    public ChatMessageRecord createChatMessage(String sessionId, String sender, String receiver,
                                         String content, String messageType) {
        LOGGER.info("[SESSION:" + sessionId + "] Creating chat message from " + sender + " to " + receiver);
        return sessionChatRepository.createChatMessage(sessionId, sender, receiver, content, messageType);
    }

    public void deactivateChatSession(String sessionId) {
        LOGGER.info("[SESSION:" + sessionId + "] Deactivating chat session (no-op - session tracked in main DB)");
        // Session deactivation is now handled in the main database
        // The per-session database only stores messages
    }

    /**
     * Check if a chat session is still active.
     * @deprecated Session activity is now tracked in the main database
     */
    @Deprecated
    public boolean isChatSessionActive(String sessionId) {
        // Return true as session activity is managed by the main database
        return true;
    }

    // Finder methods - read operations
    public List<ChatMessageRecord> findUnsentMessages(String sessionId) {
        return sessionChatRepository.findUnsentMessages(sessionId);
    }

    public List<ChatMessageRecord> findUnsentMessagesForSession(String sessionId) {
        return sessionChatRepository.findUnsentMessagesForSession(sessionId);
    }

    public List<ChatMessageRecord> findMessagesBySession(String sessionId) {
        return sessionChatRepository.findMessagesBySession(sessionId);
    }

    public List<ChatMessageRecord> findMessagesBySessionAndSender(String sessionId, String sender) {
        return sessionChatRepository.findMessagesBySessionAndSender(sessionId, sender);
    }

    public List<ChatMessageRecord> findMessagesBySessionAndReceiver(String sessionId, String receiver) {
        return sessionChatRepository.findMessagesBySessionAndReceiver(sessionId, receiver);
    }

    public List<ChatMessageRecord> findMessagesBySessionAndReceiverSince(String sessionId, String receiver, Long sinceTimestamp) {
        return sessionChatRepository.findMessagesBySessionAndReceiverSince(sessionId, receiver, sinceTimestamp);
    }

    public ChatMessageRecord findChatMessageById(String sessionId, Long id) {
        return sessionChatRepository.findChatMessageById(sessionId, id);
    }

    public void markMessageAsSent(String sessionId, Long messageId) {
        LOGGER.info("[SESSION:" + sessionId + "] Marking message as sent: " + messageId);
        sessionChatRepository.markMessageAsSent(sessionId, messageId);
    }

    /**
     * Close the session database connection.
     */
    public void closeSession(String sessionId) {
        LOGGER.info("[SESSION:" + sessionId + "] Closing session database connection");
        sessionChatRepository.closeSession(sessionId);
    }
}