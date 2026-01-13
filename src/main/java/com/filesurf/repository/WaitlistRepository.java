package com.filesurf.repository;

import com.filesurf.db.SQLiteManager;
import com.filesurf.model.WaitlistEntry;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.sql.*;
import java.time.LocalDateTime;
import java.util.logging.Logger;

@ApplicationScoped
public class WaitlistRepository {
    private static final Logger LOGGER = Logger.getLogger(WaitlistRepository.class.getName());

    @Inject
    SQLiteManager sqliteManager;

    public void initializeSchema() {
        try {
            sqliteManager.execute(conn -> {
                try (Statement stmt = conn.createStatement()) {
                    stmt.execute("""
                        CREATE TABLE IF NOT EXISTS waitlist (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            email TEXT NOT NULL UNIQUE,
                            name TEXT,
                            use_case TEXT,
                            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
                            updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                        )
                    """);
                    stmt.execute("CREATE UNIQUE INDEX IF NOT EXISTS idx_waitlist_email ON waitlist(email)");
                    LOGGER.info("Waitlist table schema initialized successfully");
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to initialize waitlist table schema: " + e.getMessage());
            throw new RuntimeException("Database initialization failed", e);
        }
    }

    public WaitlistEntry findByEmail(String email) {
        if (email == null || email.trim().isEmpty()) {
            return null;
        }
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<WaitlistEntry>) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM waitlist WHERE LOWER(email) = LOWER(?)")) {
                    ps.setString(1, email.toLowerCase().trim());
                    try (ResultSet rs = ps.executeQuery()) {
                        if (rs.next()) {
                            return WaitlistEntry.fromResultSet(rs);
                        }
                    }
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find waitlist entry by email: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    public WaitlistEntry insert(String email, String name, String useCase) {
        try {
            return sqliteManager.executeInTransaction(conn -> {
                // If already exists, return existing row to keep id stable
                WaitlistEntry existing = findByEmail(email);
                if (existing != null) {
                    return existing;
                }

                try (PreparedStatement ps = conn.prepareStatement(
                        "INSERT INTO waitlist (email, name, use_case, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
                        Statement.RETURN_GENERATED_KEYS)) {
                    LocalDateTime now = LocalDateTime.now();
                    ps.setString(1, email.toLowerCase().trim());
                    ps.setString(2, name != null ? name.trim() : null);
                    ps.setString(3, useCase != null ? useCase.trim() : null);
                    ps.setTimestamp(4, Timestamp.valueOf(now));
                    ps.setTimestamp(5, Timestamp.valueOf(now));
                    ps.executeUpdate();

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
            LOGGER.severe("Failed to insert waitlist entry: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    private WaitlistEntry findById(Connection conn, Long id) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement("SELECT * FROM waitlist WHERE id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return WaitlistEntry.fromResultSet(rs);
                }
            }
        }
        return null;
    }
}
