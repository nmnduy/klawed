package com.filesurf.repository;

import com.filesurf.db.SessionSQLiteManager;
import com.filesurf.model.ChatSessionRecord;
import com.filesurf.model.ChatMessageRecord;
import com.filesurf.model.ChatConstants;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import java.sql.*;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

/**
 * Repository for managing chat messages in per-session SQLite databases.
 * Each session has its own database file in the chat-messages directory.
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
        try {
            sessionSQLiteManager.execute(sessionId, conn -> {
                try (Statement stmt = conn.createStatement()) {
                    // Create chat_session table (local copy for this session)
                    stmt.execute("""
                        CREATE TABLE IF NOT EXISTS chat_session (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            session_id TEXT NOT NULL UNIQUE,
                            client_identity TEXT,
                            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
                            last_activity_at TIMESTAMP,
                            is_active BOOLEAN NOT NULL DEFAULT TRUE
                        )
                    """);

                    // Create chat_message table (identical to main database schema)
                    stmt.execute("""
                        CREATE TABLE IF NOT EXISTS chat_message (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            chat_session_id INTEGER NOT NULL,
                            sender TEXT NOT NULL,
                            receiver TEXT NOT NULL,
                            content TEXT NOT NULL,
                            message_type TEXT,
                            sent BOOLEAN NOT NULL DEFAULT FALSE,
                            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
                            sent_at TIMESTAMP,
                            FOREIGN KEY (chat_session_id) REFERENCES chat_session(id) ON DELETE CASCADE
                        )
                    """);

                    // Create indexes for performance
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_chat_session_session_id ON chat_session(session_id)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_chat_session_is_active ON chat_session(is_active)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_chat_message_chat_session_id ON chat_message(chat_session_id)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_chat_message_sent ON chat_message(sent)");
                    stmt.execute("CREATE INDEX IF NOT EXISTS idx_chat_message_created_at ON chat_message(created_at)");

                    LOGGER.info("[SESSION:" + sessionId + "] Database schema initialized successfully");
                }
                return null;
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to initialize database schema: " + e.getMessage());
            throw new RuntimeException("Database initialization failed for session " + sessionId, e);
        }
    }

    /**
     * Create or update a chat session in the session's database.
     */
    public ChatSessionRecord createOrUpdateChatSession(String sessionId, String clientIdentity) {
        try {
            return sessionSQLiteManager.executeInTransaction(sessionId, conn -> {
                // Schema is now initialized automatically when connection is created
                
                // Check if session exists
                ChatSessionRecord existing = findChatSessionBySessionId(conn, sessionId);

                if (existing != null) {
                    // Update existing session
                    try (PreparedStatement ps = conn.prepareStatement(
                        "UPDATE chat_session SET client_identity = ?, last_activity_at = ?, is_active = TRUE WHERE session_id = ?")) {
                        ps.setString(1, clientIdentity);
                        ps.setTimestamp(2, Timestamp.valueOf(LocalDateTime.now()));
                        ps.setString(3, sessionId);
                        ps.executeUpdate();
                    }
                    return findChatSessionBySessionId(conn, sessionId);
                } else {
                    // Insert new session
                    try (PreparedStatement ps = conn.prepareStatement(
                        "INSERT INTO chat_session (session_id, client_identity, created_at, last_activity_at, is_active) VALUES (?, ?, ?, ?, ?)",
                        Statement.RETURN_GENERATED_KEYS)) {
                        LocalDateTime now = LocalDateTime.now();
                        ps.setString(1, sessionId);
                        ps.setString(2, clientIdentity);
                        ps.setTimestamp(3, Timestamp.valueOf(now));
                        ps.setTimestamp(4, Timestamp.valueOf(now));
                        ps.setBoolean(5, true);
                        ps.executeUpdate();

                        // Get generated ID
                        try (ResultSet rs = ps.getGeneratedKeys()) {
                            if (rs.next()) {
                                Long id = rs.getLong(1);
                                return findChatSessionById(conn, id);
                            }
                        }
                    }
                    return null;
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to create/update chat session: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    public ChatSessionRecord findChatSessionBySessionId(String sessionId) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<ChatSessionRecord>) 
                conn -> findChatSessionBySessionId(conn, sessionId));
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find chat session: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    private ChatSessionRecord findChatSessionBySessionId(Connection conn, String sessionId) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
            "SELECT * FROM chat_session WHERE session_id = ?")) {
            ps.setString(1, sessionId);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return ChatSessionRecord.fromResultSet(rs);
                }
            }
        }
        return null;
    }

    private ChatSessionRecord findChatSessionById(Connection conn, Long id) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
            "SELECT * FROM chat_session WHERE id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return ChatSessionRecord.fromResultSet(rs);
                }
            }
        }
        return null;
    }

    public void deactivateChatSession(String sessionId) {
        try {
            sessionSQLiteManager.execute(sessionId, conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                    "UPDATE chat_session SET is_active = FALSE, last_activity_at = ? WHERE session_id = ?")) {
                    ps.setTimestamp(1, Timestamp.valueOf(LocalDateTime.now()));
                    ps.setString(2, sessionId);
                    ps.executeUpdate();
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to deactivate chat session: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    /**
     * Check if a chat session is still active in the session's database.
     */
    public boolean isChatSessionActive(String sessionId) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<Boolean>) conn -> {
                try (PreparedStatement ps = conn.prepareStatement(
                    "SELECT is_active FROM chat_session WHERE session_id = ?")) {
                    ps.setString(1, sessionId);
                    try (ResultSet rs = ps.executeQuery()) {
                        if (rs.next()) {
                            return rs.getBoolean("is_active");
                        }
                        return false; // Session not found
                    }
                }
            });
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to check if chat session is active: " + e.getMessage());
            return false; // On error, assume not active
        }
    }

    /**
     * Create a chat message in the session's database.
     */
    public ChatMessageRecord createChatMessage(String sessionId, String sender, String receiver,
                                              String content, String messageType) {
        try {
            return sessionSQLiteManager.executeInTransaction(sessionId, conn -> {
                // First get the session ID (not session_id string, but the numeric ID)
                Long sessionNumericId = getSessionNumericId(conn, sessionId);
                if (sessionNumericId == null) {
                    // Create session if it doesn't exist
                    ChatSessionRecord session = createOrUpdateChatSession(sessionId, "unknown");
                    sessionNumericId = getSessionNumericId(conn, sessionId);
                    if (sessionNumericId == null) {
                        throw new IllegalArgumentException("Failed to create session: " + sessionId);
                    }
                }

                // Insert new message
                try (PreparedStatement ps = conn.prepareStatement(
                    "INSERT INTO chat_message (chat_session_id, sender, receiver, content, message_type, created_at) VALUES (?, ?, ?, ?, ?, ?)",
                    Statement.RETURN_GENERATED_KEYS)) {
                    ps.setLong(1, sessionNumericId);
                    ps.setString(2, sender);
                    ps.setString(3, receiver);
                    ps.setString(4, content);
                    ps.setString(5, messageType);
                    ps.setTimestamp(6, Timestamp.valueOf(LocalDateTime.now()));
                    ps.executeUpdate();

                    // Get generated ID
                    try (ResultSet rs = ps.getGeneratedKeys()) {
                        if (rs.next()) {
                            Long id = rs.getLong(1);
                            return findChatMessageById(conn, id);
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

    private Long getSessionNumericId(Connection conn, String sessionId) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
            "SELECT id FROM chat_session WHERE session_id = ?")) {
            ps.setString(1, sessionId);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return rs.getLong("id");
                }
            }
        }
        return null;
    }

    public ChatMessageRecord findChatMessageById(String sessionId, Long id) {
        try {
            return sessionSQLiteManager.execute(sessionId, (SessionSQLiteManager.ConnectionConsumer<ChatMessageRecord>) 
                conn -> findChatMessageById(conn, id));
        } catch (SQLException e) {
            LOGGER.severe("[SESSION:" + sessionId + "] Failed to find chat message: " + e.getMessage());
            throw new RuntimeException("Database operation failed for session " + sessionId, e);
        }
    }

    private ChatMessageRecord findChatMessageById(Connection conn, Long id) throws SQLException {
        try (PreparedStatement ps = conn.prepareStatement(
            "SELECT cm.*, cs.session_id as session_string_id FROM chat_message cm " +
            "JOIN chat_session cs ON cm.chat_session_id = cs.id WHERE cm.id = ?")) {
            ps.setLong(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                if (rs.next()) {
                    return ChatMessageRecord.fromResultSet(rs);
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
                    "SELECT cm.*, cs.session_id as session_string_id FROM chat_message cm " +
                    "JOIN chat_session cs ON cm.chat_session_id = cs.id " +
                    "WHERE cm.sent = FALSE AND cm.receiver = ? ORDER BY cm.created_at")) {
                    ps.setString(1, ChatConstants.CLIENT);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSet(rs));
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
                    "SELECT cm.*, cs.session_id as session_string_id FROM chat_message cm " +
                    "JOIN chat_session cs ON cm.chat_session_id = cs.id " +
                    "WHERE cs.session_id = ? ORDER BY cm.created_at")) {
                    ps.setString(1, sessionId);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSet(rs));
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
                    "SELECT cm.*, cs.session_id as session_string_id FROM chat_message cm " +
                    "JOIN chat_session cs ON cm.chat_session_id = cs.id " +
                    "WHERE cs.session_id = ? AND cm.sender = ? ORDER BY cm.created_at")) {
                    ps.setString(1, sessionId);
                    ps.setString(2, sender);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSet(rs));
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
                    "SELECT cm.*, cs.session_id as session_string_id FROM chat_message cm " +
                    "JOIN chat_session cs ON cm.chat_session_id = cs.id " +
                    "WHERE cs.session_id = ? AND cm.receiver = ? ORDER BY cm.created_at")) {
                    ps.setString(1, sessionId);
                    ps.setString(2, receiver);
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSet(rs));
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
                String sql = "SELECT cm.*, cs.session_id as session_string_id FROM chat_message cm " +
                           "JOIN chat_session cs ON cm.chat_session_id = cs.id " +
                           "WHERE cs.session_id = ? AND cm.receiver = ? ";

                if (sinceTimestamp != null && sinceTimestamp > 0) {
                    sql += "AND cm.created_at >= datetime(?, 'unixepoch') ";
                }

                sql += "ORDER BY cm.created_at";

                try (PreparedStatement ps = conn.prepareStatement(sql)) {
                    ps.setString(1, sessionId);
                    ps.setString(2, receiver);
                    if (sinceTimestamp != null && sinceTimestamp > 0) {
                        ps.setLong(3, sinceTimestamp);
                    }
                    try (ResultSet rs = ps.executeQuery()) {
                        while (rs.next()) {
                            messages.add(ChatMessageRecord.fromResultSet(rs));
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
                    ps.setTimestamp(1, Timestamp.valueOf(LocalDateTime.now()));
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