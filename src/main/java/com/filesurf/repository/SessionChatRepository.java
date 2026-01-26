package com.filesurf.repository;

import com.filesurf.db.SessionSQLiteManager;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatConstants;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import java.sql.*;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

/**
 * Repository for managing chat messages in per-session SQLite databases.
 * Each session has its own database file in the chat-messages directory.
 * The session_id is implicit in the database filename, so no separate chat_session table is needed.
 */
@ApplicationScoped
public class SessionChatRepository {
    private static final Logger LOGGER = Logger.getLogger(SessionChatRepository.class.getName());

    @Inject
    SessionSQLiteManager sessionSQLiteManager;

    /**
     * Initialize database schema for a session.
     * Creates the necessary tables if they don't exist.
     */
    public void initializeSchema(String sessionId) {
        // Schema is now initialized automatically when connection is created via SessionSQLiteManager
        LOGGER.info("[SESSION:" + sessionId + "] Schema initialization delegated to SessionSQLiteManager");
    }

    /**
     * Create a chat message in the session's database.
     */
    public ChatMessageRecord createChatMessage(String sessionId, String sender, String receiver,
                                              String content, String messageType) {
        try {
            return sessionSQLiteManager.executeInTransaction(sessionId, conn -> {
                // Insert new message - session_id is implicit in the database filename
                try (PreparedStatement ps = conn.prepareStatement(
                    "INSERT INTO chat_message (sender, receiver, content, message_type, created_at) VALUES (?, ?, ?, ?, ?)",
                    Statement.RETURN_GENERATED_KEYS)) {
                    ps.setString(1, sender);
                    ps.setString(2, receiver);
                    ps.setString(3, content);
                    ps.setString(4, messageType);
                    ps.setLong(5, Instant.now().getEpochSecond());
                    ps.executeUpdate();

                    // Get generated ID
                    try (ResultSet rs = ps.getGeneratedKeys()) {
                        if (rs.next()) {
                            Long id = rs.getLong(1);
                            return findChatMessageById(conn, id, sessionId);
                        }
                    }
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to create chat message: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public ChatMessageRecord findChatMessageById(String sessionId, Long id) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<ChatMessageRecord>) 
                conn -> findChatMessageById(conn, id, sessionId));
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find chat message: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    private ChatMessageRecord findChatMessageById(Connection conn, Long id, String sessionId) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
            "SELECT * FROM chat_message WHERE id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return ChatMessageRecord.fromResultSetWithSessionId(rs, sessionId);
                }
            }
        }
        return null;
    }

    public List<ChatMessageRecord> findUnsentMessages(String sessionId) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<List<ChatMessageRecord>>) conn -> {
                List<ChatMessageRecord> messages = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                    "SELECT * FROM chat_message WHERE sent = FALSE AND receiver = ? ORDER BY created_at")) {
                    ps.setString(1, ChatConstants.CLIENT);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSetWithSessionId(rs, sessionId));
                        }
                    }
                }
                return messages;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find unsent messages: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public List<ChatMessageRecord> findUnsentMessagesForSession(String sessionId) {
        // For session-specific repository, this is the same as findUnsentMessages
        return findUnsentMessages(sessionId);
    }

    public List<ChatMessageRecord> findMessagesBySession(String sessionId) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<List<ChatMessageRecord>>) conn -> {
                List<ChatMessageRecord> messages = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                    "SELECT * FROM chat_message ORDER BY created_at")) {
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSetWithSessionId(rs, sessionId));
                        }
                    }
                }
                return messages;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find messages by session: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public List<ChatMessageRecord> findMessagesBySessionAndSender(String sessionId, String sender) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<List<ChatMessageRecord>>) conn -> {
                List<ChatMessageRecord> messages = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                    "SELECT * FROM chat_message WHERE sender = ? ORDER BY created_at")) {
                    ps.setString(1, sender);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSetWithSessionId(rs, sessionId));
                        }
                    }
                }
                return messages;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find messages by session and sender: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public List<ChatMessageRecord> findMessagesBySessionAndReceiver(String sessionId, String receiver) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<List<ChatMessageRecord>>) conn -> {
                List<ChatMessageRecord> messages = new ArrayList<>();
                try (PreparedStatement ps = conn.prepareStatement(
                    "SELECT * FROM chat_message WHERE receiver = ? ORDER BY created_at")) {
                    ps.setString(1, receiver);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSetWithSessionId(rs, sessionId));
                        }
                    }
                }
                return messages;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find messages by session and receiver: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public List<ChatMessageRecord> findMessagesBySessionAndReceiverSince(String sessionId, String receiver, Long sinceTimestamp) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<List<ChatMessageRecord>>) conn -> {
                List<ChatMessageRecord> messages = new ArrayList<>();
                String sql = "SELECT * FROM chat_message WHERE receiver = ? ";

                if (sinceTimestamp != null && sinceTimestamp > 0) {
                    sql += "AND created_at >= datetime(?, 'unixepoch') ";
                }

                sql += "ORDER BY created_at";

                try (PreparedStatement ps = conn.prepareStatement(sql)) {
                    ps.setString(1, receiver);
                    if (sinceTimestamp != null && sinceTimestamp > 0) {
                        ps.setLong(2, sinceTimestamp);
                    }
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSetWithSessionId(rs, sessionId));
                        }
                    }
                }
                return messages;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find messages by session and receiver since timestamp: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public void markMessageAsSent(String sessionId, Long messageId) {
        try {
            sessionSQLiteManager.execute(sessionId, conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                    "UPDATE chat_message SET sent = TRUE, sent_at = ? WHERE id = ?")) {
                    ps.setLong(1, Instant.now().getEpochSecond());
                    ps.setLong(2, messageId);
                    ps.executeUpdate();
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to mark message as sent: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    /**
     * Close the session database connection.
     */
    public void closeSession(String sessionId) {
        sessionSQLiteManager.closeSession(sessionId);
    }
}