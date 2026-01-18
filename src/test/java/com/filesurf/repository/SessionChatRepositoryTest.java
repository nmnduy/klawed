package com.filesurf.repository;

import com.filesurf.model.ChatConstants;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatSessionRecord;
import io.quarkus.test.junit.QuarkusTest;
import jakarta.inject.Inject;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.*;

@QuarkusTest
class SessionChatRepositoryTest {

    @Inject
    SessionChatRepository sessionChatRepository;

    private String testSessionId;

    @BeforeEach
    void setUp() {
        testSessionId = "test-session-" + UUID.randomUUID();
    }

    @AfterEach
    void tearDown() {
        // Clean up session database
        sessionChatRepository.closeSession(testSessionId);
    }

    @Test
    void testCreateAndFindChatSession() {
        // Create a chat session
        ChatSessionRecord session = sessionChatRepository.createOrUpdateChatSession(
            testSessionId, "test-client");
        
        assertNotNull(session);
        assertEquals(testSessionId, session.getSessionId());
        assertEquals("test-client", session.getClientIdentity());
        assertTrue(session.getIsActive());
        
        // Find the session
        ChatSessionRecord found = sessionChatRepository.findChatSessionBySessionId(testSessionId);
        assertNotNull(found);
        assertEquals(session.getId(), found.getId());
        assertEquals(testSessionId, found.getSessionId());
    }

    @Test
    void testCreateChatMessage() {
        // First create a session
        sessionChatRepository.createOrUpdateChatSession(testSessionId, "test-client");
        
        // Create a chat message
        ChatMessageRecord message = sessionChatRepository.createChatMessage(
            testSessionId,
            ChatConstants.CLIENT,
            ChatConstants.AGENT,
            "Hello, world!",
            ChatConstants.DB_MESSAGE_TYPE_TEXT
        );
        
        assertNotNull(message);
        assertEquals(testSessionId, message.getSessionStringId());
        assertEquals(ChatConstants.CLIENT, message.getSender());
        assertEquals(ChatConstants.AGENT, message.getReceiver());
        assertEquals("Hello, world!", message.getContent());
        assertEquals(ChatConstants.DB_MESSAGE_TYPE_TEXT, message.getMessageType());
        assertFalse(message.getSent());
    }

    @Test
    void testFindMessagesBySession() {
        // First create a session
        sessionChatRepository.createOrUpdateChatSession(testSessionId, "test-client");
        
        // Create multiple messages
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Message 1", "text");
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.AGENT, ChatConstants.CLIENT, "Response 1", "text");
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Message 2", "text");
        
        // Find all messages for the session
        List<ChatMessageRecord> messages = sessionChatRepository.findMessagesBySession(testSessionId);
        
        assertEquals(3, messages.size());
        assertEquals("Message 1", messages.get(0).getContent());
        assertEquals("Response 1", messages.get(1).getContent());
        assertEquals("Message 2", messages.get(2).getContent());
    }

    @Test
    void testMarkMessageAsSent() {
        // First create a session and message
        sessionChatRepository.createOrUpdateChatSession(testSessionId, "test-client");
        ChatMessageRecord message = sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Test message", "text");
        
        assertFalse(message.getSent());
        
        // Mark as sent
        sessionChatRepository.markMessageAsSent(testSessionId, message.getId());
        
        // Find the message again to verify it's marked as sent
        ChatMessageRecord updated = sessionChatRepository.findChatMessageById(testSessionId, message.getId());
        assertNotNull(updated);
        assertTrue(updated.getSent());
        assertNotNull(updated.getSentAt());
    }

    @Test
    void testFindUnsentMessages() {
        // First create a session
        sessionChatRepository.createOrUpdateChatSession(testSessionId, "test-client");
        
        // Create sent and unsent messages
        ChatMessageRecord sentMessage = sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.AGENT, ChatConstants.CLIENT, "Sent message", "text");
        sessionChatRepository.markMessageAsSent(testSessionId, sentMessage.getId());
        
        ChatMessageRecord unsentMessage1 = sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.AGENT, ChatConstants.CLIENT, "Unsent message 1", "text");
        ChatMessageRecord unsentMessage2 = sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.AGENT, ChatConstants.CLIENT, "Unsent message 2", "text");
        
        // Find unsent messages
        List<ChatMessageRecord> unsentMessages = sessionChatRepository.findUnsentMessages(testSessionId);
        
        assertEquals(2, unsentMessages.size());
        assertTrue(unsentMessages.stream().anyMatch(m -> m.getContent().equals("Unsent message 1")));
        assertTrue(unsentMessages.stream().anyMatch(m -> m.getContent().equals("Unsent message 2")));
        assertFalse(unsentMessages.stream().anyMatch(m -> m.getContent().equals("Sent message")));
    }

    @Test
    void testDeactivateChatSession() {
        // Create a session
        sessionChatRepository.createOrUpdateChatSession(testSessionId, "test-client");
        
        // Verify it's active
        assertTrue(sessionChatRepository.isChatSessionActive(testSessionId));
        
        // Deactivate it
        sessionChatRepository.deactivateChatSession(testSessionId);
        
        // Verify it's not active
        assertFalse(sessionChatRepository.isChatSessionActive(testSessionId));
    }
}