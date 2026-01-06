package com.filesurf.db;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import java.sql.*;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.logging.Logger;

@ApplicationScoped
public class SQLiteManager {
    private static final Logger LOGGER = Logger.getLogger(SQLiteManager.class.getName());
    
    private Connection connection;
    private final Object lock = new Object();
    
    @PostConstruct
    void init() throws SQLException {
        LOGGER.info("Initializing SQLiteManager...");
        // Create single connection
        connection = DriverManager.getConnection("jdbc:sqlite:data/filesurf.db");
        
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
        
        LOGGER.info("SQLiteManager initialized with WAL mode and optimal PRAGMAs");
    }
    
    @PreDestroy
    void cleanup() {
        LOGGER.info("Cleaning up SQLiteManager...");
        if (connection != null) {
            try {
                connection.close();
            } catch (SQLException e) {
                LOGGER.warning("Error closing SQLite connection: " + e.getMessage());
            }
        }
    }
    
    public <T> T execute(ConnectionConsumer<T> operation) throws SQLException {
        synchronized(lock) {
            return operation.accept(connection);
        }
    }
    
    public void execute(ConnectionOperation operation) throws SQLException {
        synchronized(lock) {
            operation.accept(connection);
        }
    }
    
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
    
    public <T> T executeWithTimeout(ConnectionConsumer<T> operation, long timeoutMs) 
            throws TimeoutException, SQLException {
        ExecutorService executor = Executors.newSingleThreadExecutor();
        Future<T> future = executor.submit(() -> execute(operation));
        
        try {
            return future.get(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (TimeoutException e) {
            future.cancel(true);
            throw e;
        } catch (Exception e) {
            if (e.getCause() instanceof SQLException) {
                throw (SQLException) e.getCause();
            }
            throw new RuntimeException(e);
        } finally {
            executor.shutdown();
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