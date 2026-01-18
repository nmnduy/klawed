package com.filesurf.db;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.*;
import java.util.logging.Logger;

/**
 * Manages SQLite connection for contact form submissions.
 * Uses a dedicated database file in the data directory.
 */
@ApplicationScoped
public class ContactFormDbManager {
    private static final Logger LOGGER = Logger.getLogger(ContactFormDbManager.class.getName());

    private static final String CONTACT_DB_PATH = "data/contact_forms.db";

    private Connection connection;
    private final Object lock = new Object();

    @PostConstruct
    void init() throws SQLException {
        LOGGER.info("Initializing ContactFormDbManager...");
        ensureDirectoryExists("data");
        connection = DriverManager.getConnection("jdbc:sqlite:" + CONTACT_DB_PATH);

        // Set PRAGMAs for optimal SQLite performance
        try (Statement stmt = connection.createStatement()) {
            stmt.execute("PRAGMA journal_mode = WAL");
            stmt.execute("PRAGMA synchronous = NORMAL");
            stmt.execute("PRAGMA busy_timeout = 5000");
            stmt.execute("PRAGMA foreign_keys = ON");
            stmt.execute("PRAGMA cache_size = -2000");
            stmt.execute("PRAGMA mmap_size = 268435456");
            stmt.execute("PRAGMA temp_store = MEMORY");
            stmt.execute("PRAGMA encoding = 'UTF-8'");
        }

        LOGGER.info("ContactFormDbManager initialized with WAL mode and optimal PRAGMAs");
    }

    /**
     * Execute an operation with the contact forms connection.
     */
    public <T> T execute(ConnectionConsumer<T> operation) throws SQLException {
        synchronized(lock) {
            return operation.accept(connection);
        }
    }

    /**
     * Execute an operation with the contact forms connection (void version).
     */
    public void execute(ConnectionOperation operation) throws SQLException {
        synchronized(lock) {
            operation.accept(connection);
        }
    }

    /**
     * Execute an operation in a transaction with the contact forms connection.
     */
    public <T> T executeInTransaction(ConnectionConsumer<T> operation) throws SQLException {
        return execute(conn -> {
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
     * Ensures the directory exists, creating it if necessary.
     */
    private void ensureDirectoryExists(String dirPath) {
        try {
            Path path = Paths.get(dirPath);
            if (!Files.exists(path)) {
                Files.createDirectories(path);
                LOGGER.info("Created directory: " + dirPath);
            }
        } catch (Exception e) {
            LOGGER.severe("Failed to create directory '" + dirPath + "': " + e.getMessage());
            throw new RuntimeException("Failed to create directory: " + dirPath, e);
        }
    }

    @PreDestroy
    void cleanup() {
        LOGGER.info("Cleaning up ContactFormDbManager...");
        if (connection != null) {
            try {
                connection.close();
                LOGGER.info("Closed contact forms database connection");
            } catch (SQLException e) {
                LOGGER.warning("Error closing contact forms SQLite connection: " + e.getMessage());
            }
        }
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
