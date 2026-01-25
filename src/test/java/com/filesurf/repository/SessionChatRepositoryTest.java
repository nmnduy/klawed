package com.filesurf.repository;

import com.filesurf.model.ChatConstants;
import com.filesurf.model.ChatMessageRecord;
import io.quarkus.test.junit.QuarkusTest;
import jakarta.inject.Inject;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for SessionChatRepository.
 * Each test uses a unique session ID to isolate test data.
 */
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
    void testCreateChatMessage() {
        // Create a chat message - session is implicit in the database filename
        ChatMessageRecord message = sessionChatRepository.createChatMessage(
            testSessionId,
            ChatConstants.CLIENT,
            ChatConstants.AGENT,
            "Hello, world!",
            ChatConstants.DB_MESSAGE_TYPE_TEXT
        );

        assertNotNull(message);
        assertEquals(testSessionId, message.getSessionId());
        assertEquals(ChatConstants.CLIENT, message.getSender());
        assertEquals(ChatConstants.AGENT, message.getReceiver());
        assertEquals("Hello, world!", message.getContent());
        assertEquals(ChatConstants.DB_MESSAGE_TYPE_TEXT, message.getMessageType());
        assertFalse(message.getSent());
    }

    @Test
    void testFindMessagesBySession() {
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
        // All messages should have the correct session ID
        for (ChatMessageRecord msg : messages) {
            assertEquals(testSessionId, msg.getSessionId());
        }
    }

    @Test
    void testMarkMessageAsSent() {
        // Create a message
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
    void testFindMessagesBySender() {
        // Create messages from different senders
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Client message 1", "text");
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.AGENT, ChatConstants.CLIENT, "Agent message 1", "text");
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Client message 2", "text");

        // Find messages by sender
        List<ChatMessageRecord> clientMessages = sessionChatRepository.findMessagesBySessionAndSender(
            testSessionId, ChatConstants.CLIENT);

        assertEquals(2, clientMessages.size());
        assertTrue(clientMessages.stream().allMatch(m -> m.getSender().equals(ChatConstants.CLIENT)));
    }

    @Test
    void testFindMessagesByReceiver() {
        // Create messages to different receivers
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "To agent 1", "text");
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.AGENT, ChatConstants.CLIENT, "To client 1", "text");
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "To agent 2", "text");

        // Find messages by receiver
        List<ChatMessageRecord> agentMessages = sessionChatRepository.findMessagesBySessionAndReceiver(
            testSessionId, ChatConstants.AGENT);

        assertEquals(2, agentMessages.size());
        assertTrue(agentMessages.stream().allMatch(m -> m.getReceiver().equals(ChatConstants.AGENT)));
    }

    @Test
    void testCloseSession() {
        // Create some messages
        sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Test message", "text");

        // Close the session
        sessionChatRepository.closeSession(testSessionId);

        // Creating a new message should work (new connection)
        ChatMessageRecord message = sessionChatRepository.createChatMessage(
            testSessionId, ChatConstants.CLIENT, ChatConstants.AGENT, "Another message", "text");

        assertNotNull(message);
        assertEquals("Another message", message.getContent());
    }
}
