package com.filesurf.repository;

import com.filesurf.db.ContactFormDbManager;
import com.filesurf.model.ContactFormEntry;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.sql.*;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

@ApplicationScoped
public class ContactFormRepository {
    private static final Logger LOGGER = Logger.getLogger(ContactFormRepository.class.getName());

    @Inject
    ContactFormDbManager dbManager;

    public void initializeSchema() {
        try {
            dbManager.execute(conn -> {
                try (Statement stmt = conn.createStatement()) {
                    stmt.execute("""
                        CREATE TABLE IF NOT EXISTS contact_forms (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            email TEXT NOT NULL,
                            company TEXT,
                            message TEXT,
                            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                        )
                    """);
                    LOGGER.info("Contact forms table schema initialized successfully");
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to initialize contact forms table schema: " + e.getMessage());
            throw new RuntimeException("Database initialization failed", e);
        }
    }

    public ContactFormEntry insert(String email, String company, String message) {
        try {
            return dbManager.executeInTransaction(conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "INSERT INTO contact_forms (email, company, message, created_at) VALUES (?, ?, ?, ?)",
                        Statement.RETURN_GENERATED_KEYS)) {
                    LocalDateTime now = LocalDateTime.now();
                    ps.setString(1, email != null ? email.toLowerCase().trim() : null);
                    ps.setString(2, company != null ? company.trim() : null);
                    ps.setString(3, message != null ? message.trim() : null);
                    ps.setTimestamp(4, Timestamp.valueOf(now));
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
            LOGGER.severe("Failed to insert contact form entry: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    public List<ContactFormEntry> findAll() {
        try {
            return dbManager.execute(conn -> {
                List<ContactFormEntry> entries = new ArrayList<>();
                try (Statement stmt = conn.createStatement();
                     ResultSet rs = stmt.executeQuery("SELECT * FROM contact_forms ORDER BY created_at DESC")) {
                    while (rs.next()) {
                        entries.add(ContactFormEntry.fromResultSet(rs));
                    }
                }
                return entries;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find all contact form entries: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    public long count() {
        try {
            return dbManager.execute(conn -> {
                try (Statement stmt = conn.createStatement();
                     ResultSet rs = stmt.executeQuery("SELECT COUNT(*) FROM contact_forms")) {
                    return rs.next() ? rs.getLong(1) : 0;
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to count contact form entries: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    private ContactFormEntry findById(Connection conn, Long id) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement("SELECT * FROM contact_forms WHERE id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return ContactFormEntry.fromResultSet(rs);
                }
            }
        }
        return null;
    }
}
