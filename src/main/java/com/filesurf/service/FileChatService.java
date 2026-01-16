package com.filesurf.service;

import com.filesurf.repository.ChatRepository;
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
    ChatRepository chatRepository;

    @PostConstruct
    void init() {
        LOGGER.info("Initializing FileChatService and database schema...");
        chatRepository.initializeSchema();
    }

    public ChatSessionRecord createOrUpdateChatSession(String sessionId, String clientIdentity) {
        LOGGER.info("Creating/updating chat session: " + sessionId + " for client: " + clientIdentity);
        return chatRepository.createOrUpdateChatSession(sessionId, clientIdentity);
    }

    public ChatMessageRecord createChatMessage(String sessionId, String sender, String receiver,
                                         String content, String messageType) {
        LOGGER.info("Creating chat message for session: " + sessionId + " from " + sender + " to " + receiver);
        return chatRepository.createChatMessage(sessionId, sender, receiver, content, messageType);
    }

    public void deactivateChatSession(String sessionId) {
        LOGGER.info("Deactivating chat session: " + sessionId);
        chatRepository.deactivateChatSession(sessionId);
    }

    /**
     * Check if a chat session is still active in the database.
     */
    public boolean isChatSessionActive(String sessionId) {
        return chatRepository.isChatSessionActive(sessionId);
    }

    // Finder methods - read operations
    public ChatSessionRecord findChatSessionBySessionId(String sessionId) {
        return chatRepository.findChatSessionBySessionId(sessionId);
    }

    public List<ChatMessageRecord> findUnsentMessages() {
        return chatRepository.findUnsentMessages();
    }

    public List<ChatMessageRecord> findUnsentMessagesForSession(String sessionId) {
        return chatRepository.findUnsentMessagesForSession(sessionId);
    }

    public List<ChatMessageRecord> findMessagesBySession(String sessionId) {
        return chatRepository.findMessagesBySession(sessionId);
    }

    public List<ChatMessageRecord> findMessagesBySessionAndSender(String sessionId, String sender) {
        return chatRepository.findMessagesBySessionAndSender(sessionId, sender);
    }

    public List<ChatMessageRecord> findMessagesBySessionAndReceiver(String sessionId, String receiver) {
        return chatRepository.findMessagesBySessionAndReceiver(sessionId, receiver);
    }

    public List<ChatMessageRecord> findMessagesBySessionAndReceiverSince(String sessionId, String receiver, Long sinceTimestamp) {
        return chatRepository.findMessagesBySessionAndReceiverSince(sessionId, receiver, sinceTimestamp);
    }

    public ChatMessageRecord findChatMessageById(Long id) {
        return chatRepository.findChatMessageById(id);
    }

    public void markMessageAsSent(Long messageId) {
        LOGGER.info("Marking message as sent: " + messageId);
        chatRepository.markMessageAsSent(messageId);
    }
}