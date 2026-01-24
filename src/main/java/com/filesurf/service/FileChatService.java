package com.filesurf.service;

import com.filesurf.repository.SessionChatRepository;
import com.filesurf.model.ChatSessionRecord;
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

    public ChatSessionRecord createOrUpdateChatSession(String sessionId, String clientIdentity) {
        LOGGER.info("[SESSION:" + sessionId + "] Creating/updating chat session for client: " + clientIdentity);
        
        // Check if session exists before creating/updating to track metrics
        ChatSessionRecord existingSession = sessionChatRepository.findChatSessionBySessionId(sessionId);
        boolean isNewSession = (existingSession == null);
        
        ChatSessionRecord result = sessionChatRepository.createOrUpdateChatSession(sessionId, clientIdentity);
        
        // Track metrics based on whether this was a new or resumed session
        if (isNewSession) {
            metricsService.incrementChatSessionsCreated();
            LOGGER.info("[SESSION:" + sessionId + "] New session created");
        } else {
            metricsService.incrementChatSessionsResumed();
            LOGGER.info("[SESSION:" + sessionId + "] Existing session resumed");
        }
        
        return result;
    }

    public ChatMessageRecord createChatMessage(String sessionId, String sender, String receiver,
                                         String content, String messageType) {
        LOGGER.info("[SESSION:" + sessionId + "] Creating chat message from " + sender + " to " + receiver);
        return sessionChatRepository.createChatMessage(sessionId, sender, receiver, content, messageType);
    }

    public void deactivateChatSession(String sessionId) {
        LOGGER.info("[SESSION:" + sessionId + "] Deactivating chat session");
        sessionChatRepository.deactivateChatSession(sessionId);
    }

    /**
     * Check if a chat session is still active in the database.
     */
    public boolean isChatSessionActive(String sessionId) {
        return sessionChatRepository.isChatSessionActive(sessionId);
    }

    // Finder methods - read operations
    public ChatSessionRecord findChatSessionBySessionId(String sessionId) {
        return sessionChatRepository.findChatSessionBySessionId(sessionId);
    }

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