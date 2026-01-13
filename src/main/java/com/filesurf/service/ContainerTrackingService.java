package com.filesurf.service;

import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import org.eclipse.microprofile.config.inject.ConfigProperty;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.Timestamp;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.logging.Logger;

/**
 * Service for tracking Podman container state in a separate SQLite database.
 * 
 * This provides persistent tracking of session-to-container mappings, allowing
 * the application to recover container state after restarts without losing
 * track of running containers.
 * 
 * The database is separate from the main filesurf.db to:
 * - Keep container tracking concerns isolated
 * - Allow independent backup/rotation policies
 * - Avoid schema coupling with the main application database
 */
@ApplicationScoped
public class ContainerTrackingService {

    private static final Logger LOGGER = Logger.getLogger(ContainerTrackingService.class.getName());

    @ConfigProperty(name = "container.tracking.db.path", defaultValue = "data/containers.db")
    String dbPath;

    @ConfigProperty(name = "container.tracking.retention.days", defaultValue = "14")
    int retentionDays;

    private String jdbcUrl;

    /**
     * Container tracking record.
     */
    public static class ContainerRecord {
        public final String sessionId;
        public final String containerId;
        public final String containerName;
        public final String imageVersion;
        public final Instant createdAt;
        public final Instant stoppedAt;
        public final String status;

        public ContainerRecord(String sessionId, String containerId, String containerName,
                               String imageVersion, Instant createdAt, Instant stoppedAt, String status) {
            this.sessionId = sessionId;
            this.containerId = containerId;
            this.containerName = containerName;
            this.imageVersion = imageVersion;
            this.createdAt = createdAt;
            this.stoppedAt = stoppedAt;
            this.status = status;
        }
    }

    @PostConstruct
    public void init() {
        try {
            // Ensure parent directory exists
            Path dbFile = Path.of(dbPath);
            Path parentDir = dbFile.getParent();
            if (parentDir != null && !Files.exists(parentDir)) {
                Files.createDirectories(parentDir);
                LOGGER.info("Created container tracking database directory: " + parentDir);
            }

            jdbcUrl = "jdbc:sqlite:" + dbPath;
            
            // Initialize schema
            initializeSchema();
            
            // Run cleanup of old records
            int cleaned = cleanupOldRecords();
            if (cleaned > 0) {
                LOGGER.info("Cleaned up " + cleaned + " container tracking records older than " + retentionDays + " days");
            }
            
            LOGGER.info("ContainerTrackingService initialized with database: " + dbPath);
        } catch (IOException | SQLException e) {
            LOGGER.severe("Failed to initialize ContainerTrackingService: " + e.getMessage());
            throw new RuntimeException("Failed to initialize container tracking database", e);
        }
    }

    /**
     * Initialize the database schema.
     */
    private void initializeSchema() throws SQLException {
        try (Connection conn = getConnection();
             Statement stmt = conn.createStatement()) {
            
            // Enable WAL mode for better concurrency
            stmt.execute("PRAGMA journal_mode=WAL");
            
            // Create container_tracking table
            stmt.execute("""
                CREATE TABLE IF NOT EXISTS container_tracking (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    session_id TEXT NOT NULL UNIQUE,
                    container_id TEXT NOT NULL,
                    container_name TEXT NOT NULL,
                    image_version TEXT NOT NULL,
                    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    stopped_at TIMESTAMP,
                    status TEXT NOT NULL DEFAULT 'running'
                )
                """);
            
            // Create indexes for efficient queries
            stmt.execute("CREATE INDEX IF NOT EXISTS idx_container_tracking_created_at ON container_tracking(created_at)");
            stmt.execute("CREATE INDEX IF NOT EXISTS idx_container_tracking_status ON container_tracking(status)");
            stmt.execute("CREATE INDEX IF NOT EXISTS idx_container_tracking_session_id ON container_tracking(session_id)");
            
            LOGGER.fine("Container tracking database schema initialized");
        }
    }

    /**
     * Get a database connection.
     */
    private Connection getConnection() throws SQLException {
        return DriverManager.getConnection(jdbcUrl);
    }

    /**
     * Record a new container start.
     * 
     * @param sessionId The session ID
     * @param containerId The container ID (full hash)
     * @param containerName The container name (klawed-{sessionId})
     * @param imageVersion The image version used
     */
    public void recordContainerStart(String sessionId, String containerId, String containerName, String imageVersion) {
        String sql = """
            INSERT INTO container_tracking (session_id, container_id, container_name, image_version, created_at, status)
            VALUES (?, ?, ?, ?, ?, 'running')
            ON CONFLICT(session_id) DO UPDATE SET
                container_id = excluded.container_id,
                container_name = excluded.container_name,
                image_version = excluded.image_version,
                created_at = excluded.created_at,
                stopped_at = NULL,
                status = 'running'
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setString(1, sessionId);
            pstmt.setString(2, containerId);
            pstmt.setString(3, containerName);
            pstmt.setString(4, imageVersion);
            pstmt.setTimestamp(5, Timestamp.from(Instant.now()));
            
            pstmt.executeUpdate();
            LOGGER.fine("[SESSION:" + sessionId + "] Recorded container start: " + containerId);
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to record container start: " + e.getMessage());
        }
    }

    /**
     * Record a container stop.
     * 
     * @param sessionId The session ID
     * @param status The stop status ('stopped' or 'killed')
     */
    public void recordContainerStop(String sessionId, String status) {
        String sql = """
            UPDATE container_tracking 
            SET stopped_at = ?, status = ?
            WHERE session_id = ? AND status = 'running'
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setTimestamp(1, Timestamp.from(Instant.now()));
            pstmt.setString(2, status);
            pstmt.setString(3, sessionId);
            
            int updated = pstmt.executeUpdate();
            if (updated > 0) {
                LOGGER.fine("[SESSION:" + sessionId + "] Recorded container stop with status: " + status);
            }
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to record container stop: " + e.getMessage());
        }
    }

    /**
     * Record a container stop by container ID or name.
     * 
     * @param containerId The container ID or name
     * @param status The stop status ('stopped' or 'killed')
     */
    public void recordContainerStopByContainerId(String containerId, String status) {
        String sql = """
            UPDATE container_tracking 
            SET stopped_at = ?, status = ?
            WHERE (container_id = ? OR container_name = ?) AND status = 'running'
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setTimestamp(1, Timestamp.from(Instant.now()));
            pstmt.setString(2, status);
            pstmt.setString(3, containerId);
            pstmt.setString(4, containerId);
            
            int updated = pstmt.executeUpdate();
            if (updated > 0) {
                LOGGER.fine("Recorded container stop for " + containerId + " with status: " + status);
            }
        } catch (SQLException e) {
            LOGGER.warning("Failed to record container stop for " + containerId + ": " + e.getMessage());
        }
    }

    /**
     * Get all containers currently marked as running in the database.
     * 
     * @return List of container records with status='running'
     */
    public List<ContainerRecord> getRunningContainers() {
        List<ContainerRecord> records = new ArrayList<>();
        String sql = """
            SELECT session_id, container_id, container_name, image_version, created_at, stopped_at, status
            FROM container_tracking
            WHERE status = 'running'
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql);
             ResultSet rs = pstmt.executeQuery()) {
            
            while (rs.next()) {
                records.add(mapResultSetToRecord(rs));
            }
        } catch (SQLException e) {
            LOGGER.warning("Failed to get running containers: " + e.getMessage());
        }
        
        return records;
    }

    /**
     * Get container record by session ID.
     * 
     * @param sessionId The session ID
     * @return Optional container record
     */
    public Optional<ContainerRecord> getContainerBySessionId(String sessionId) {
        String sql = """
            SELECT session_id, container_id, container_name, image_version, created_at, stopped_at, status
            FROM container_tracking
            WHERE session_id = ?
            ORDER BY created_at DESC
            LIMIT 1
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setString(1, sessionId);
            
            try (ResultSet rs = pstmt.executeQuery()) {
                if (rs.next()) {
                    return Optional.of(mapResultSetToRecord(rs));
                }
            }
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to get container record: " + e.getMessage());
        }
        
        return Optional.empty();
    }

    /**
     * Get running container record by session ID.
     * 
     * @param sessionId The session ID
     * @return Optional container record if session has a running container
     */
    public Optional<ContainerRecord> getRunningContainerBySessionId(String sessionId) {
        String sql = """
            SELECT session_id, container_id, container_name, image_version, created_at, stopped_at, status
            FROM container_tracking
            WHERE session_id = ? AND status = 'running'
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setString(1, sessionId);
            
            try (ResultSet rs = pstmt.executeQuery()) {
                if (rs.next()) {
                    return Optional.of(mapResultSetToRecord(rs));
                }
            }
        } catch (SQLException e) {
            LOGGER.warning("[SESSION:" + sessionId + "] Failed to get running container record: " + e.getMessage());
        }
        
        return Optional.empty();
    }

    /**
     * Check if a session has a container marked as running in the database.
     * 
     * @param sessionId The session ID
     * @return true if session has a running container record
     */
    public boolean hasRunningContainer(String sessionId) {
        return getRunningContainerBySessionId(sessionId).isPresent();
    }

    /**
     * Get all session IDs that have containers marked as running.
     * 
     * @return List of session IDs
     */
    public List<String> getRunningSessionIds() {
        List<String> sessionIds = new ArrayList<>();
        String sql = "SELECT session_id FROM container_tracking WHERE status = 'running'";
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql);
             ResultSet rs = pstmt.executeQuery()) {
            
            while (rs.next()) {
                sessionIds.add(rs.getString("session_id"));
            }
        } catch (SQLException e) {
            LOGGER.warning("Failed to get running session IDs: " + e.getMessage());
        }
        
        return sessionIds;
    }

    /**
     * Mark all running containers as stopped (used during cleanup).
     * 
     * @param status The status to set ('stopped', 'killed', 'orphaned')
     * @return Number of records updated
     */
    public int markAllRunningAsStopped(String status) {
        String sql = """
            UPDATE container_tracking 
            SET stopped_at = ?, status = ?
            WHERE status = 'running'
            """;
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setTimestamp(1, Timestamp.from(Instant.now()));
            pstmt.setString(2, status);
            
            return pstmt.executeUpdate();
        } catch (SQLException e) {
            LOGGER.warning("Failed to mark all running containers as stopped: " + e.getMessage());
            return 0;
        }
    }

    /**
     * Cleanup records older than the retention period.
     * 
     * @return Number of records deleted
     */
    public int cleanupOldRecords() {
        Instant cutoff = Instant.now().minus(retentionDays, ChronoUnit.DAYS);
        String sql = "DELETE FROM container_tracking WHERE created_at < ?";
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            pstmt.setTimestamp(1, Timestamp.from(cutoff));
            
            return pstmt.executeUpdate();
        } catch (SQLException e) {
            LOGGER.warning("Failed to cleanup old container tracking records: " + e.getMessage());
            return 0;
        }
    }

    /**
     * Get count of records by status.
     * 
     * @param status The status to count (or null for all)
     * @return Record count
     */
    public int getRecordCount(String status) {
        String sql = status != null 
            ? "SELECT COUNT(*) FROM container_tracking WHERE status = ?"
            : "SELECT COUNT(*) FROM container_tracking";
        
        try (Connection conn = getConnection();
             PreparedStatement pstmt = conn.prepareStatement(sql)) {
            
            if (status != null) {
                pstmt.setString(1, status);
            }
            
            try (ResultSet rs = pstmt.executeQuery()) {
                if (rs.next()) {
                    return rs.getInt(1);
                }
            }
        } catch (SQLException e) {
            LOGGER.warning("Failed to get record count: " + e.getMessage());
        }
        
        return 0;
    }

    /**
     * Map a ResultSet row to a ContainerRecord.
     */
    private ContainerRecord mapResultSetToRecord(ResultSet rs) throws SQLException {
        Timestamp stoppedAt = rs.getTimestamp("stopped_at");
        return new ContainerRecord(
            rs.getString("session_id"),
            rs.getString("container_id"),
            rs.getString("container_name"),
            rs.getString("image_version"),
            rs.getTimestamp("created_at").toInstant(),
            stoppedAt != null ? stoppedAt.toInstant() : null,
            rs.getString("status")
        );
    }
}
