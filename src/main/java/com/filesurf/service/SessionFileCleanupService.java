package com.filesurf.service;

import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;
import io.quarkus.scheduler.Scheduled;

import java.io.IOException;
import java.nio.file.*;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.logging.Logger;
import java.util.stream.Stream;

/**
 * Service for cleaning up orphaned session files, particularly SQLite databases.
 * Runs periodically to clean up files from sessions that are no longer active.
 */
@ApplicationScoped
public class SessionFileCleanupService {

    private static final Logger LOGGER = Logger.getLogger(SessionFileCleanupService.class.getName());

    @Inject
    FileChatService fileChatService;

    @Inject
    SessionManager sessionManager;

    @Inject
    KlawedAgentManager klawedAgentManager;

    // Configuration
    private static final long CLEANUP_INTERVAL_HOURS = 6; // Run every 6 hours
    private static final long MAX_SESSION_AGE_HOURS = 24; // Clean sessions older than 24 hours
    private static final Path SESSION_BASE_DIR = Path.of("/tmp/is-sessions");

    /**
     * Initialize the cleanup service
     */
    @PostConstruct
    void init() {
        LOGGER.info("SessionFileCleanupService initialized");
        LOGGER.info("Will clean sessions older than " + MAX_SESSION_AGE_HOURS + " hours");
    }

    /**
     * Scheduled cleanup task
     */
    @Scheduled(every = CLEANUP_INTERVAL_HOURS + "h", delayed = "5m")
    void scheduledCleanup() {
        LOGGER.info("Starting scheduled session file cleanup");
        try {
            CleanupStats stats = cleanupOrphanedSessionFiles();
            LOGGER.info("Scheduled cleanup completed: " + stats);
        } catch (Exception e) {
            LOGGER.severe("Error during scheduled cleanup: " + e.getMessage());
        }
    }

    /**
     * Clean up orphaned session files
     * @return Cleanup statistics
     */
    public CleanupStats cleanupOrphanedSessionFiles() throws IOException {
        CleanupStats stats = new CleanupStats();
        
        if (!Files.exists(SESSION_BASE_DIR)) {
            LOGGER.info("Session base directory does not exist: " + SESSION_BASE_DIR);
            return stats;
        }

        Instant cutoffTime = Instant.now().minus(MAX_SESSION_AGE_HOURS, ChronoUnit.HOURS);
        
        try (Stream<Path> dirStream = Files.list(SESSION_BASE_DIR)) {
            dirStream.filter(Files::isDirectory)
                    .forEach(sessionDir -> {
                        try {
                            processSessionDirectory(sessionDir, cutoffTime, stats);
                        } catch (Exception e) {
                            LOGGER.warning("Error processing session directory " + sessionDir + ": " + e.getMessage());
                            stats.errors++;
                        }
                    });
        }

        LOGGER.info("Cleanup completed: " + stats);
        return stats;
    }

    /**
     * Process a single session directory
     */
    private void processSessionDirectory(Path sessionDir, Instant cutoffTime, CleanupStats stats) throws IOException {
        String sessionId = sessionDir.getFileName().toString();
        
        // Check if directory is old enough to consider for cleanup
        Instant lastModified = Files.getLastModifiedTime(sessionDir).toInstant();
        if (lastModified.isAfter(cutoffTime)) {
            LOGGER.fine("Session directory too recent, skipping: " + sessionId);
            stats.skippedRecent++;
            return;
        }

        // Check if session is still active in the database
        if (fileChatService.isChatSessionActive(sessionId)) {
            LOGGER.info("Session " + sessionId + " is still active in database, skipping cleanup");
            stats.skippedActive++;
            return;
        }

        // Check if klawed agent is still running for this session
        try {
            if (klawedAgentManager.isAgentAlive(sessionId)) {
                LOGGER.info("Klawed agent still alive for session " + sessionId + ", skipping cleanup");
                stats.skippedAgentAlive++;
                return;
            }
        } catch (IOException e) {
            LOGGER.warning("Error checking agent status for session " + sessionId + ": " + e.getMessage());
        }

        // Session appears to be orphaned - clean it up
        LOGGER.info("Cleaning up orphaned session: " + sessionId);
        cleanupSessionFiles(sessionDir, stats);
    }

    /**
     * Clean up files in a session directory
     */
    private void cleanupSessionFiles(Path sessionDir, CleanupStats stats) throws IOException {
        String sessionId = sessionDir.getFileName().toString();
        
        // First, try to delete SQLite files specifically
        deleteSqliteFiles(sessionDir, stats);
        
        // Then try to delete the entire directory
        try {
            sessionManager.deleteSessionDirectory(sessionDir);
            stats.directoriesDeleted++;
            LOGGER.info("Successfully deleted session directory: " + sessionId);
        } catch (IOException e) {
            LOGGER.warning("Failed to delete session directory " + sessionId + ": " + e.getMessage());
            stats.errors++;
            
            // If directory deletion fails, at least clean up tmp folder
            cleanupTmpFolder(sessionDir, stats);
        }
    }

    /**
     * Delete SQLite database files
     */
    private void deleteSqliteFiles(Path sessionDir, CleanupStats stats) {
        try (Stream<Path> fileStream = Files.walk(sessionDir)) {
            fileStream.filter(path -> {
                        String fileName = path.getFileName().toString();
                        return fileName.endsWith(".db") || 
                               fileName.endsWith(".db-shm") || 
                               fileName.endsWith(".db-wal") ||
                               fileName.endsWith(".sqlite") ||
                               fileName.endsWith(".sqlite3");
                    })
                    .forEach(path -> {
                        try {
                            if (Files.deleteIfExists(path)) {
                                stats.sqliteFilesDeleted++;
                                LOGGER.fine("Deleted SQLite file: " + path.getFileName());
                            }
                        } catch (IOException e) {
                            LOGGER.warning("Failed to delete SQLite file " + path + ": " + e.getMessage());
                            stats.errors++;
                        }
                    });
        } catch (IOException e) {
            LOGGER.warning("Error walking session directory for SQLite files: " + e.getMessage());
            stats.errors++;
        }
    }

    /**
     * Clean up tmp folder if directory deletion failed
     */
    private void cleanupTmpFolder(Path sessionDir, CleanupStats stats) {
        Path tmpDir = sessionDir.resolve("tmp");
        if (Files.exists(tmpDir)) {
            try {
                sessionManager.deleteSessionDirectory(tmpDir);
                stats.tmpFoldersCleaned++;
                LOGGER.info("Cleaned up tmp folder for session: " + sessionDir.getFileName());
            } catch (IOException e) {
                LOGGER.warning("Failed to clean up tmp folder: " + tmpDir + ": " + e.getMessage());
                stats.errors++;
            }
        }
    }

    /**
     * Manual cleanup trigger
     */
    public CleanupStats cleanupNow() throws IOException {
        LOGGER.info("Manual cleanup triggered");
        return cleanupOrphanedSessionFiles();
    }

    /**
     * Get statistics about session directories
     */
    public SessionStats getSessionStats() throws IOException {
        SessionStats stats = new SessionStats();
        
        if (!Files.exists(SESSION_BASE_DIR)) {
            return stats;
        }

        Instant now = Instant.now();
        Instant oneDayAgo = now.minus(1, ChronoUnit.DAYS);
        Instant oneWeekAgo = now.minus(7, ChronoUnit.DAYS);

        try (Stream<Path> dirStream = Files.list(SESSION_BASE_DIR)) {
            dirStream.filter(Files::isDirectory)
                    .forEach(dir -> {
                        try {
                            stats.totalSessions++;
                            
                            Instant lastModified = Files.getLastModifiedTime(dir).toInstant();
                            if (lastModified.isBefore(oneWeekAgo)) {
                                stats.olderThanWeek++;
                            } else if (lastModified.isBefore(oneDayAgo)) {
                                stats.olderThanDay++;
                            }
                            
                            // Count SQLite files
                            try (Stream<Path> fileStream = Files.walk(dir)) {
                                long sqliteCount = fileStream.filter(path -> {
                                            String fileName = path.getFileName().toString();
                                            return fileName.endsWith(".db") || 
                                                   fileName.endsWith(".db-shm") || 
                                                   fileName.endsWith(".db-wal");
                                        })
                                        .count();
                                stats.totalSqliteFiles += sqliteCount;
                            }
                            
                        } catch (IOException e) {
                            LOGGER.warning("Error getting stats for directory " + dir + ": " + e.getMessage());
                        }
                    });
        }

        return stats;
    }

    /**
     * Cleanup statistics
     */
    public static class CleanupStats {
        public int directoriesDeleted = 0;
        public int sqliteFilesDeleted = 0;
        public int tmpFoldersCleaned = 0;
        public int skippedRecent = 0;
        public int skippedActive = 0;
        public int skippedAgentAlive = 0;
        public int errors = 0;

        @Override
        public String toString() {
            return String.format(
                "CleanupStats{directoriesDeleted=%d, sqliteFilesDeleted=%d, tmpFoldersCleaned=%d, " +
                "skippedRecent=%d, skippedActive=%d, skippedAgentAlive=%d, errors=%d}",
                directoriesDeleted, sqliteFilesDeleted, tmpFoldersCleaned,
                skippedRecent, skippedActive, skippedAgentAlive, errors
            );
        }
    }

    /**
     * Session statistics
     */
    public static class SessionStats {
        public int totalSessions = 0;
        public int olderThanDay = 0;
        public int olderThanWeek = 0;
        public long totalSqliteFiles = 0;

        @Override
        public String toString() {
            return String.format(
                "SessionStats{totalSessions=%d, olderThanDay=%d, olderThanWeek=%d, totalSqliteFiles=%d}",
                totalSessions, olderThanDay, olderThanWeek, totalSqliteFiles
            );
        }
    }
}