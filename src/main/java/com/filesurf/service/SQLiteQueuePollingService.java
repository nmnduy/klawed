package com.filesurf.service;

import io.quarkus.scheduler.Scheduled;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.control.ActivateRequestContext;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Logger;

/**
 * Background service that polls SQLite queue databases for klawed responses
 * and saves them to the main application database.
 * 
 * This service bridges the gap between:
 * 1. Klawed writing responses to SQLite queue
 * 2. Application database (chat_message table)
 * 3. WebSocket delivery (handled by ChatMessagePollingService)
 */
@ApplicationScoped
public class SQLiteQueuePollingService {
    
    private static final Logger LOGGER = Logger.getLogger(SQLiteQueuePollingService.class.getName());
    
    @Inject
    FileChatService fileChatService;
    
    @ConfigProperty(name = "klawed.sqlite-queue.sender-name", defaultValue = "client")
    String senderName;
    
    @ConfigProperty(name = "klawed.sqlite-queue.receiver-name", defaultValue = "klawed")
    String receiverName;
    
    // Persistent storage root path (matches SessionManager)
    @ConfigProperty(name = "filesurf.persist.root", defaultValue = "./data/persistent")
    String persistRoot;
    
    // Track active sessions that need polling
    // Map: sessionId -> userId
    private final Map<String, String> activeSessions = new ConcurrentHashMap<>();
    
    /**
     * Register a session for SQLite queue polling
     */
    public void registerSession(String sessionId, String userId) {
        activeSessions.put(sessionId, userId);
        LOGGER.info("[SESSION:" + sessionId + "] Registered for SQLite queue polling (total active: " + activeSessions.size() + ")");
    }
    
    /**
     * Unregister a session from SQLite queue polling
     */
    public void unregisterSession(String sessionId) {
        activeSessions.remove(sessionId);
        LOGGER.info("[SESSION:" + sessionId + "] Unregistered from SQLite queue polling (total active: " + activeSessions.size() + ")");
    }
    
    /**
     * Poll SQLite queues for klawed responses every 500ms
     */
    @Scheduled(every = "0.5s")
    @ActivateRequestContext
    public void pollSQLiteQueues() {
        if (activeSessions.isEmpty()) {
            return; // Silent - no logging when there are no active sessions
        }
        
        // Poll each active session's SQLite queue
        for (Map.Entry<String, String> entry : activeSessions.entrySet()) {
            String sessionId = entry.getKey();
            String userId = entry.getValue();
            
            try {
                pollSessionQueue(sessionId, userId);
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error polling SQLite queue: " + e.getMessage());
            }
        }
    }
    
    /**
     * Poll a specific session's SQLite queue for klawed responses
     */
    private void pollSessionQueue(String sessionId, String userId) {
        // Construct workspace directory path
        Path workspaceDir = Path.of(persistRoot).resolve(userId);
        
        // Determine SQLite database path
        String dbFileName = "klawed_messages_" + sessionId + ".db";
        Path sqliteDbPath = workspaceDir.resolve(dbFileName);
        
        // Check if database exists
        if (!Files.exists(sqliteDbPath)) {
            // Database doesn't exist yet (klawed container hasn't started or created it)
            return;
        }
        
        // Create SQLiteQueueClient to poll for messages
        SQLiteQueueClient.Config config = new SQLiteQueueClient.Config(sqliteDbPath.toString())
            .withSenderName(receiverName)  // We receive from klawed
            .withReceiverName(senderName)  // Klawed receives from client
            .withSessionId(sessionId)
            .withFileChatService(fileChatService)
            .withPollTimeoutMs(100);  // Short timeout for quick polls
        
        SQLiteQueueClient queueClient = new SQLiteQueueClient(config);
        
        try {
            // Connect and poll for messages
            queueClient.connect();
            
            // receiveMessages() will automatically save messages to the main database
            // via FileChatService (see SQLiteQueueClient.receiveMessages() implementation)
            List<String> messages = queueClient.receiveMessages();
            
            if (!messages.isEmpty()) {
                LOGGER.info("[SESSION:" + sessionId + "] Received " + messages.size() + " message(s) from klawed");
            }
            
        } catch (Exception e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Error receiving messages from SQLite queue: " + e.getMessage());
        } finally {
            // Always clean up
            try {
                queueClient.shutdown();
            } catch (Exception e) {
                // Ignore shutdown errors
            }
        }
    }
}
