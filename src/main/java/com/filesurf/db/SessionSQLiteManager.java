package com.filesurf.db;

import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.*;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Logger;

/**
 * Manages SQLite connections for per-session chat message databases.
 * Each session has its own SQLite database file in the chat-messages directory.
 */
@ApplicationScoped
public class SessionSQLiteManager {
    private static final Logger LOGGER = Logger.getLogger(SessionSQLiteManager.class.getName());

    @ConfigProperty(name = "chat.messages.db-dir", defaultValue = "./data/chat-messages")
    String chatMessagesDbDir;

    // Pool of connections indexed by sessionId
    private final Map<String, Connection> connectionPool = new ConcurrentHashMap<>();
    private final Object lock = new Object();

    /**
     * Get or create a SQLite connection for a session.
     */
    public Connection getConnection(String sessionId) throws SQLException {
        return connectionPool.computeIfAbsent(sessionId, sid -> {
            try {
                String dbFileName = "chat_messages_" + sid + ".db";
                String dbPath = chatMessagesDbDir + "/" + dbFileName;
                String jdbcUrl = "jdbc:sqlite:" + dbPath;
                
                // Ensure directory exists before creating database
                ensureDirectoryExists(chatMessagesDbDir);
                
                Connection conn = DriverManager.getConnection(jdbcUrl);
                
                // Set PRAGMAs for optimal SQLite performance
                try (Statement stmt = conn.createStatement()) {
                    stmt.execute("PRAGMA journal_mode = WAL");
                    stmt.execute("PRAGMA synchronous = NORMAL");
                    stmt.execute("PRAGMA busy_timeout = 5000");
                    stmt.execute("PRAGMA foreign_keys = ON");
                    stmt.execute("PRAGMA cache_size = -2000");
                    stmt.execute("PRAGMA mmap_size = 268435456");
                    stmt.execute("PRAGMA temp_store = MEMORY");
                    stmt.execute("PRAGMA encoding = 'UTF-8'");
                }
                
                LOGGER.info("[SESSION:" + sid + "] Created SQLite connection to " + dbFileName);
                return conn;
            } catch (SQLException e) {
                LOGGER.severe("[SESSION:" + sid + "] Failed to create SQLite connection: " + e.getMessage());
                throw new RuntimeException("Failed to create SQLite connection for session " + sid, e);
            }
        });
    }

    /**
     * Execute an operation with a session-specific connection.
     */
    public <T> T execute(String sessionId, ConnectionConsumer<T> operation) throws SQLException {
        synchronized(lock) {
            Connection conn = getConnection(sessionId);
            return operation.accept(conn);
        }
    }

    /**
     * Execute an operation with a session-specific connection (void version).
     */
    public void execute(String sessionId, ConnectionOperation operation) throws SQLException {
        synchronized(lock) {
            Connection conn = getConnection(sessionId);
            operation.accept(conn);
        }
    }

    /**
     * Execute an operation in a transaction with a session-specific connection.
     */
    public <T> T executeInTransaction(String sessionId, ConnectionConsumer<T> operation) throws SQLException {
        return execute(sessionId, conn -> {
            boolean autoCommit = conn.getAutoCommit();
            try {
                conn.setAutoCommit(false);
                T result = operation.accept(conn);
                conn.commit();
                return result;
            } catch (SQLException e) {
                conn.rollback();
                throw e;
            } finally {
                conn.setAutoCommit(autoCommit);
            }
        });
    }

    /**
     * Close and remove a session connection.
     */
    public void closeSession(String sessionId) {
        Connection conn = connectionPool.remove(sessionId);
        if (conn != null) {
            try {
                conn.close();
                LOGGER.info("[SESSION:" + sessionId + "] Closed SQLite connection");
            } catch (SQLException e) {
                LOGGER.warning("[SESSION:" + sessionId + "] Error closing SQLite connection: " + e.getMessage());
            }
        }
    }

    /**
     * Ensures the directory exists, creating it if necessary.
     */
    private void ensureDirectoryExists(String dirPath) {
        try {
            Path path = Paths.get(dirPath);
            if (!Files.exists(path)) {
                Files.createDirectories(path);
                LOGGER.info("Created directory for chat messages: " + dirPath);
            }
        } catch (Exception e) {
            LOGGER.severe("Failed to create directory '" + dirPath + "': " + e.getMessage());
            throw new RuntimeException("Failed to create directory for chat messages: " + dirPath, e);
        }
    }

    @PreDestroy
    void cleanup() {
        LOGGER.info("Cleaning up SessionSQLiteManager...");
        for (Map.Entry<String, Connection> entry : connectionPool.entrySet()) {
            try {
                entry.getValue().close();
                LOGGER.info("[SESSION:" + entry.getKey() + "] Closed SQLite connection during cleanup");
            } catch (SQLException e) {
                LOGGER.warning("[SESSION:" + entry.getKey() + "] Error closing SQLite connection during cleanup: " + e.getMessage());
            }
        }
        connectionPool.clear();
        LOGGER.info("SessionSQLiteManager cleanup complete");
    }

    @FunctionalInterface
    public interface ConnectionConsumer<T> {
        T accept(Connection conn) throws SQLException;
    }

    @FunctionalInterface
    public interface ConnectionOperation {
        void accept(Connection conn) throws SQLException;
    }
}