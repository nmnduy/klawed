package com.filesurf.repository;

import com.filesurf.db.SQLiteManager;
import com.filesurf.model.UserRecord;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.sql.*;
import java.time.Instant;
import java.util.logging.Logger;

/**
 * Repository for user management operations.
 * Links emails to userIds for authentication.
 */
@ApplicationScoped
public class UserRepository {
    private static final Logger LOGGER = Logger.getLogger(UserRepository.class.getName());

    @Inject
    SQLiteManager sqliteManager;

    /**
     * Initialize the users table schema
     */
    public void initializeSchema() {
        try {
            sqliteManager.execute(conn -> {
                try (Statement stmt = conn.createStatement()) {
                    // Create users table
                    stmt.execute("""
                        CREATE TABLE IF NOT EXISTS users (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            user_id TEXT NOT NULL UNIQUE,
                            email TEXT NOT NULL UNIQUE,
                            created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                            last_login_at INTEGER,
                            is_active BOOLEAN NOT NULL DEFAULT TRUE
                        )
                    """);

                    // Create indexes for performance
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_users_user_id ON users(user_id)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_users_email ON users(email)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_users_is_active ON users(is_active)");

                    LOGGER.info("Users table schema initialized successfully");
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to initialize users table schema: " + e.getMessage());
            throw new RuntimeException("Database initialization failed", e);
        }
    }

    /**
     * Find a user by their userId (the cookie value)
     */
    public UserRecord findByUserId(String userId) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<UserRecord>) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM users WHERE user_id = ?")) {
                    ps.setString(1, userId);
                    try (ResultSet rs = ps.executeQuery()) {
                        if (rs.next()) {
                            return UserRecord.fromResultSet(rs);
                        }
                    }
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find user by userId: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Find a user by their email address
     */
    public UserRecord findByEmail(String email) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<UserRecord>) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM users WHERE LOWER(email) = LOWER(?)")) {
                    ps.setString(1, email);
                    try (ResultSet rs = ps.executeQuery()) {
                        if (rs.next()) {
                            return UserRecord.fromResultSet(rs);
                        }
                    }
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find user by email: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Create a new user with the given email.
     * Generates a new userId.
     */
    public UserRecord createUser(String email, String userId) {
        try {
            return sqliteManager.executeInTransaction(conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "INSERT INTO users (user_id, email, created_at, last_login_at, is_active) VALUES (?, ?, ?, ?, ?)",
                        Statement.RETURN_GENERATED_KEYS)) {
                    long now = Instant.now().getEpochSecond();
                    ps.setString(1, userId);
                    ps.setString(2, email.toLowerCase().trim());
                    ps.setLong(3, now);
                    ps.setLong(4, now);
                    ps.setBoolean(5, true);
                    ps.executeUpdate();

                    // Get generated ID
                    try (ResultSet rs = ps.getGeneratedKeys()) {
                        if (rs.next()) {
                            Long id = rs.getLong(1);
                            return findById(conn, id);
                        }
                    }
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to create user: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Update the last login timestamp for a user
     */
    public void updateLastLogin(String userId) {
        try {
            sqliteManager.execute((SQLiteManager.ConnectionOperation) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "UPDATE users SET last_login_at = ? WHERE user_id = ?")) {
                    ps.setLong(1, Instant.now().getEpochSecond());
                    ps.setString(2, userId);
                    ps.executeUpdate();
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to update last login: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Check if an email is already registered
     */
    public boolean emailExists(String email) {
        UserRecord user = findByEmail(email);
        return user != null;
    }

    /**
     * Check if a userId exists in the database
     */
    public boolean userIdExists(String userId) {
        UserRecord user = findByUserId(userId);
        return user != null;
    }

    private UserRecord findById(Connection conn, Long id) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
                "SELECT * FROM users WHERE id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return UserRecord.fromResultSet(rs);
                }
            }
        }
        return null;
    }
}
