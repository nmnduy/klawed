package com.filesurf.websocket;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.filesurf.model.KlawedSocketMessage;
import com.filesurf.model.ChatConstants;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class KlawedSocketMessageTest {
    
    private final ObjectMapper objectMapper = new ObjectMapper();
    
    @Test
    public void testCreateSocketMessage() {
        // Test creating a socket message with string content
        KlawedSocketMessage message = KlawedSocketMessage.create(ChatConstants.MESSAGE_TYPE_TEXT, "Hello world");
        assertEquals(ChatConstants.MESSAGE_TYPE_TEXT, message.getMessageType());
        assertEquals("Hello world", message.getContent());
    }
    
    @Test
    public void testCreateTextMessage() {
        // Test creating a TEXT message (simplified socket interface)
        KlawedSocketMessage textMessage = KlawedSocketMessage.createText("Hello from simplified socket interface");
        assertEquals(ChatConstants.MESSAGE_TYPE_TEXT, textMessage.getMessageType());
        assertEquals("Hello from simplified socket interface", textMessage.getContent());
    }
    
    @Test
    public void testCreateErrorMessage() {
        // Test creating an ERROR message
        KlawedSocketMessage errorMessage = KlawedSocketMessage.createError("Something went wrong");
        assertEquals(ChatConstants.MESSAGE_TYPE_ERROR, errorMessage.getMessageType());
        assertEquals("Something went wrong", errorMessage.getContent());
    }
    
    @Test
    public void testCreateStatusMessage() {
        // Test creating a STATUS message
        KlawedSocketMessage statusMessage = KlawedSocketMessage.createStatus("Processing complete");
        assertEquals(ChatConstants.MESSAGE_TYPE_STATUS, statusMessage.getMessageType());
        assertEquals("Processing complete", statusMessage.getContent());
    }
    
    @Test
    public void testExtractTextContent() {
        // Test TEXT message extraction
        KlawedSocketMessage textMsg = KlawedSocketMessage.createText("Hello from AI");
        assertEquals("Hello from AI", textMsg.extractTextContent());
        
        // Test ERROR message extraction
        KlawedSocketMessage errorMsg = KlawedSocketMessage.createError("Something went wrong");
        assertEquals("Something went wrong", errorMsg.extractTextContent());
        
        // Test STATUS message extraction
        KlawedSocketMessage statusMsg = KlawedSocketMessage.createStatus("Processing complete");
        assertEquals("Processing complete", statusMsg.extractTextContent());
        
        // Test with null content
        KlawedSocketMessage nullMsg = new KlawedSocketMessage();
        nullMsg.setMessageType(ChatConstants.MESSAGE_TYPE_TEXT);
        nullMsg.setContent(null);
        assertNull(nullMsg.extractTextContent());
        
        // Test with non-string content (should return null)
        KlawedSocketMessage nonStringMsg = new KlawedSocketMessage();
        nonStringMsg.setMessageType(ChatConstants.MESSAGE_TYPE_TEXT);
        nonStringMsg.setContent(new Object());
        assertNull(nonStringMsg.extractTextContent());
    }
    
    @Test
    public void testJsonSerialization() throws Exception {
        // Test TEXT message serialization and deserialization
        KlawedSocketMessage original = KlawedSocketMessage.createText("Hello klawed");
        
        String json = objectMapper.writeValueAsString(original);
        assertTrue(json.contains("\"messageType\":\"" + ChatConstants.MESSAGE_TYPE_TEXT + "\""));
        assertTrue(json.contains("\"content\":\"Hello klawed\""));
        
        KlawedSocketMessage deserialized = objectMapper.readValue(json, KlawedSocketMessage.class);
        assertEquals(ChatConstants.MESSAGE_TYPE_TEXT, deserialized.getMessageType());
        assertEquals("Hello klawed", deserialized.getContent());
        assertEquals("Hello klawed", deserialized.extractTextContent());
    }
    
    @Test
    public void testTextMessageSerialization() throws Exception {
        // Test TEXT message serialization (what we send to klawed)
        KlawedSocketMessage textMessage = KlawedSocketMessage.createText("Hello klawed");
        String json = objectMapper.writeValueAsString(textMessage);
        
        assertEquals("{\"messageType\":\"TEXT\",\"content\":\"Hello klawed\"}", json);
        
        // Deserialize and verify
        KlawedSocketMessage deserialized = objectMapper.readValue(json, KlawedSocketMessage.class);
        assertEquals(ChatConstants.MESSAGE_TYPE_TEXT, deserialized.getMessageType());
        assertEquals("Hello klawed", deserialized.getContent());
        assertEquals("Hello klawed", deserialized.extractTextContent());
    }
    
    @Test
    public void testErrorMessageSerialization() throws Exception {
        // Test ERROR message serialization
        KlawedSocketMessage errorMessage = KlawedSocketMessage.createError("Connection failed");
        String json = objectMapper.writeValueAsString(errorMessage);
        
        assertEquals("{\"messageType\":\"ERROR\",\"content\":\"Connection failed\"}", json);
        
        // Deserialize and verify
        KlawedSocketMessage deserialized = objectMapper.readValue(json, KlawedSocketMessage.class);
        assertEquals(ChatConstants.MESSAGE_TYPE_ERROR, deserialized.getMessageType());
        assertEquals("Connection failed", deserialized.getContent());
        assertEquals("Connection failed", deserialized.extractTextContent());
    }
}