package com.filesurf.repository;

import com.filesurf.db.SQLiteManager;
import com.filesurf.model.FeedbackRecord;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.sql.*;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

/**
 * Repository for feedback management operations.
 * Stores user feedback in SQLite database.
 */
@ApplicationScoped
public class FeedbackRepository {
    private static final Logger LOGGER = Logger.getLogger(FeedbackRepository.class.getName());

    @Inject
    SQLiteManager sqliteManager;

    /**
     * Initialize the feedback table schema
     */
    public void initializeSchema() {
        try {
            sqliteManager.execute(conn -> {
                try (Statement stmt = conn.createStatement()) {
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
                            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                        )
                    """);

                    // Create indexes for performance
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_feedback_id ON feedback(feedback_id)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_user_id ON feedback(user_id)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_type ON feedback(type)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_feedback_created_at ON feedback(created_at)");

                    LOGGER.info("Feedback table schema initialized successfully");
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to initialize feedback table schema: " + e.getMessage());
            throw new RuntimeException("Database initialization failed", e);
        }
    }

    /**
     * Create a new feedback entry
     */
    public FeedbackRecord create(String feedbackId, String type, String description,
                                 String userId, String userEmail, String errorDetails,
                                 String environment) {
        try {
            return sqliteManager.executeInTransaction(conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        """
                        INSERT INTO feedback (feedback_id, type, description, user_id, user_email, 
                                            error_details, environment, created_at)
                        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        Statement.RETURN_GENERATED_KEYS)) {
                    LocalDateTime now = LocalDateTime.now();
                    ps.setString(1, feedbackId);
                    ps.setString(2, type);
                    ps.setString(3, description);
                    ps.setString(4, userId);
                    ps.setString(5, userEmail);
                    ps.setString(6, errorDetails);
                    ps.setString(7, environment);
                    ps.setTimestamp(8, Timestamp.valueOf(now));
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
            LOGGER.severe("Failed to create feedback: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Find feedback by its unique feedback ID
     */
    public FeedbackRecord findByFeedbackId(String feedbackId) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<FeedbackRecord>) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM feedback WHERE feedback_id = ?")) {
                    ps.setString(1, feedbackId);
                    try (ResultSet rs = ps.executeQuery()) {
                        if (rs.next()) {
                            return FeedbackRecord.fromResultSet(rs);
                        }
                    }
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find feedback by feedbackId: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Find all feedback from a specific user
     */
    public List<FeedbackRecord> findByUserId(String userId) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<List<FeedbackRecord>>) conn -> {
                List<FeedbackRecord> feedbacks = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM feedback WHERE user_id = ? ORDER BY created_at DESC")) {
                    ps.setString(1, userId);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            feedbacks.add(FeedbackRecord.fromResultSet(rs));
                        }
                    }
                }
                return feedbacks;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find feedback by userId: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Find all feedback of a specific type
     */
    public List<FeedbackRecord> findByType(String type) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<List<FeedbackRecord>>) conn -> {
                List<FeedbackRecord> feedbacks = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM feedback WHERE type = ? ORDER BY created_at DESC")) {
                    ps.setString(1, type);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            feedbacks.add(FeedbackRecord.fromResultSet(rs));
                        }
                    }
                }
                return feedbacks;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find feedback by type: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Get all feedback ordered by date (newest first)
     */
    public List<FeedbackRecord> findAll(int limit, int offset) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<List<FeedbackRecord>>) conn -> {
                List<FeedbackRecord> feedbacks = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT * FROM feedback ORDER BY created_at DESC LIMIT ? OFFSET ?")) {
                    ps.setInt(1, limit);
                    ps.setInt(2, offset);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            feedbacks.add(FeedbackRecord.fromResultSet(rs));
                        }
                    }
                }
                return feedbacks;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to find all feedback: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Count total feedback entries
     */
    public long count() {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<Long>) conn -> {
                try (Statement stmt = conn.createStatement();
                     ResultSet rs = stmt.executeQuery("SELECT COUNT(*) FROM feedback")) {
                    if (rs.next()) {
                        return rs.getLong(1);
                    }
                }
                return 0L;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to count feedback: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Count feedback by type
     */
    public long countByType(String type) {
        try {
            return sqliteManager.execute((SQLiteManager.ConnectionConsumer<Long>) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "SELECT COUNT(*) FROM feedback WHERE type = ?")) {
                    ps.setString(1, type);
                    try (ResultSet rs = ps.executeQuery()) {
                        if (rs.next()) {
                            return rs.getLong(1);
                        }
                    }
                }
                return 0L;
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to count feedback by type: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    /**
     * Delete feedback by ID (for admin purposes)
     */
    public boolean delete(String feedbackId) {
        try {
            return sqliteManager.executeInTransaction(conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                        "DELETE FROM feedback WHERE feedback_id = ?")) {
                    ps.setString(1, feedbackId);
                    int rowsAffected = ps.executeUpdate();
                    return rowsAffected > 0;
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("Failed to delete feedback: " + e.getMessage());
            throw new RuntimeException("Database operation failed", e);
        }
    }

    private FeedbackRecord findById(Connection conn, Long id) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
                "SELECT * FROM feedback WHERE id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return FeedbackRecord.fromResultSet(rs);
                }
            }
        }
        return null;
    }
}
