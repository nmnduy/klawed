package com.filesurf.db;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.*;
import java.util.logging.Logger;

/**
 * Database manager for feedback storage.
 * Maintains a separate SQLite database for user feedback.
 * Similar to KlawedSandboxService sessions database pattern.
 */
@ApplicationScoped
public class FeedbackDatabaseManager {
    private static final Logger LOGGER = Logger.getLogger(FeedbackDatabaseManager.class.getName());

    @ConfigProperty(name = "feedback.db.path", defaultValue = "data/feedback.db")
    String feedbackDbPath;

    private String jdbcUrl;
    private Connection connection;
    private final Object lock = new Object();

    @PostConstruct
    public void init() {
        try {
            // Create parent directory if it doesn't exist
            Path dbFile = Path.of(feedbackDbPath);
            Path parentDir = dbFile.getParent();
            if (parentDir != null && !Files.exists(parentDir)) {
                Files.createDirectories(parentDir);
                LOGGER.info("Created feedback database directory: " + parentDir);
            }

            // Initialize JDBC URL and connection
            jdbcUrl = "jdbc:sqlite:" + feedbackDbPath;
            connection = DriverManager.getConnection(jdbcUrl);

            // Set PRAGMAs for optimal SQLite performance
            try (Statement stmt = connection.createStatement()) {
                stmt.execute("PRAGMA journal_mode = WAL");
                stmt.execute("PRAGMA synchronous = NORMAL");
                stmt.execute("PRAGMA busy_timeout = 5000");
                stmt.execute("PRAGMA foreign_keys = ON");
                stmt.execute("PRAGMA cache_size = -2000");
                stmt.execute("PRAGMA temp_store = MEMORY");
            }

            LOGGER.info("Feedback database initialized: " + feedbackDbPath);

            // Initialize schema
            initializeSchema();
        } catch (Exception e) {
            throw new RuntimeException("Failed to initialize feedback database", e);
        }
    }

    /**
     * Initialize the feedback table schema
     */
    private void initializeSchema() throws SQLException {
        synchronized (lock) {
            try (Statement stmt = connection.createStatement()) {
                // Create feedback table
                stmt.execute("""
                    CREATE TABLE IF NOT EXISTS feedback (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        feedback_id TEXT NOT NULL UNIQUE,
                        type TEXT NOT NULL,
                        description TEXT NOT NULL,
                        user_id TEXT NOT NULL,
                        user_email TEXT NOT NULL,
                        error_details TEXT,
                        environment TEXT,
                        created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
                    )
                """);

                // Create indexes for performance
                stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_feedback_id ON feedback(feedback_id)");
                stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_user_id ON feedback(user_id)");
                stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_type ON feedback(type)");
                stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_created_at ON feedback(created_at)");

                LOGGER.info("Feedback table schema initialized successfully");
            }
        }
    }

    @PreDestroy
    public void cleanup() {
        LOGGER.info("Cleaning up FeedbackDatabaseManager...");
        if (connection != null) {
            try {
                connection.close();
            } catch (SQLException e) {
                LOGGER.warning("Error closing feedback database connection: " + e.getMessage());
            }
        }
    }

    /**
     * Execute a database operation with the connection
     */
    public <T> T execute(ConnectionConsumer<T> operation) throws SQLException {
        synchronized (lock) {
            return operation.accept(connection);
        }
    }

    /**
     * Execute a database operation without a return value
     */
    public void execute(ConnectionOperation operation) throws SQLException {
        synchronized (lock) {
            operation.accept(connection);
        }
    }

    /**
     * Execute a database operation in a transaction
     */
    public <T> T executeInTransaction(ConnectionConsumer<T> operation) throws SQLException {
        synchronized (lock) {
            boolean autoCommit = connection.getAutoCommit();
            try {
                connection.setAutoCommit(false);
                T result = operation.accept(connection);
                connection.commit();
                return result;
            } catch (SQLException e) {
                connection.rollback();
                throw e;
            } finally {
                connection.setAutoCommit(autoCommit);
            }
        }
    }

    /**
     * Functional interface for database operations that return a value
     */
    @FunctionalInterface
    public interface ConnectionConsumer<T> {
        T accept(Connection conn) throws SQLException;
    }

    /**
     * Functional interface for database operations that don't return a value
     */
    @FunctionalInterface
    public interface ConnectionOperation {
        void accept(Connection conn) throws SQLException;
    }
}
