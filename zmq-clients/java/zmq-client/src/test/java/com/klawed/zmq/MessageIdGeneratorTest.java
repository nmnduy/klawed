package com.klawed.zmq;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class MessageIdGeneratorTest {
    
    @Test
    void testGenerateMessageId() {
        MessageIdGenerator generator = new MessageIdGenerator();
        
        String message = "Hello, world!";
        String messageId = generator.generateMessageId(message);
        
        assertNotNull(messageId);
        assertEquals(32, messageId.length()); // 32 hex characters
        assertTrue(messageId.matches("[0-9a-f]{32}")); // Valid hex string
    }
    
    @Test
    void testGenerateMessageIdWithLength() {
        MessageIdGenerator generator = new MessageIdGenerator();
        
        String message = "A".repeat(500); // Longer than HASH_SAMPLE_SIZE
        String messageId = generator.generateMessageId(message, 500);
        
        assertNotNull(messageId);
        assertEquals(32, messageId.length());
        assertTrue(messageId.matches("[0-9a-f]{32}"));
    }
    
    @Test
    void testGenerateMessageIdDifferentMessages() {
        MessageIdGenerator generator = new MessageIdGenerator();
        
        String message1 = "Hello, world!";
        String message2 = "Goodbye, world!";
        
        String id1 = generator.generateMessageId(message1);
        String id2 = generator.generateMessageId(message2);
        
        assertNotEquals(id1, id2); // Different messages should have different IDs
    }
    
    @Test
    void testGenerateMessageIdSameMessage() {
        MessageIdGenerator generator = new MessageIdGenerator();
        
        String message = "Test message";
        String id1 = generator.generateMessageId(message);
        
        // Wait a bit to ensure different timestamp
        try {
            Thread.sleep(10);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        
        String id2 = generator.generateMessageId(message);
        
        // Same message but different timestamps should produce different IDs
        assertNotEquals(id1, id2);
    }
    
    @Test
    void testGetMessageIdLength() {
        assertEquals(32, MessageIdGenerator.getMessageIdLength());
    }
    
    @Test
    void testConstructorWithSalt() {
        long salt = 123456789L;
        MessageIdGenerator generator = new MessageIdGenerator(salt);
        
        assertEquals(salt, generator.getSalt());
    }
    
    @Test
    void testConstructorWithoutSalt() {
        MessageIdGenerator generator = new MessageIdGenerator();
        
        // Salt should be set (non-zero)
        assertNotEquals(0, generator.getSalt());
    }
}