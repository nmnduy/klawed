package com.filesurf.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.filesurf.model.ChatConstants;
import com.filesurf.model.SQLiteQueueConstants;
import jakarta.inject.Inject;

import java.io.IOException;
import java.sql.*;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.logging.Logger;

/**
 * SQLite queue client for communicating with klawed agent via SQLite database.
 * Based on the SQLite queue specification from klawedspace/docs/sqlite-queue.md
 */
public class SQLiteQueueClient {
    
    private static final Logger LOGGER = Logger.getLogger(SQLiteQueueClient.class.getName());
    private final ObjectMapper objectMapper = new ObjectMapper();
    
    // Track pending tool requests to determine if klawed is still working
    private final java.util.Set<String> pendingToolRequests = java.util.Collections.synchronizedSet(new java.util.HashSet<>());
    
    @Inject
    FileChatService fileChatService;
    
    private String sessionId;
    
    // Configuration matching SQLite queue defaults
    public static class Config {
        // Connection settings
        private String dbPath;
        private String senderName = SQLiteQueueConstants.DEFAULT_SENDER_NAME;
        private String receiverName = SQLiteQueueConstants.DEFAULT_RECEIVER_NAME;
        private String sessionId;
        private FileChatService fileChatService;
        
        // Client configuration
        private int pollIntervalMs = SQLiteQueueConstants.DEFAULT_POLL_INTERVAL_MS;
        private int pollTimeoutMs = SQLiteQueueConstants.DEFAULT_POLL_TIMEOUT_MS;
        private int maxRetries = SQLiteQueueConstants.DEFAULT_MAX_RETRIES;
        private int maxMessageSize = SQLiteQueueConstants.DEFAULT_MAX_MESSAGE_SIZE; // 1MB
        private int maxQueueSize = SQLiteQueueConstants.DEFAULT_MAX_QUEUE_SIZE;
        
        public Config(String dbPath) {
            this.dbPath = dbPath;
        }
        
        // Builder pattern for fluent configuration
        public Config withSenderName(String senderName) {
            this.senderName = senderName;
            return this;
        }
        
        public Config withReceiverName(String receiverName) {
            this.receiverName = receiverName;
            return this;
        }
        
        public Config withSessionId(String sessionId) {
            this.sessionId = sessionId;
            return this;
        }
        
        public Config withFileChatService(FileChatService fileChatService) {
            this.fileChatService = fileChatService;
            return this;
        }
        
        public Config withPollIntervalMs(int pollIntervalMs) {
            this.pollIntervalMs = pollIntervalMs;
            return this;
        }
        
        public Config withPollTimeoutMs(int pollTimeoutMs) {
            this.pollTimeoutMs = pollTimeoutMs;
            return this;
        }
        
        public Config withMaxRetries(int maxRetries) {
            this.maxRetries = maxRetries;
            return this;
        }
        
        public Config withMaxMessageSize(int maxMessageSize) {
            this.maxMessageSize = maxMessageSize;
            return this;
        }
        
        public Config withMaxQueueSize(int maxQueueSize) {
            this.maxQueueSize = maxQueueSize;
            return this;
        }
        
        // Getters
        public String getDbPath() { return dbPath; }
        public String getSenderName() { return senderName; }
        public String getReceiverName() { return receiverName; }
        public String getSessionId() { return sessionId; }
        public FileChatService getFileChatService() { return fileChatService; }
        public int getPollIntervalMs() { return pollIntervalMs; }
        public int getPollTimeoutMs() { return pollTimeoutMs; }
        public int getMaxRetries() { return maxRetries; }
        public int getMaxMessageSize() { return maxMessageSize; }
        public int getMaxQueueSize() { return maxQueueSize; }
    }
    
    private final Config config;
    private Connection connection;
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private final AtomicInteger messagesSent = new AtomicInteger(0);
    private final AtomicInteger messagesReceived = new AtomicInteger(0);
    private final AtomicLong lastActivityTime = new AtomicLong(System.currentTimeMillis());
    
    // Thread pool for async operations
    private final ExecutorService executorService = Executors.newCachedThreadPool();
    
    // SQL statements
    private static final String CREATE_TABLE_SQL = 
        "CREATE TABLE IF NOT EXISTS messages (" +
        "id INTEGER PRIMARY KEY AUTOINCREMENT," +
        "sender TEXT NOT NULL," +
        "receiver TEXT NOT NULL," +
        "message TEXT NOT NULL," +
        "sent INTEGER DEFAULT 0," +
        "created_at INTEGER DEFAULT (strftime('%s', 'now'))," +
        "updated_at INTEGER DEFAULT (strftime('%s', 'now'))" +
        ");";
    
    private static final String CREATE_INDEX_SENDER_SQL = 
        "CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent);";
    
    private static final String CREATE_INDEX_RECEIVER_SQL = 
        "CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent);";
    
    private static final String INSERT_MESSAGE_SQL = 
        "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0);";
    
    private static final String SELECT_MESSAGES_SQL = 
        "SELECT id, message FROM messages WHERE receiver = ? AND sent = 0 ORDER BY created_at ASC LIMIT ?;";
    
    private static final String ACK_MESSAGE_SQL = 
        "UPDATE messages SET sent = 1, updated_at = strftime('%s', 'now') WHERE id = ?;";
    
    private static final String COUNT_PENDING_SQL = 
        "SELECT COUNT(*) FROM messages WHERE sent = 0;";
    
    private static final String COUNT_TOTAL_SQL = 
        "SELECT COUNT(*) FROM messages;";
    
    private static final String COUNT_UNREAD_SQL = 
        "SELECT COUNT(*) FROM messages WHERE sender = ? AND sent = 0;";
    
    public SQLiteQueueClient(String dbPath) {
        this(new Config(dbPath));
    }
    
    public SQLiteQueueClient(Config config) {
        this.config = config;
        this.sessionId = config.getSessionId();
        
        LOGGER.info("Initializing SQLiteQueueClient for database: " + config.getDbPath());
        LOGGER.info("Configuration - sender=" + config.getSenderName() +
                   ", receiver=" + config.getReceiverName() +
                   ", sessionId=" + (sessionId != null ? sessionId : "null") +
                   ", pollInterval=" + config.getPollIntervalMs() + "ms" +
                   ", pollTimeout=" + config.getPollTimeoutMs() + "ms");
    }
    
    /**
     * Connect to the SQLite database and initialize schema
     */
    public void connect() throws IOException {
        LOGGER.info("Connecting to SQLite database: " + config.getDbPath());
        
        try {
            // Create database connection
            connection = DriverManager.getConnection("jdbc:sqlite:" + config.getDbPath());
            
            // Set pragmas for better performance
            try (Statement stmt = connection.createStatement()) {
                stmt.execute("PRAGMA journal_mode = WAL;");
                stmt.execute("PRAGMA synchronous = NORMAL;");
                stmt.execute("PRAGMA busy_timeout = 5000;");
                stmt.execute("PRAGMA foreign_keys = ON;");
            }
            
            // Initialize schema
            initializeSchema();
            
            connected.set(true);
            LOGGER.info("Successfully connected to SQLite database: " + config.getDbPath());
        } catch (SQLException e) {
            connected.set(false);
            throw new IOException("Failed to connect to SQLite database: " + config.getDbPath(), e);
        }
    }
    
    /**
     * Initialize database schema (create table and indexes if needed)
     */
    private void initializeSchema() throws SQLException {
        try (Statement stmt = connection.createStatement()) {
            // Create messages table
            stmt.execute(CREATE_TABLE_SQL);
            
            // Create indexes
            stmt.execute(CREATE_INDEX_SENDER_SQL);
            stmt.execute(CREATE_INDEX_RECEIVER_SQL);
            
            LOGGER.info("SQLite queue schema initialized");
        }
    }
    
    /**
     * Send a message to the receiver
     */
    public void sendMessage(String message) throws IOException {
        sendMessage(message, config.getReceiverName());
    }
    
    /**
     * Send a message to a specific receiver
     */
    public void sendMessage(String message, String receiver) throws IOException {
        LOGGER.info("SQLiteQueueClient.sendMessage called");
        
        if (!connected.get() || connection == null) {
            throw new IOException("SQLiteQueueClient not connected");
        }
        
        // Validate message size
        if (message.length() > config.getMaxMessageSize()) {
            throw new IOException("Message too large: " + message.length() + 
                                " bytes (max: " + config.getMaxMessageSize() + ")");
        }
        
        // Create JSON message
        String jsonMessage;
        try {
            ObjectNode json = objectMapper.createObjectNode();
            json.put("messageType", "TEXT");
            json.put("content", message);
            jsonMessage = objectMapper.writeValueAsString(json);
        } catch (Exception e) {
            throw new IOException("Failed to create JSON message", e);
        }
        
        // Insert message into database
        try (PreparedStatement pstmt = connection.prepareStatement(INSERT_MESSAGE_SQL)) {
            pstmt.setString(1, config.getSenderName());
            pstmt.setString(2, receiver);
            pstmt.setString(3, jsonMessage);
            pstmt.executeUpdate();
            
            messagesSent.incrementAndGet();
            lastActivityTime.set(System.currentTimeMillis());
            
            LOGGER.info("Message sent to " + receiver + " (length: " + message.length() + " chars)");
        } catch (SQLException e) {
            throw new IOException("Failed to send message via SQLite queue", e);
        }
    }
    
    /**
     * Receive messages from the sender with timeout
     */
    public List<String> receiveMessages() throws IOException {
        return receiveMessages(config.getPollTimeoutMs());
    }
    
    /**
     * Receive messages from the sender with specified timeout
     */
    public List<String> receiveMessages(int timeoutMs) throws IOException {
        LOGGER.fine("SQLiteQueueClient.receiveMessages called with timeout: " + timeoutMs + "ms");
        
        if (!connected.get() || connection == null) {
            throw new IOException("SQLiteQueueClient not connected");
        }
        
        long startTime = System.currentTimeMillis();
        List<String> messages = new ArrayList<>();
        
        while (System.currentTimeMillis() - startTime < timeoutMs) {
            // Re-check connection at start of each poll iteration (connection can be shutdown by another thread)
            if (!connected.get() || connection == null) {
                LOGGER.fine("SQLiteQueueClient disconnected during polling loop, returning collected messages");
                return messages;
            }
            
            try (PreparedStatement pstmt = connection.prepareStatement(SELECT_MESSAGES_SQL)) {
                pstmt.setString(1, config.getSenderName());
                pstmt.setInt(2, 10); // Limit to 10 messages at a time
                
                try (ResultSet rs = pstmt.executeQuery()) {
                    while (rs.next()) {
                        long messageId = rs.getLong("id");
                        String jsonMessage = rs.getString("message");
                        
                        // Parse JSON to extract content
                        try {
                            ObjectNode json = (ObjectNode) objectMapper.readTree(jsonMessage);
                            
                            // Check if messageType exists
                            if (!json.has("messageType")) {
                                LOGGER.warning("Message missing messageType field: " + jsonMessage);
                                acknowledgeMessage(messageId);
                                continue;
                            }
                            
                            String messageType = json.get("messageType").asText();
                            if (messageType == null || messageType.isEmpty()) {
                                LOGGER.warning("Message has empty or null messageType field: " + jsonMessage);
                                acknowledgeMessage(messageId);
                                continue;
                            }
                            
                            if (ChatConstants.MESSAGE_TYPE_TEXT.equals(messageType) || ChatConstants.MESSAGE_TYPE_ERROR.equals(messageType)) {
                                // TEXT and ERROR messages should have content field
                                if (json.has("content")) {
                                    String content = json.get("content").asText();
                                    messages.add(content);
                                    
                                    // Save message to main database if it's from klawed to client
                                    // and we have FileChatService available
                                    if (config.getFileChatService() != null && config.getSessionId() != null) {
                                        try {
                                            // Determine message type for database
                                            String dbMessageType = ChatConstants.MESSAGE_TYPE_TEXT.equals(messageType) ? 
                                                ChatConstants.DB_MESSAGE_TYPE_TEXT : ChatConstants.DB_MESSAGE_TYPE_ERROR;
                                            
                                            // Save to main database (agent -> client)
                                            config.getFileChatService().createChatMessage(
                                                config.getSessionId(),
                                                ChatConstants.AGENT,  // sender
                                                ChatConstants.CLIENT, // receiver  
                                                content,
                                                dbMessageType
                                            );
                                            LOGGER.fine("Saved message from klawed to main database: " + 
                                                       content.substring(0, Math.min(100, content.length())) + 
                                                       (content.length() > 100 ? "..." : ""));
                                        } catch (Exception dbEx) {
                                            LOGGER.warning("Failed to save message to main database: " + dbEx.getMessage());
                                        }
                                    }
                                } else {
                                    LOGGER.warning(messageType + " message missing content field: " + jsonMessage);
                                    // Still add a placeholder message
                                    messages.add("[" + messageType + " MESSAGE]");
                                }
                            } else if (ChatConstants.MESSAGE_TYPE_TOOL.equals(messageType)) {
                                // Handle tool request
                                if (json.has("toolName") && json.has("toolId")) {
                                    String toolName = json.get("toolName").asText();
                                    String toolId = json.get("toolId").asText();
                                    LOGGER.info("Received TOOL request: " + toolName + " (id: " + toolId + ")");
                                    
                                    // Track pending tool request
                                    pendingToolRequests.add(toolId);
                                    LOGGER.fine("Added pending tool request: " + toolId + " (" + toolName + ")");
                                    
                                    // Save TOOL message to main database for WebSocket forwarding
                                    if (config.getFileChatService() != null) {
                                        try {
                                            // Create a JSON string with tool details
                                            String toolJson = objectMapper.writeValueAsString(json);
                                            config.getFileChatService().createChatMessage(
                                                config.getSessionId(),
                                                ChatConstants.AGENT,  // sender
                                                ChatConstants.CLIENT, // receiver  
                                                toolJson,
                                                ChatConstants.DB_MESSAGE_TYPE_TOOL
                                            );
                                            LOGGER.fine("Saved TOOL message to main database for: " + toolName);
                                        } catch (Exception dbEx) {
                                            LOGGER.warning("Failed to save TOOL message to main database: " + dbEx.getMessage());
                                        }
                                    }
                                } else {
                                    LOGGER.warning("TOOL message missing required fields: " + jsonMessage);
                                }
                            } else if (ChatConstants.MESSAGE_TYPE_TOOL_RESULT.equals(messageType)) {
                                // Handle tool result
                                if (json.has("toolName") && json.has("toolId") && json.has("isError")) {
                                    String toolName = json.get("toolName").asText();
                                    String toolId = json.get("toolId").asText();
                                    boolean isError = json.get("isError").asBoolean();
                                    LOGGER.fine("Received TOOL_RESULT: " + toolName + 
                                              " (id: " + toolId + ", error: " + isError + ")");
                                    
                                    // Remove from pending tool requests
                                    if (pendingToolRequests.remove(toolId)) {
                                        LOGGER.fine("Removed completed tool request: " + toolId + " (" + toolName + ")");
                                    }
                                    
                                    // Save TOOL_RESULT message to main database for WebSocket forwarding
                                    if (config.getFileChatService() != null) {
                                        try {
                                            // Create a JSON string with tool result details
                                            String toolResultJson = objectMapper.writeValueAsString(json);
                                            config.getFileChatService().createChatMessage(
                                                config.getSessionId(),
                                                ChatConstants.AGENT,  // sender
                                                ChatConstants.CLIENT, // receiver  
                                                toolResultJson,
                                                ChatConstants.DB_MESSAGE_TYPE_TOOL_RESULT
                                            );
                                            LOGGER.fine("Saved TOOL_RESULT message to main database for: " + toolName);
                                        } catch (Exception dbEx) {
                                            LOGGER.warning("Failed to save TOOL_RESULT message to main database: " + dbEx.getMessage());
                                        }
                                    }
                                } else {
                                    LOGGER.warning("TOOL_RESULT message missing required fields: " + jsonMessage);
                                }
                            } else if (ChatConstants.MESSAGE_TYPE_API_CALL.equals(messageType)) {
                                // Handle API call in progress
                                LOGGER.fine("Received API_CALL message - AI is processing");
                                // Don't add API_CALL to messages list - it will be sent via WebSocket from main database
                                LOGGER.fine("API_CALL message kept for WebSocket forwarding only");
                                
                                // Extract API call details if available
                                String apiCallDetails = "AI is thinking";
                                if (json.has("model") || json.has("provider") || json.has("estimatedDurationMs")) {
                                    StringBuilder details = new StringBuilder("AI is thinking (");
                                    if (json.has("model")) {
                                        details.append("model: ").append(json.get("model").asText());
                                    }
                                    if (json.has("provider")) {
                                        if (details.length() > "AI is processing (".length()) {
                                            details.append(", ");
                                        }
                                        details.append("provider: ").append(json.get("provider").asText());
                                    }
                                    if (json.has("estimatedDurationMs")) {
                                        long estimatedMs = json.get("estimatedDurationMs").asLong();
                                        if (details.length() > "AI is processing (".length()) {
                                            details.append(", ");
                                        }
                                        details.append("estimated: ").append(estimatedMs).append("ms");
                                    }
                                    details.append(")");
                                    apiCallDetails = details.toString();
                                }
                                
                                // Send status update about API call
                                sendStatusUpdate(apiCallDetails);
                                
                                // Save API call message to main database
                                if (config.getFileChatService() != null) {
                                    try {
                                        // Create a JSON string with API call details
                                        String apiCallJson = objectMapper.writeValueAsString(json);
                                        config.getFileChatService().createChatMessage(
                                            config.getSessionId(),
                                            ChatConstants.AGENT,  // sender
                                            ChatConstants.CLIENT, // receiver  
                                            apiCallJson,
                                            ChatConstants.DB_MESSAGE_TYPE_API_CALL
                                        );
                                        LOGGER.fine("Saved API_CALL message to main database");
                                    } catch (Exception dbEx) {
                                        LOGGER.warning("Failed to save API_CALL message to main database: " + dbEx.getMessage());
                                    }
                                }
                            } else if (ChatConstants.MESSAGE_TYPE_END_AI_TURN.equals(messageType)) {
                                // Handle end of AI turn - signal that AI is done processing
                                LOGGER.fine("Received END_AI_TURN message - AI turn completed");
                                
                                // Clear pending tool requests
                                pendingToolRequests.clear();
                                
                                // Forward to WebSocket via message to main database
                                if (config.getFileChatService() != null && config.getSessionId() != null) {
                                    try {
                                        // Create a JSON message for END_AI_TURN
                                        ObjectNode endTurnJson = objectMapper.createObjectNode();
                                        endTurnJson.put("messageType", ChatConstants.MESSAGE_TYPE_END_AI_TURN);
                                        String endTurnJsonStr = objectMapper.writeValueAsString(endTurnJson);
                                        
                                        config.getFileChatService().createChatMessage(
                                            config.getSessionId(),
                                            ChatConstants.AGENT,  // sender
                                            ChatConstants.CLIENT, // receiver  
                                            endTurnJsonStr,
                                            ChatConstants.DB_MESSAGE_TYPE_STATUS  // Use status type for internal messages
                                        );
                                        LOGGER.info("Forwarded END_AI_TURN message to main database for WebSocket delivery");
                                    } catch (Exception dbEx) {
                                        LOGGER.warning("Failed to save END_AI_TURN message to main database: " + dbEx.getMessage());
                                    }
                                }
                            } else {
                                LOGGER.warning("Unknown message type: " + messageType);
                                messages.add("[UNKNOWN MESSAGE TYPE: " + messageType + "]");
                            }
                            
                            // Acknowledge message
                            acknowledgeMessage(messageId);
                            messagesReceived.incrementAndGet();
                            lastActivityTime.set(System.currentTimeMillis());
                            
                        } catch (Exception e) {
                            LOGGER.warning("Failed to parse message JSON: " + e.getMessage());
                            // Still acknowledge the message
                            acknowledgeMessage(messageId);
                        }
                    }
                }
                
                // If we got messages, return them
                if (!messages.isEmpty()) {
                    LOGGER.fine("Received " + messages.size() + " message(s)");
                    return messages;
                }
                
            } catch (SQLException e) {
                // Check if this is due to connection being closed
                if (!connected.get() || connection == null) {
                    throw new IOException("SQLiteQueueClient disconnected during receive", e);
                }
                throw new IOException("Failed to receive messages via SQLite queue", e);
            }
            
            // Wait for poll interval before trying again
            try {
                Thread.sleep(config.getPollIntervalMs());
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("Interrupted while waiting for messages", e);
            }
        }
        
        LOGGER.fine("No messages received within timeout");
        return messages; // Return empty list
    }
    
    /**
     * Send a message and wait for response
     */
    public String sendAndReceive(String message) throws IOException {
        return sendAndReceive(message, config.getPollTimeoutMs());
    }
    
    /**
     * Send a message and wait for response with timeout
     * @deprecated Use sendMessageAsync for WebSocket sessions
     */
    public String sendAndReceive(String message, int timeoutMs) throws IOException {
        LOGGER.info("SQLiteQueueClient.sendAndReceive called");
        
        // Clear pending tool requests from previous interactions
        pendingToolRequests.clear();
        
        // Send the message
        sendMessage(message);
        
        // Send initial status update
        sendStatusUpdate("AI is thinking...");
        
        // Wait for response
        long startTime = System.currentTimeMillis();
        StringBuilder response = new StringBuilder();
        
        while (System.currentTimeMillis() - startTime < timeoutMs) {
            // Poll for messages with a short timeout
            List<String> messages = receiveMessages(500);
            
            // Add any messages to response
            for (String msg : messages) {
                response.append(msg).append("\n");
            }
            
            // Check if we have any response and no pending tool requests
            // This indicates klawed has finished processing
            if (response.length() > 0 && pendingToolRequests.isEmpty()) {
                LOGGER.info("Klawed has completed processing with " + response.length() + " chars of response");
                sendStatusUpdate("Processing complete");
                return response.toString().trim();
            }
        }
        
        // Total timeout reached
        if (response.length() > 0) {
            LOGGER.info("Total timeout reached, returning " + response.length() + " chars of response");
            LOGGER.info("Pending tool requests: " + pendingToolRequests.size());
            sendStatusUpdate("Processing complete (timeout)");
            return response.toString().trim();
        }
        
        throw new IOException("No response received within timeout: " + timeoutMs + "ms");
    }
    
    /**
     * Send a message asynchronously (for WebSocket sessions)
     * Returns immediately after sending the message
     * Responses will be delivered via ChatMessagePollingService
     */
    public void sendMessageAsync(String message) throws IOException {
        LOGGER.info("SQLiteQueueClient.sendMessageAsync called");
        
        // Clear pending tool requests from previous interactions
        pendingToolRequests.clear();
        
        // Send the message
        sendMessage(message);
        
        // Send initial status update
        sendStatusUpdate("AI is thinking...");
    }
    
    /**
     * Send a status update to the main database for polling service to send
     */
    private void sendStatusUpdate(String status) {
        if (config.getFileChatService() != null && config.getSessionId() != null) {
            try {
                config.getFileChatService().createChatMessage(
                    config.getSessionId(),
                    ChatConstants.AGENT,  // sender
                    ChatConstants.CLIENT, // receiver  
                    status,
                    ChatConstants.DB_MESSAGE_TYPE_STATUS
                );
                LOGGER.fine("Sent status update: " + status);
            } catch (Exception e) {
                LOGGER.warning("Failed to send status update: " + e.getMessage());
            }
        }
    }
    
    /**
     * Acknowledge a message as read
     */
    private void acknowledgeMessage(long messageId) throws SQLException {
        // Guard against race condition where connection is closed by another thread
        Connection conn = this.connection;
        if (conn == null) {
            LOGGER.fine("Cannot acknowledge message " + messageId + " - connection is null");
            return;
        }
        try (PreparedStatement pstmt = conn.prepareStatement(ACK_MESSAGE_SQL)) {
            pstmt.setLong(1, messageId);
            pstmt.executeUpdate();
        }
    }
    
    /**
     * Check if klawed is still working (has pending tool requests)
     */
    public boolean isKlawedWorking() {
        return !pendingToolRequests.isEmpty();
    }
    
    /**
     * Get number of pending tool requests
     */
    public int getPendingToolRequestCount() {
        return pendingToolRequests.size();
    }
    
    /**
     * Get queue statistics
     */
    public QueueStats getQueueStats() throws IOException {
        if (!connected.get() || connection == null) {
            throw new IOException("SQLiteQueueClient not connected");
        }
        
        try {
            int pendingCount = 0;
            int totalCount = 0;
            int unreadCount = 0;
            
            // Count pending messages
            try (Statement stmt = connection.createStatement();
                 ResultSet rs = stmt.executeQuery(COUNT_PENDING_SQL)) {
                if (rs.next()) {
                    pendingCount = rs.getInt(1);
                }
            }
            
            // Count total messages
            try (Statement stmt = connection.createStatement();
                 ResultSet rs = stmt.executeQuery(COUNT_TOTAL_SQL)) {
                if (rs.next()) {
                    totalCount = rs.getInt(1);
                }
            }
            
            // Count unread messages for this sender and session
            try (PreparedStatement pstmt = connection.prepareStatement(COUNT_UNREAD_SQL)) {
                pstmt.setString(1, config.getSenderName());
                try (ResultSet rs = pstmt.executeQuery()) {
                    if (rs.next()) {
                        unreadCount = rs.getInt(1);
                    }
                }
            }
            
            return new QueueStats(pendingCount, totalCount, unreadCount);
            
        } catch (SQLException e) {
            throw new IOException("Failed to get queue statistics", e);
        }
    }
    
    /**
     * Check if client is connected
     */
    public boolean isConnected() {
        return connected.get() && connection != null;
    }
    
    /**
     * Shutdown the client
     */
    public void shutdown() {
        LOGGER.info("Shutting down SQLiteQueueClient");
        
        connected.set(false);
        
        if (connection != null) {
            try {
                connection.close();
            } catch (SQLException e) {
                LOGGER.warning("Error closing SQLite connection: " + e.getMessage());
            }
            connection = null;
        }
        
        executorService.shutdown();
        try {
            if (!executorService.awaitTermination(5, TimeUnit.SECONDS)) {
                executorService.shutdownNow();
            }
        } catch (InterruptedException e) {
            executorService.shutdownNow();
            Thread.currentThread().interrupt();
        }
        
        LOGGER.info("SQLiteQueueClient shutdown complete");
    }
    
    /**
     * Data class for queue statistics
     */
    public static class QueueStats {
        private final int pendingCount;
        private final int totalCount;
        private final int unreadCount;
        
        public QueueStats(int pendingCount, int totalCount, int unreadCount) {
            this.pendingCount = pendingCount;
            this.totalCount = totalCount;
            this.unreadCount = unreadCount;
        }
        
        public int getPendingCount() { return pendingCount; }
        public int getTotalCount() { return totalCount; }
        public int getUnreadCount() { return unreadCount; }
        
        @Override
        public String toString() {
            return String.format("QueueStats[pending=%d, total=%d, unread=%d]",
                pendingCount, totalCount, unreadCount);
        }
    }
}
