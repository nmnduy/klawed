package com.filesurf.service;

import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.nio.file.Path;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Logger;

/**
 * Pool of reusable SQLiteQueueClient instances, one per session.
 * This avoids creating and destroying connections on every poll cycle.
 */
@ApplicationScoped
public class SQLiteQueueClientPool {

    private static final Logger LOGGER = Logger.getLogger(SQLiteQueueClientPool.class.getName());

    @Inject
    FileChatService fileChatService;

    @Inject
    MetricsService metricsService;

    @ConfigProperty(name = "klawed.sqlite-queue.sender-name", defaultValue = "client")
    String senderName;

    @ConfigProperty(name = "klawed.sqlite-queue.receiver-name", defaultValue = "klawed")
    String receiverName;

    @ConfigProperty(name = "klawed.sqlite-queue.db-dir", defaultValue = "./data/klawed-messages")
    String sqliteQueueDbDir;

    // Pool of clients indexed by sessionId
    // Each session has ONE client configured for RECEIVING (sender=klawed, receiver=client)
    // This is the most common operation (polling every 500ms)
    // For SENDING, we use sendMessage(message, "klawed") to override the receiver
    private final Map<String, SQLiteQueueClient> clientPool = new ConcurrentHashMap<>();

    /**
     * Get or create a single SQLiteQueueClient for a session.
     * Configured for receiving messages FROM klawed (sender=klawed, receiver=client).
     * Each session has its own dedicated SQLite DB file, so only one connection needed.
     */
    public SQLiteQueueClient getOrCreateClient(String sessionId, String userId) {
        return clientPool.computeIfAbsent(sessionId, sid -> {
            String dbFileName = "klawed_messages_" + sid + ".db";
            Path sqliteDbPath = Path.of(sqliteQueueDbDir).resolve(dbFileName);

            // Configure for RECEIVING from klawed (most common operation - polling)
            SQLiteQueueClient.Config config = new SQLiteQueueClient.Config(sqliteDbPath.toString())
                .withSenderName(receiverName)  // sender="klawed" (we receive FROM klawed)
                .withReceiverName(senderName)  // receiver="client" (messages TO client)
                .withSessionId(sid)
                .withUserId(userId)
                .withFileChatService(fileChatService)
                .withMetricsService(metricsService)
                .withPollIntervalMs(1000)
                .withPollTimeoutMs(1000);

            SQLiteQueueClient client = new SQLiteQueueClient(config);

            try {
                client.connect();
                LOGGER.info("[SESSION:" + sid + "] Created and connected SQLiteQueueClient");
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sid + "] Failed to connect SQLiteQueueClient: " + e.getMessage());
                throw new RuntimeException("Failed to connect SQLiteQueueClient for session " + sid, e);
            }

            return client;
        });
    }

    /**
     * Get or create a single SQLiteQueueClient for a session.
     * Backward compatibility - calls getOrCreateClient(sessionId, null)
     */
    public SQLiteQueueClient getOrCreateClient(String sessionId) {
        return getOrCreateClient(sessionId, null);
    }

    /**
     * @deprecated Use getOrCreateClient() instead
     */
    @Deprecated
    public SQLiteQueueClient getOrCreateReceiver(String sessionId) {
        return getOrCreateClient(sessionId);
    }

    /**
     * @deprecated Use getOrCreateClient() instead
     * Note: The client is configured for receiving, so when sending,
     * you must use sendMessage(message, "klawed") to override the receiver
     */
    @Deprecated
    public SQLiteQueueClient getOrCreateSender(String sessionId) {
        return getOrCreateClient(sessionId);
    }

    /**
     * Remove and shutdown the client for a session
     */
    public void removeSession(String sessionId) {
        SQLiteQueueClient client = clientPool.remove(sessionId);

        if (client != null) {
            try {
                client.shutdown();
                LOGGER.info("[SESSION:" + sessionId + "] Shutdown and removed SQLiteQueueClient");
            } catch (Exception e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error shutting down client: " + e.getMessage());
            }
        }
    }

    /**
     * Shutdown all clients on application shutdown
     */
    @PreDestroy
    public void shutdown() {
        LOGGER.info("SQLiteQueueClientPool shutting down");
        shutdownAll();
    }

    /**
     * Shutdown all clients in the pool
     */
    public void shutdownAll() {
        LOGGER.info("Shutting down all SQLiteQueueClient instances in pool (total: " + clientPool.size() + ")");

        for (Map.Entry<String, SQLiteQueueClient> entry : clientPool.entrySet()) {
            try {
                entry.getValue().shutdown();
            } catch (Exception e) {
                LOGGER.warning("Error shutting down client for key " + entry.getKey() + ": " + e.getMessage());
            }
        }

        clientPool.clear();
        LOGGER.info("All SQLiteQueueClient instances shutdown and pool cleared");
    }

    /**
     * Get the number of active clients in the pool
     */
    public int getPoolSize() {
        return clientPool.size();
    }
}
