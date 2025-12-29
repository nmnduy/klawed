package com.klawed.zmq;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class PendingMessageTest {
    
    @Test
    void testConstructorAndGetters() {
        String messageId = "test123";
        String messageJson = "{\"messageType\":\"TEXT\",\"content\":\"Hello\"}";
        long sentTimeMs = System.currentTimeMillis();
        
        PendingMessage pending = new PendingMessage(messageId, messageJson, sentTimeMs);
        
        assertEquals(messageId, pending.getMessageId());
        assertEquals(messageJson, pending.getMessageJson());
        assertEquals(sentTimeMs, pending.getSentTimeMs());
        assertEquals(0, pending.getRetryCount());
    }
    
    @Test
    void testIncrementRetryCount() {
        PendingMessage pending = new PendingMessage("test", "{}", System.currentTimeMillis());
        
        assertEquals(0, pending.getRetryCount());
        assertEquals(1, pending.incrementRetryCount());
        assertEquals(1, pending.getRetryCount());
        assertEquals(2, pending.incrementRetryCount());
        assertEquals(2, pending.getRetryCount());
    }
    
    @Test
    void testGetElapsedTimeMs() {
        long sentTimeMs = System.currentTimeMillis() - 1000; // 1 second ago
        PendingMessage pending = new PendingMessage("test", "{}", sentTimeMs);
        
        long elapsed = pending.getElapsedTimeMs(System.currentTimeMillis());
        assertTrue(elapsed >= 1000 && elapsed <= 1100); // Approximately 1000ms
    }
    
    @Test
    void testToString() {
        PendingMessage pending = new PendingMessage("test123", "{}", 123456789L);
        String str = pending.toString();
        
        assertNotNull(str);
        assertTrue(str.contains("test123"));
        assertTrue(str.contains("123456789"));
    }
}