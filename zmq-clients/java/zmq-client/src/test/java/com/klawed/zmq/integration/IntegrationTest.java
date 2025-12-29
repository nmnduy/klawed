package com.klawed.zmq.integration;

import com.klawed.zmq.KlawedZMQClient;
import com.klawed.zmq.Message;
import com.klawed.zmq.ZMQException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfSystemProperty;

import java.io.IOException;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Integration test that requires a running Klawed daemon.
 * These tests are disabled by default and can be enabled with system property.
 */
@EnabledIfSystemProperty(named = "test.integration", matches = "true")
class IntegrationTest {
    
    private static final String TEST_ENDPOINT = "tcp://127.0.0.1:5555";
    
    @Test
    void testConnection() throws ZMQException, IOException {
        // This test requires a running Klawed daemon
        try (KlawedZMQClient client = new KlawedZMQClient(TEST_ENDPOINT)) {
            assertTrue(client.isRunning());
            assertEquals(TEST_ENDPOINT, client.getEndpoint());
            assertEquals(0, client.getPendingMessageCount());
        }
    }
    
    @Test
    void testSimpleTextRequest() throws ZMQException, IOException {
        try (KlawedZMQClient client = new KlawedZMQClient(TEST_ENDPOINT)) {
            Message response = client.sendText("What is 2+2?");
            
            assertNotNull(response);
            assertEquals(com.klawed.zmq.MessageType.TEXT, response.getMessageType());
            assertNotNull(response.getContent());
            assertTrue(response.getContent().length() > 0);
            
            // Response should contain some form of answer
            String content = response.getContent().toLowerCase();
            assertTrue(content.contains("4") || content.contains("four"));
        }
    }
    
    @Test
    void testMultipleRequests() throws ZMQException, IOException {
        try (KlawedZMQClient client = new KlawedZMQClient(TEST_ENDPOINT)) {
            // First request
            Message response1 = client.sendText("What is 2+2?");
            assertNotNull(response1);
            assertEquals(com.klawed.zmq.MessageType.TEXT, response1.getMessageType());
            
            // Second request
            Message response2 = client.sendText("What is 3+3?");
            assertNotNull(response2);
            assertEquals(com.klawed.zmq.MessageType.TEXT, response2.getMessageType());
            
            // Responses should be different
            assertNotEquals(response1.getContent(), response2.getContent());
        }
    }
    
    @Test
    void testReceiveTimeout() throws ZMQException, IOException {
        try (KlawedZMQClient client = new KlawedZMQClient(TEST_ENDPOINT)) {
            // Try to receive with very short timeout
            Message response = client.receive(100); // 100ms timeout
            
            // Should timeout (return null) since no message is being sent
            assertNull(response);
        }
    }
}